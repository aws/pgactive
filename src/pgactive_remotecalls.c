
/* -------------------------------------------------------------------------
 *
 * pgactive_remotecalls.c
 *     Make libpq requests to a remote pgactive instance
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_remotecalls.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pgactive.h"
#include "pgactive_internal.h"

#include "fmgr.h"
#include "funcapi.h"
#include "libpq-fe.h"
#include "miscadmin.h"

#include "libpq/pqformat.h"

#include "access/heapam.h"
#include "access/xact.h"

#include "catalog/pg_type.h"

#include "executor/spi.h"

#include "replication/origin.h"
#include "replication/walreceiver.h"

#include "postmaster/bgworker.h"
#include "postmaster/bgwriter.h"

#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

#include "utils/builtins.h"
#include "utils/pg_lsn.h"

PGDLLEXPORT Datum pgactive_get_node_info(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pgactive_get_node_info);

/*
 * Make standard postgres connection, ERROR on failure.
 */
PGconn *
pgactive_connect_nonrepl(const char *connstring, const char *appnamesuffix, bool report_fatal)
{
	PGconn	   *nonrepl_conn;
	StringInfoData dsn;
	char	   *servername;

	servername = get_connect_string(connstring);

	initStringInfo(&dsn);
	appendStringInfo(&dsn, "%s %s %s application_name='%s:%s'",
					 pgactive_default_apply_connection_options,
					 pgactive_extra_apply_connection_options,
					 (servername == NULL ? connstring : servername),
					 pgactive_get_my_cached_node_name(), appnamesuffix);

	/*
	 * Test to see if there's an entry in the remote's pgactive.pgactive_nodes
	 * for our system identifier. If there is, that'll tell us what stage of
	 * startup we are up to and let us resume an incomplete start.
	 */
	nonrepl_conn = PQconnectdb(dsn.data);
	if (PQstatus(nonrepl_conn) != CONNECTION_OK && report_fatal)
	{
		ereport(FATAL,
				(errmsg("could not connect to the server in non-replication mode: %s",
						GetPQerrorMessage(nonrepl_conn))));
	}

	pfree(dsn.data);

	return nonrepl_conn;
}

/*
 * Close a connection if it exists. The connection passed
 * is a pointer to a *PGconn; if the target is NULL, it's
 * presumed not inited or already closed and is ignored.
 */
void
pgactive_cleanup_conn_close(int code, Datum connptr)
{
	PGconn	  **conn_p;
	PGconn	   *conn;

	conn_p = (PGconn **) DatumGetPointer(connptr);
	Assert(conn_p != NULL);
	conn = *conn_p;

	if (conn == NULL)
		return;
	if (PQstatus(conn) != CONNECTION_OK)
		return;
	PQfinish(conn);
}

/*
 * Frees contents of a remote_node_info (but not the struct its self)
 */
void
free_remote_node_info(remote_node_info * ri)
{
	if (ri->sysid_str != NULL)
		pfree(ri->sysid_str);
	if (ri->variant != NULL)
		pfree(ri->variant);
	if (ri->version != NULL)
		pfree(ri->version);
	if (ri->node_name != NULL)
		pfree(ri->node_name);
	if (ri->dbname != NULL)
		pfree(ri->dbname);
}

/*
 * Given two connections, execute a COPY ... TO stdout on one connection
 * and feed the results to a COPY ... FROM stdin on the other connection
 * for the purpose of copying a set of rows between two nodes.
 *
 * It copies pgactive_connections entries from the remote table to the
 * local table of the same name, optionally with a filtering query.
 *
 * "from" here is from the client perspective, i.e. to copy from
 * the server we "COPY ... TO stdout", and to copy to the server we
 * "COPY ... FROM stdin".
 *
 * On failure an ERROR will be raised.
 *
 * Note that query parameters are not supported for COPY, so values must be
 * carefully interpolated into the SQL if you're using a query, not just a
 * table name. Be careful of SQL injection opportunities.
 */
void
pgactive_copytable(PGconn *copyfrom_conn, PGconn *copyto_conn,
				   const char *copyfrom_query, const char *copyto_query)
{
	PGresult   *copyfrom_result;
	PGresult   *copyto_result;
	int			copyinresult,
				copyoutresult;
	char	   *copybuf;

	copyfrom_result = PQexec(copyfrom_conn, copyfrom_query);
	if (PQresultStatus(copyfrom_result) != PGRES_COPY_OUT)
	{
		ereport(ERROR,
				(errmsg("execution of COPY ... TO stdout failed"),
				 errdetail("Query '%s': %s", copyfrom_query,
						   PQerrorMessage(copyfrom_conn))));
	}

	copyto_result = PQexec(copyto_conn, copyto_query);
	if (PQresultStatus(copyto_result) != PGRES_COPY_IN)
	{
		ereport(ERROR,
				(errmsg("execution of COPY ... FROM stdout failed"),
				 errdetail("Query '%s': %s", copyto_query,
						   PQerrorMessage(copyto_conn))));
	}

	while ((copyoutresult = PQgetCopyData(copyfrom_conn, &copybuf, false)) > 0)
	{
		if ((copyinresult = PQputCopyData(copyto_conn, copybuf, copyoutresult)) != 1)
		{
			ereport(ERROR,
					(errmsg("writing to destination table failed"),
					 errdetail("Destination connection reported: %s",
							   PQerrorMessage(copyto_conn))));
		}
		PQfreemem(copybuf);
	}

	if (copyoutresult != -1)
	{
		ereport(ERROR,
				(errmsg("reading from origin table/query failed"),
				 errdetail("Source connection returned %d: %s",
						   copyoutresult, PQerrorMessage(copyfrom_conn))));
	}

	/* Send local finish */
	if (PQputCopyEnd(copyto_conn, NULL) != 1)
	{
		ereport(ERROR,
				(errmsg("sending copy-completion to destination connection failed"),
				 errdetail("Destination connection reported: %s",
						   PQerrorMessage(copyto_conn))));
	}
}

/*
 * The implementation guts of pgactive_get_node_info, callable with a
 * pre-existing connection.
 */
void
pgactive_get_remote_nodeinfo_internal(PGconn *conn, struct remote_node_info *ri)
{
	PGresult   *res;
	int			i;
	char	   *remote_pgactive_version_str;
	int			parsed_version_num;

	/* Make sure pgactive is actually present and active on the remote */
	pgactive_ensure_ext_installed(conn);

	/*
	 * Acquire remote node information. With this, we can also safely find out
	 * if we're superuser at this point.
	 */
	res = PQexec(conn, "SELECT pgactive.pgactive_version(), pgactive.pgactive_version_num(), "
				 "pgactive.pgactive_variant(), pgactive.pgactive_min_remote_version_num(), "
				 "pgactive._pgactive_has_required_privs() AS hasrequiredprivs, "
				 "pgactive.pgactive_get_local_node_name() AS node_name, "
				 "current_database()::text AS dbname, "
				 "pg_database_size(current_database()) AS dbsize, "
				 "current_setting('pgactive.max_nodes') AS max_nodes, "
				 "current_setting('pgactive.skip_ddl_replication') AS skip_ddl_replication, "
				 "count(1) FROM pgactive.pgactive_nodes WHERE node_status NOT IN (pgactive.pgactive_node_status_to_char('pgactive_NODE_STATUS_KILLED'));");

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
		ereport(ERROR,
				(errmsg("unable to get pgactive information from remote node"),
				 errdetail("Querying remote failed with: %s", PQerrorMessage(conn))));

	Assert(PQnfields(res) == 11);
	Assert(PQntuples(res) == 1);
	remote_pgactive_version_str = PQgetvalue(res, 0, 0);
	ri->version = pstrdup(remote_pgactive_version_str);
	ri->version_num = atoi(PQgetvalue(res, 0, 1));
	ri->variant = pstrdup(PQgetvalue(res, 0, 2));
	ri->min_remote_version_num = atoi(PQgetvalue(res, 0, 3));
	ri->has_required_privs = DatumGetBool(
										  DirectFunctionCall1(boolin, CStringGetDatum(PQgetvalue(res, 0, 4))));
	ri->node_name = pstrdup(PQgetvalue(res, 0, 5));
	ri->dbname = pstrdup(PQgetvalue(res, 0, 6));
	ri->dbsize = DatumGetInt64(
							   DirectFunctionCall1(int8in, CStringGetDatum(PQgetvalue(res, 0, 7))));
	ri->max_nodes = DatumGetInt32(
								  DirectFunctionCall1(int4in, CStringGetDatum(PQgetvalue(res, 0, 8))));
	ri->skip_ddl_replication = DatumGetBool(
											DirectFunctionCall1(boolin, CStringGetDatum(PQgetvalue(res, 0, 9))));
	ri->cur_nodes = DatumGetInt32(
								  DirectFunctionCall1(int4in, CStringGetDatum(PQgetvalue(res, 0, 10))));
	PQclear(res);

	/*
	 * Even though we should be able to get it from pgactive_version_num,
	 * always parse the pgactive version so that the parse code gets sanity
	 * checked, and so that we notice if the remote version is too old to have
	 * pgactive_version_num.
	 */
	parsed_version_num = pgactive_parse_version(ri->version, NULL, NULL,
												NULL, NULL);

	if (ri->version_num != parsed_version_num)
		elog(WARNING, "parsed pgactive version %d from string %s != returned pgactive version %d",
			 parsed_version_num, remote_pgactive_version_str, ri->version_num);

	res = PQexec(conn, "SELECT datcollate, datctype FROM pg_database "
				 "WHERE datname = current_database();");

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
		ereport(ERROR,
				(errmsg("unable to get database collation information from remote node"),
				 errdetail("Querying remote failed with: %s", PQerrorMessage(conn))));

	Assert(PQnfields(res) == 2);
	Assert(PQntuples(res) == 1);
	ri->datcollate =
		PQgetisnull(res, 0, 0) ? NULL : pstrdup(PQgetvalue(res, 0, 0));
	ri->datctype =
		PQgetisnull(res, 0, 1) ? NULL : pstrdup(PQgetvalue(res, 0, 1));
	PQclear(res);

	/* Get the remote node identity */
	res = PQexec(conn, "SELECT sysid, timeline, dboid "
				 "FROM pgactive.pgactive_get_local_nodeid();");

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
		ereport(ERROR,
				(errmsg("unable to get remote node identity"),
				 errdetail("Querying remote failed with: %s", PQerrorMessage(conn))));

	Assert(PQnfields(res) == 3);
	Assert(PQntuples(res) == 1);

	for (i = 0; i < 3; i++)
	{
		if (PQgetisnull(res, 0, i))
			elog(ERROR, "unexpectedly null field %s", PQfname(res, i));
	}

	ri->sysid_str = pstrdup(PQgetvalue(res, 0, 0));
	if (sscanf(ri->sysid_str, UINT64_FORMAT, &ri->nodeid.sysid) != 1)
		elog(ERROR, "could not parse remote sysid %s", ri->sysid_str);

	ri->nodeid.timeline = DatumGetObjectId(
										   DirectFunctionCall1(oidin, CStringGetDatum(PQgetvalue(res, 0, 1))));
	ri->nodeid.dboid = DatumGetObjectId(
										DirectFunctionCall1(oidin, CStringGetDatum(PQgetvalue(res, 0, 2))));
	PQclear(res);

	/* Get the remote node status */
	res = PQexec(conn, "SELECT node_status FROM pgactive.pgactive_nodes WHERE "
				 "(node_sysid, node_timeline, node_dboid) = pgactive.pgactive_get_local_nodeid();");

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
		ereport(ERROR,
				(errmsg("unable to get remote node status"),
				 errdetail("Querying remote failed with: %s", PQerrorMessage(conn))));

	Assert(PQnfields(res) == 1);

	if (PQntuples(res) == 0)
	{
		/* This happens when creating first node in pgactive group */
		ri->node_status = '\0';
	}
	else if (PQntuples(res) == 1)
	{
		if (PQgetisnull(res, 0, 0))
			elog(ERROR, "unexpectedly null field node_status in pgactive.pgactive_nodes");

		ri->node_status = PQgetvalue(res, 0, 0)[0];
	}
	else
		elog(ERROR, "got more than one pgactive.pgactive_nodes row matching local nodeid"); /* shouldn't happen */

	PQclear(res);

	/* Fetch total indexes size from remote node */
	res = PQexec(conn, "SELECT sum(pg_indexes_size(r.oid)) AS indexessize "
				 "FROM pg_class r JOIN pg_namespace n "
				 "ON relnamespace = n.oid WHERE n.nspname NOT IN "
				 "('pg_catalog', 'pgactive', 'information_schema') "
				 "AND relkind = 'r' AND relpersistence = 'p';");

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
		ereport(ERROR,
				(errmsg("unable to get total indexes size from remote node"),
				 errdetail("Querying remote failed with: %s", PQerrorMessage(conn))));

	Assert(PQnfields(res) == 1);
	Assert(PQntuples(res) == 1);

	if (PQgetisnull(res, 0, 0))
		ri->indexessize = 0;
	else
		ri->indexessize = DatumGetInt64(
										DirectFunctionCall1(int8in, CStringGetDatum(PQgetvalue(res, 0, 0))));

	PQclear(res);
}

static void
pgactive_test_remote_connectback_internal(PGconn *conn,
										  struct remote_node_info *ri,
										  const char *my_dsn)
{
	PGresult   *res;
	const char *mydsn_values[1];
	Oid			mydsn_types[1] = {TEXTOID};

	mydsn_values[0] = my_dsn;

	/*
	 * Ask the remote to connect back to us in replication mode, then discard
	 * the results.
	 */
	res = PQexecParams(conn, "SELECT * FROM "
					   "pgactive._pgactive_get_node_info_private($1);",
					   1, mydsn_types, mydsn_values, NULL, NULL, 0);

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		/* TODO clone remote error to local */
		ereport(ERROR,
				(errmsg("connection from remote back to local failed"),
				 errdetail("Remote reported: %s", PQerrorMessage(conn))));
	}

	Assert(PQnfields(res) == 18);

	if (PQntuples(res) != 1)
		elog(ERROR, "got %d tuples instead of expected 1", PQntuples(res));

	ri->sysid_str = pstrdup(PQgetvalue(res, 0, 0));
	if (sscanf(ri->sysid_str, UINT64_FORMAT, &ri->nodeid.sysid) != 1)
		elog(ERROR, "could not parse remote sysid %s", ri->sysid_str);

	ri->nodeid.timeline = DatumGetObjectId(
										   DirectFunctionCall1(oidin, CStringGetDatum(PQgetvalue(res, 0, 1))));
	ri->nodeid.dboid = DatumGetObjectId(
										DirectFunctionCall1(oidin, CStringGetDatum(PQgetvalue(res, 0, 2))));

	ri->variant = pstrdup(PQgetvalue(res, 0, 3));
	ri->version = pstrdup(PQgetvalue(res, 0, 4));
	ri->version_num = atoi(PQgetvalue(res, 0, 5));
	ri->min_remote_version_num = atoi(PQgetvalue(res, 0, 6));
	ri->has_required_privs = DatumGetBool(
										  DirectFunctionCall1(boolin, CStringGetDatum(PQgetvalue(res, 0, 7))));
	ri->node_status =
		PQgetisnull(res, 0, 8) ? '\0' : PQgetvalue(res, 0, 8)[0];
	ri->node_name = pstrdup(PQgetvalue(res, 0, 9));
	ri->dbname = pstrdup(PQgetvalue(res, 0, 10));
	ri->dbsize = DatumGetInt64(
							   DirectFunctionCall1(int8in, CStringGetDatum(PQgetvalue(res, 0, 11))));
	ri->indexessize = DatumGetInt64(
									DirectFunctionCall1(int8in, CStringGetDatum(PQgetvalue(res, 0, 12))));
	ri->max_nodes = DatumGetInt32(
								  DirectFunctionCall1(int4in, CStringGetDatum(PQgetvalue(res, 0, 13))));
	ri->skip_ddl_replication = DatumGetBool(
											DirectFunctionCall1(boolin, CStringGetDatum(PQgetvalue(res, 0, 14))));
	ri->cur_nodes = DatumGetInt32(
								  DirectFunctionCall1(int4in, CStringGetDatum(PQgetvalue(res, 0, 15))));
	ri->datcollate =
		PQgetisnull(res, 0, 16) ? NULL : pstrdup(PQgetvalue(res, 0, 16));
	ri->datctype =
		PQgetisnull(res, 0, 17) ? NULL : pstrdup(PQgetvalue(res, 0, 17));

	PQclear(res);
}

Datum
pgactive_get_node_info(PG_FUNCTION_ARGS)
{
	char	   *dsn;
	Datum		values[18];
	bool		isnull[18];
	TupleDesc	tupleDesc;
	HeapTuple	returnTuple;
	PGconn	   *conn;
	NameData	appname;
	pgactiveNodeId node;

	if (get_call_result_type(fcinfo, NULL, &tupleDesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	dsn = text_to_cstring(PG_GETARG_TEXT_P(0));

	if (PG_ARGISNULL(1))
	{
		/*
		 * Verify that we can make a replication connection to the node
		 * identified by dsn so that pg_hba.conf issues get caught early.
		 */
		snprintf(NameStr(appname), NAMEDATALEN,
				 "pgactiveverifyreplicationconnection");
		conn = pgactive_connect(dsn, &appname, &node);
		PQfinish(conn);

		/*
		 * Establish a non-replication connection to the the node identified
		 * by dsn to get the required info.
		 */
		conn = pgactive_connect_nonrepl(dsn, "pgactivelocalnodeinfo", true);
	}
	else
	{
		char	   *remote_dsn = text_to_cstring(PG_GETARG_TEXT_P(1));

		/*
		 * When remote_dsn is specified establish a non-replication connection
		 * to a remote node and use that connection to connect back to the
		 * local node in both replication and non-replication modes. This is
		 * used during setup to make sure the local node is useable. Then, it
		 * reports the same data as pgactive_get_node_info, but it's reported
		 * about the local node via the remote node.
		 */
		conn = pgactive_connect_nonrepl(remote_dsn, "pgactiveremotenodeinfo", true);
	}

	memset(values, 0, sizeof(values));
	memset(isnull, 0, sizeof(isnull));

	PG_ENSURE_ERROR_CLEANUP(pgactive_cleanup_conn_close,
							PointerGetDatum(&conn));
	{
		struct remote_node_info ri;

		memset(&ri, 0, sizeof(ri));

		if (PG_ARGISNULL(1))
			pgactive_get_remote_nodeinfo_internal(conn, &ri);
		else
			pgactive_test_remote_connectback_internal(conn, &ri, dsn);

		Assert(ri.sysid_str != NULL);
		values[0] = CStringGetTextDatum(ri.sysid_str);
		values[1] = ObjectIdGetDatum(ri.nodeid.timeline);
		values[2] = ObjectIdGetDatum(ri.nodeid.dboid);
		values[3] = CStringGetTextDatum(ri.variant);
		values[4] = CStringGetTextDatum(ri.version);
		values[5] = Int32GetDatum(ri.version_num);
		values[6] = Int32GetDatum(ri.min_remote_version_num);
		values[7] = BoolGetDatum(ri.has_required_privs);
		if (ri.node_status == '\0')
			isnull[8] = true;
		else
			values[8] = CharGetDatum(ri.node_status);

		values[9] = CStringGetTextDatum(ri.node_name);
		values[10] = CStringGetTextDatum(ri.dbname);
		values[11] = Int64GetDatum(ri.dbsize);
		values[12] = Int64GetDatum(ri.indexessize);
		values[13] = Int32GetDatum(ri.max_nodes);
		values[14] = BoolGetDatum(ri.skip_ddl_replication);
		values[15] = Int32GetDatum(ri.cur_nodes);

		if (ri.datcollate == NULL)
			isnull[16] = true;
		else
			values[16] = CStringGetTextDatum(ri.datcollate);

		if (ri.datctype == NULL)
			isnull[17] = true;
		else
			values[17] = CStringGetTextDatum(ri.datctype);

		returnTuple = heap_form_tuple(tupleDesc, values, isnull);

		free_remote_node_info(&ri);
	}
	PG_END_ENSURE_ERROR_CLEANUP(pgactive_cleanup_conn_close,
								PointerGetDatum(&conn));

	PQfinish(conn);

	PG_RETURN_DATUM(HeapTupleGetDatum(returnTuple));
}
