
/* -------------------------------------------------------------------------
 *
 * bdr_remotecalls.c
 *     Make libpq requests to a remote BDR instance
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		bdr_remotecalls.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "bdr.h"
#include "bdr_internal.h"

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

#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

#include "utils/builtins.h"
#include "utils/pg_lsn.h"

PGDLLEXPORT Datum bdr_get_remote_nodeinfo(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_test_replication_connection(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_test_remote_connectback(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_copytable_test(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_drop_remote_slot(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(bdr_get_remote_nodeinfo);
PG_FUNCTION_INFO_V1(bdr_test_replication_connection);
PG_FUNCTION_INFO_V1(bdr_test_remote_connectback);
PG_FUNCTION_INFO_V1(bdr_copytable_test);
PG_FUNCTION_INFO_V1(bdr_drop_remote_slot);

/*
 * Make standard postgres connection, ERROR on failure.
 */
PGconn *
bdr_connect_nonrepl(const char *connstring, const char *appnamesuffix)
{
	PGconn	   *nonrepl_conn;
	StringInfoData dsn;
	char	   *servername;

	servername = get_connect_string(connstring);

	initStringInfo(&dsn);
	appendStringInfo(&dsn, "%s %s %s application_name='%s:%s'",
					 bdr_default_apply_connection_options,
					 bdr_extra_apply_connection_options,
					 (servername == NULL ? connstring : servername),
					 bdr_get_my_cached_node_name(), appnamesuffix);

	/*
	 * Test to see if there's an entry in the remote's bdr.bdr_nodes for our
	 * system identifier. If there is, that'll tell us what stage of startup
	 * we are up to and let us resume an incomplete start.
	 */
	nonrepl_conn = PQconnectdb(dsn.data);
	if (PQstatus(nonrepl_conn) != CONNECTION_OK)
	{
		ereport(FATAL,
				(errmsg("could not connect to the server in non-replication mode: %s",
						PQerrorMessage(nonrepl_conn)),
				 errdetail("dsn was: %s", dsn.data)));
	}

	return nonrepl_conn;
}

/*
 * Close a connection if it exists. The connection passed
 * is a pointer to a *PGconn; if the target is NULL, it's
 * presumed not inited or already closed and is ignored.
 */
void
bdr_cleanup_conn_close(int code, Datum connptr)
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
 * It copies bdr_connections entries from the remote table to the
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
bdr_copytable(PGconn *copyfrom_conn, PGconn *copyto_conn,
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
 * Test function for bdr_copytable.
 */
Datum
bdr_copytable_test(PG_FUNCTION_ARGS)
{
	const char *fromdsn = PG_GETARG_CSTRING(0);
	const char *todsn = PG_GETARG_CSTRING(1);
	const char *fromquery = PG_GETARG_CSTRING(2);
	const char *toquery = PG_GETARG_CSTRING(3);

	PGconn	   *fromconn,
			   *toconn;

	fromconn = PQconnectdb(fromdsn);
	if (PQstatus(fromconn) != CONNECTION_OK)
		elog(ERROR, "from conn failed");

	toconn = PQconnectdb(todsn);
	if (PQstatus(toconn) != CONNECTION_OK)
		elog(ERROR, "to conn failed");

	bdr_copytable(fromconn, toconn, fromquery, toquery);

	PQfinish(fromconn);
	PQfinish(toconn);

	PG_RETURN_VOID();
}

/*
 * The implementation guts of bdr_get_remote_nodeinfo, callable with
 * a pre-existing connection.
 */
void
bdr_get_remote_nodeinfo_internal(PGconn *conn, struct remote_node_info *ri)
{
	PGresult   *res;
	int			i;
	char	   *remote_bdr_version_str;
	int			parsed_version_num;

	/* Make sure BDR is actually present and active on the remote */
	bdr_ensure_ext_installed(conn);

	/*
	 * Acquire remote node information. With this, we can also safely find out
	 * if we're superuser at this point.
	 */
	res = PQexec(conn, "SELECT bdr.bdr_version(), bdr.bdr_version_num(), "
				 "bdr.bdr_variant(), bdr.bdr_min_remote_version_num(), "
				 "current_setting('is_superuser') AS issuper, "
				 "bdr.bdr_get_local_node_name() AS node_name, "
				 "current_database()::text AS dbname, "
				 "pg_database_size(current_database()) AS dbsize, "
				 "current_setting('bdr.max_nodes') AS max_nodes, "
				 "current_setting('bdr.skip_ddl_replication') AS skip_ddl_replication, "
				 "count(1) FROM bdr.bdr_nodes WHERE node_status NOT IN (bdr.bdr_node_status_to_char('BDR_NODE_STATUS_KILLED'));");

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
		ereport(ERROR,
				(errmsg("unable to get BDR information from remote node"),
				 errdetail("Querying remote failed with: %s", PQerrorMessage(conn))));

	Assert(PQnfields(res) == 11);
	Assert(PQntuples(res) == 1);
	remote_bdr_version_str = PQgetvalue(res, 0, 0);
	ri->version = pstrdup(remote_bdr_version_str);
	ri->version_num = atoi(PQgetvalue(res, 0, 1));
	ri->variant = pstrdup(PQgetvalue(res, 0, 2));
	ri->min_remote_version_num = atoi(PQgetvalue(res, 0, 3));
	ri->is_superuser = DatumGetBool(
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
	 * Even though we should be able to get it from bdr_version_num, always
	 * parse the BDR version so that the parse code gets sanity checked, and
	 * so that we notice if the remote version is too old to have
	 * bdr_version_num.
	 */
	parsed_version_num = bdr_parse_version(ri->version, NULL, NULL,
										   NULL, NULL);

	if (ri->version_num != parsed_version_num)
		elog(WARNING, "parsed BDR version %d from string %s != returned BDR version %d",
			 parsed_version_num, remote_bdr_version_str, ri->version_num);

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
				 "FROM bdr.bdr_get_local_nodeid();");

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
	res = PQexec(conn, "SELECT node_status FROM bdr.bdr_nodes WHERE "
				 "(node_sysid, node_timeline, node_dboid) = bdr.bdr_get_local_nodeid();");

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
		ereport(ERROR,
				(errmsg("unable to get remote node status"),
				 errdetail("Querying remote failed with: %s", PQerrorMessage(conn))));

	Assert(PQnfields(res) == 1);

	if (PQntuples(res) == 0)
	{
		/* This happens when creating first node in BDR group */
		ri->node_status = '\0';
	}
	else if (PQntuples(res) == 1)
	{
		if (PQgetisnull(res, 0, 0))
			elog(ERROR, "unexpectedly null field node_status in bdr.bdr_nodes");

		ri->node_status = PQgetvalue(res, 0, 0)[0];
	}
	else
		elog(ERROR, "got more than one bdr.bdr_nodes row matching local nodeid");	/* shouldn't happen */

	PQclear(res);
}

Datum
bdr_get_remote_nodeinfo(PG_FUNCTION_ARGS)
{
	const char *remote_node_dsn = text_to_cstring(PG_GETARG_TEXT_P(0));
	Datum		values[17];
	bool		isnull[17];
	TupleDesc	tupleDesc;
	HeapTuple	returnTuple;
	PGconn	   *conn;

	if (get_call_result_type(fcinfo, NULL, &tupleDesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	conn = bdr_connect_nonrepl(remote_node_dsn, "bdrnodeinfo");

	memset(values, 0, sizeof(values));
	memset(isnull, 0, sizeof(isnull));

	PG_ENSURE_ERROR_CLEANUP(bdr_cleanup_conn_close,
							PointerGetDatum(&conn));
	{
		struct remote_node_info ri;

		memset(&ri, 0, sizeof(ri));
		bdr_get_remote_nodeinfo_internal(conn, &ri);

		if (ri.sysid_str != NULL)
		{
			values[0] = CStringGetTextDatum(ri.sysid_str);
			values[1] = ObjectIdGetDatum(ri.nodeid.timeline);
			values[2] = ObjectIdGetDatum(ri.nodeid.dboid);
		}
		else
		{
			/* Old peer version lacks sysid info */
			isnull[0] = true;
			isnull[1] = true;
			isnull[2] = true;
		}
		values[3] = CStringGetTextDatum(ri.variant);
		values[4] = CStringGetTextDatum(ri.version);
		values[5] = Int32GetDatum(ri.version_num);
		values[6] = Int32GetDatum(ri.min_remote_version_num);
		values[7] = BoolGetDatum(ri.is_superuser);
		if (ri.node_status == '\0')
			isnull[8] = true;
		else
			values[8] = CharGetDatum(ri.node_status);

		values[9] = CStringGetTextDatum(ri.node_name);
		values[10] = CStringGetTextDatum(ri.dbname);
		values[11] = Int64GetDatum(ri.dbsize);
		values[12] = Int32GetDatum(ri.max_nodes);
		values[13] = BoolGetDatum(ri.skip_ddl_replication);
		values[14] = Int32GetDatum(ri.cur_nodes);

		if (ri.datcollate == NULL)
			isnull[15] = true;
		else
			values[15] = CStringGetTextDatum(ri.datcollate);

		if (ri.datctype == NULL)
			isnull[16] = true;
		else
			values[16] = CStringGetTextDatum(ri.datctype);

		returnTuple = heap_form_tuple(tupleDesc, values, isnull);

		free_remote_node_info(&ri);
	}
	PG_END_ENSURE_ERROR_CLEANUP(bdr_cleanup_conn_close,
								PointerGetDatum(&conn));

	PQfinish(conn);

	PG_RETURN_DATUM(HeapTupleGetDatum(returnTuple));
}

/*
 * Test a given dsn as a replication connection, appending the replication
 * parameter, and return the node identity information from IDENTIFY SYSTEM.
 *
 * This can be used safely against the local_dsn, as it does not enforce
 * that the local node ID differ from the identity on the other end.
 */
Datum
bdr_test_replication_connection(PG_FUNCTION_ARGS)
{
	const char *conninfo = text_to_cstring(PG_GETARG_TEXT_P(0));
	char	   *servername;
	TupleDesc	tupleDesc;
	HeapTuple	returnTuple;
	PGconn	   *conn;
	NameData	appname;
	BDRNodeId	remote;
	Datum		values[3];
	bool		isnull[3] = {false, false, false};
	char		sysid_str[33];

	if (get_call_result_type(fcinfo, NULL, &tupleDesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	snprintf(NameStr(appname), NAMEDATALEN, "BDR test connection");
	servername = get_connect_string(conninfo);
	conn = bdr_connect((servername == NULL ? conninfo : servername), &appname, &remote);
	snprintf(sysid_str, sizeof(sysid_str), UINT64_FORMAT, remote.sysid);

	values[0] = CStringGetTextDatum(sysid_str);
	values[1] = ObjectIdGetDatum(remote.timeline);
	values[2] = ObjectIdGetDatum(remote.dboid);

	returnTuple = heap_form_tuple(tupleDesc, values, isnull);

	PQfinish(conn);

	PG_RETURN_DATUM(HeapTupleGetDatum(returnTuple));
}

void
bdr_test_remote_connectback_internal(PGconn *conn,
									 struct remote_node_info *ri, const char *my_dsn)
{
	PGresult   *res;
	const char *mydsn_values[1];
	Oid			mydsn_types[1] = {TEXTOID};

	mydsn_values[0] = my_dsn;

	/* Make sure BDR is actually present and active on the remote */
	bdr_ensure_ext_installed(conn);

	/*
	 * Ask the remote to connect back to us in replication mode, then discard
	 * the results.
	 */
	res = PQexecParams(conn, "SELECT sysid, timeline, dboid "
					   "FROM bdr.bdr_test_replication_connection($1)",
					   1, mydsn_types, mydsn_values, NULL, NULL, 0);

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		/* TODO clone remote error to local */
		ereport(ERROR,
				(errmsg("connection from remote back to local in replication mode failed"),
				 errdetail("Remote reported: %s", PQerrorMessage(conn))));
	}

	PQclear(res);

	/*
	 * Acquire bdr_get_remote_nodeinfo's results from running it on the remote
	 * node to connect back to us.
	 */
	res = PQexecParams(conn, "SELECT sysid, timeline, dboid, variant, version, "
					   "       version_num, min_remote_version_num, is_superuser "
					   "FROM bdr.bdr_get_remote_nodeinfo($1)",
					   1, mydsn_types, mydsn_values, NULL, NULL, 0);

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		/* TODO clone remote error to local */
		ereport(ERROR,
				(errmsg("connection from remote back to local failed"),
				 errdetail("Remote reported: %s", PQerrorMessage(conn))));
	}

	Assert(PQnfields(res) == 8);

	if (PQntuples(res) != 1)
		elog(ERROR, "got %d tuples instead of expected 1", PQntuples(res));

	ri->sysid_str = NULL;
	ri->nodeid.sysid = 0;
	ri->nodeid.timeline = 0;
	ri->nodeid.dboid = InvalidOid;
	ri->variant = NULL;
	ri->version = NULL;
	ri->version_num = 0;
	ri->min_remote_version_num = 0;
	ri->is_superuser = true;

	if (!PQgetisnull(res, 0, 0))
	{
		ri->sysid_str = pstrdup(PQgetvalue(res, 0, 0));

		if (sscanf(ri->sysid_str, UINT64_FORMAT, &ri->nodeid.sysid) != 1)
			elog(ERROR, "could not parse sysid %s", ri->sysid_str);
	}

	if (!PQgetisnull(res, 0, 1))
	{
		ri->nodeid.timeline = DatumGetObjectId(
											   DirectFunctionCall1(oidin, CStringGetDatum(PQgetvalue(res, 0, 1))));
	}

	if (!PQgetisnull(res, 0, 2))
	{
		ri->nodeid.dboid = DatumGetObjectId(
											DirectFunctionCall1(oidin, CStringGetDatum(PQgetvalue(res, 0, 2))));
	}

	if (PQgetisnull(res, 0, 3))
		elog(ERROR, "variant should never be null");
	ri->variant = pstrdup(PQgetvalue(res, 0, 3));

	if (!PQgetisnull(res, 0, 4))
		ri->version = pstrdup(PQgetvalue(res, 0, 4));

	if (!PQgetisnull(res, 0, 5))
		ri->version_num = atoi(PQgetvalue(res, 0, 5));

	if (!PQgetisnull(res, 0, 6))
		ri->min_remote_version_num = atoi(PQgetvalue(res, 0, 6));

	if (!PQgetisnull(res, 0, 7))
		ri->is_superuser = DatumGetBool(
										DirectFunctionCall1(boolin, CStringGetDatum(PQgetvalue(res, 0, 7))));

	PQclear(res);
}

/*
 * Establish a connection to a remote node and use that connection to connect
 * back to the local node in both replication and non-replication modes.
 *
 * This is used during setup to make sure the local node is useable.
 *
 * Reports the same data as bdr_get_remote_nodeinfo, but it's reported
 * about the local node via the remote node.
 */
Datum
bdr_test_remote_connectback(PG_FUNCTION_ARGS)
{
	const char *remote_node_dsn;
	const char *my_dsn;
	const char *remote_servername;
	const char *servername;
	Datum		values[8];
	bool		isnull[8] = {false, false, false, false, false, false, false, false};
	TupleDesc	tupleDesc;
	HeapTuple	returnTuple;
	PGconn	   *conn;

	if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
		elog(ERROR, "both arguments must be non-null");

	remote_node_dsn = text_to_cstring(PG_GETARG_TEXT_P(0));
	my_dsn = text_to_cstring(PG_GETARG_TEXT_P(1));

	if (get_call_result_type(fcinfo, NULL, &tupleDesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	remote_servername = get_connect_string(remote_node_dsn);
	conn = bdr_connect_nonrepl((remote_servername == NULL ? remote_node_dsn : remote_servername), "bdrconnectback");

	PG_ENSURE_ERROR_CLEANUP(bdr_cleanup_conn_close,
							PointerGetDatum(&conn));
	{
		struct remote_node_info ri;

		memset(&ri, 0, sizeof(ri));
		servername = get_connect_string(my_dsn);
		bdr_test_remote_connectback_internal(conn, &ri, (servername == NULL ? my_dsn : servername));

		if (ri.sysid_str != NULL)
			values[0] = CStringGetTextDatum(ri.sysid_str);
		else
			isnull[0] = true;

		values[1] = ObjectIdGetDatum(ri.nodeid.timeline);

		if (ri.nodeid.dboid != InvalidOid)
			values[2] = ObjectIdGetDatum(ri.nodeid.dboid);
		else
			isnull[2] = true;

		if (ri.variant != NULL)
			values[3] = CStringGetTextDatum(ri.variant);
		else
			isnull[3] = true;

		if (ri.version != NULL)
			values[4] = CStringGetTextDatum(ri.version);
		else
			isnull[4] = true;

		if (ri.version_num != 0)
			values[5] = Int32GetDatum(ri.version_num);
		else
			isnull[5] = true;

		if (ri.min_remote_version_num != 0)
			values[6] = Int32GetDatum(ri.min_remote_version_num);
		else
			isnull[6] = true;

		values[7] = BoolGetDatum(ri.is_superuser);

		returnTuple = heap_form_tuple(tupleDesc, values, isnull);

		free_remote_node_info(&ri);
	}
	PG_END_ENSURE_ERROR_CLEANUP(bdr_cleanup_conn_close,
								PointerGetDatum(&conn));

	PQfinish(conn);

	PG_RETURN_DATUM(HeapTupleGetDatum(returnTuple));
}


/*
 * Drops replication slot on remote node that has been used by the local node.
 */
Datum
bdr_drop_remote_slot(PG_FUNCTION_ARGS)
{
	const char *remote_sysid_str = text_to_cstring(PG_GETARG_TEXT_P(0));
	PGconn	   *conn;
	PGresult   *res;
	NameData	slotname;
	BdrConnectionConfig *cfg;
	BDRNodeId	remote;

	remote.timeline = PG_GETARG_OID(1);
	remote.dboid = PG_GETARG_OID(2);

	if (sscanf(remote_sysid_str, UINT64_FORMAT, &remote.sysid) != 1)
		elog(ERROR, "parsing of remote sysid as uint64 failed");

	cfg = bdr_get_connection_config(&remote, false);
	conn = bdr_connect_nonrepl(cfg->dsn, "bdr_drop_replication_slot");
	bdr_free_connection_config(cfg);

	PG_ENSURE_ERROR_CLEANUP(bdr_cleanup_conn_close,
							PointerGetDatum(&conn));
	{
		struct remote_node_info ri;
		const char *values[1];
		Oid			types[1] = {TEXTOID};
		BDRNodeId	myid;

		bdr_make_my_nodeid(&myid);
		memset(&ri, 0, sizeof(ri));

		/* Try connecting and build slot name from retrieved info */
		bdr_get_remote_nodeinfo_internal(conn, &ri);
		bdr_slot_name(&slotname, &myid, remote.dboid);
		free_remote_node_info(&ri);

		values[0] = NameStr(slotname);

		/* Check if the slot exists */
		res = PQexecParams(conn,
						   "SELECT plugin "
						   "FROM pg_catalog.pg_replication_slots "
						   "WHERE slot_name = $1",
						   1, types, values, NULL, NULL, 0);

		if (PQresultStatus(res) != PGRES_TUPLES_OK)
		{
			ereport(ERROR,
					(errmsg("getting remote slot info failed"),
					 errdetail("SELECT FROM pg_catalog.pg_replication_slots failed with: %s",
							   PQerrorMessage(conn))));
		}

		/* Slot not found return false */
		if (PQntuples(res) == 0)
		{
			PQfinish(conn);
			PG_RETURN_BOOL(false);
		}

		/* Slot found, validate that it's BDR slot */
		if (PQgetisnull(res, 0, 0))
			elog(ERROR, "unexpectedly null field %s", PQfname(res, 0));

		if (strcmp("bdr", PQgetvalue(res, 0, 0)) != 0)
			ereport(ERROR,
					(errmsg("slot %s is not BDR slot", NameStr(slotname))));

		res = PQexecParams(conn, "SELECT pg_drop_replication_slot($1)",
						   1, types, values, NULL, NULL, 0);

		/* And finally, drop the slot. */
		if (PQresultStatus(res) != PGRES_TUPLES_OK)
		{
			ereport(ERROR,
					(errmsg("remote slot drop failed"),
					 errdetail("SELECT pg_drop_replication_slot() failed with: %s",
							   PQerrorMessage(conn))));
		}
	}
	PG_END_ENSURE_ERROR_CLEANUP(bdr_cleanup_conn_close,
								PointerGetDatum(&conn));

	PQfinish(conn);

	PG_RETURN_BOOL(true);
}
