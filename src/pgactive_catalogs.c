/* -------------------------------------------------------------------------
 *
 * pgactive_catalogs.c
 *		Access to pgactive catalog information like pgactive.pgactive_nodes
 *
 * Functions usable by both the output plugin and the extension/workers for
 * accessing and manipulating pgactive's catalogs, like pgactive.pgactive_nodes.
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive.c
 *
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include "pgactive.h"
#include "miscadmin.h"

#include "access/xact.h"

#include "catalog/pg_type.h"

#include "commands/dbcommands.h"

#include "executor/spi.h"

#include "nodes/makefuncs.h"

#include "replication/origin.h"

#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "catalog/pg_namespace.h"

static int	getattno(const char *colname);
static char *pgactive_textarr_to_identliststr(ArrayType *textarray);

Datum		pgactive_node_status_to_char(PG_FUNCTION_ARGS);
Datum		pgactive_node_status_from_char(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pgactive_node_status_to_char);
PG_FUNCTION_INFO_V1(pgactive_node_status_from_char);

/*
 * Get the pgactive.pgactive_nodes status value for the specified node from the local
 * pgactive.pgactive_nodes table via SPI.
 *
 * Returns the status value, or '\0' if no such row exists or pgactive schema
 * doesn't exist (extension may have been dropped).
 *
 * SPI must be initialized, and you must be in a running transaction.
 */
pgactiveNodeStatus
pgactive_nodes_get_local_status(const pgactiveNodeId * const node, bool missing_ok)
{
	int			spi_ret;
	Oid			argtypes[] = {TEXTOID, OIDOID, OIDOID};
	Datum		values[3];
	bool		isnull;
	char		status;
	char		sysid_str[33];
	Oid			schema_oid;

	Assert(IsTransactionState());

	snprintf(sysid_str, sizeof(sysid_str), UINT64_FORMAT, node->sysid);

	/*
	 * Determine if pgactive is present on this DB. The output plugin can be
	 * started on a db that doesn't actually have pgactive active, but we
	 * don't want to allow that.
	 *
	 * Check for a pgactive schema.
	 */
	schema_oid = pgactiveGetSysCacheOid1(NAMESPACENAME, Anum_pg_namespace_oid, CStringGetDatum("pgactive"));
	if (schema_oid == InvalidOid)
	{
		if (missing_ok)
			return '\0';
		else
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("no pgactive schema is present in database %s, cannot create a pgactive slot",
							get_database_name(MyDatabaseId)),
					 errhint("There is no pgactive.connections entry for this database on the target node or pgactive is not in shared_preload_libraries.")));
	}

	values[0] = CStringGetTextDatum(sysid_str);
	values[1] = ObjectIdGetDatum(node->timeline);
	values[2] = ObjectIdGetDatum(node->dboid);

	spi_ret = SPI_execute_with_args(
									"SELECT node_status FROM pgactive.pgactive_nodes "
									"WHERE node_sysid = $1 AND node_timeline = $2 AND node_dboid = $3",
									3, argtypes, values, NULL, false, 1);

	if (spi_ret != SPI_OK_SELECT)
		elog(ERROR, "unable to query pgactive.pgactive_nodes, SPI error %d", spi_ret);

	if (SPI_processed == 0)
		return '\0';

	status = DatumGetChar(SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1,
										&isnull));

	if (isnull)
		elog(ERROR, "node_status in pgactive.pgactive_nodes table cannot be null");

	return (pgactiveNodeStatus) status;
}

/*
 * Get the pgactive.pgactive_nodes record for the specififed node from the local
 * pgactive.pgactive_nodes table via SPI.
 *
 * Returns the status value, or NULL if no such row exists.
 *
 * SPI must be initialized, and you must be in a running transaction.
 */
pgactiveNodeInfo *
pgactive_nodes_get_local_info(const pgactiveNodeId * const node)
{
	pgactiveNodeInfo *nodeinfo = NULL;
	char		sysid_str[33];
	HeapTuple	tuple = NULL;
	Relation	rel;
	RangeVar   *rv;
	SysScanDesc scan;
	ScanKeyData key[3];

	snprintf(sysid_str, sizeof(sysid_str), UINT64_FORMAT, node->sysid);

	rv = makeRangeVar("pgactive", "pgactive_nodes", -1);
	rel = table_openrv(rv, RowExclusiveLock);

	ScanKeyInit(&key[0],
				1,
				BTEqualStrategyNumber, F_TEXTEQ,
				CStringGetTextDatum(sysid_str));
	ScanKeyInit(&key[1],
				2,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(node->timeline));
	ScanKeyInit(&key[2],
				3,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(node->dboid));

	scan = systable_beginscan(rel, 0, true, NULL, 3, key);

	tuple = systable_getnext(scan);

	if (HeapTupleIsValid(tuple))
	{
		bool		isnull;
		TupleDesc	desc = RelationGetDescr(rel);
		Datum		tmp;

		nodeinfo = palloc0(sizeof(pgactiveNodeInfo));
		pgactive_nodeid_cpy(&nodeinfo->id, node);
		nodeinfo->status = (pgactiveNodeStatus) DatumGetChar(fastgetattr(tuple, 4, desc, &isnull));
		if (isnull)
			elog(ERROR, "node_status in pgactive.pgactive_nodes table cannot be null");

		tmp = fastgetattr(tuple, 5, desc, &isnull);
		if (isnull)
			nodeinfo->name = NULL;
		else
			nodeinfo->name = pstrdup(TextDatumGetCString(tmp));

		tmp = fastgetattr(tuple, 6, desc, &isnull);
		if (!isnull)
			nodeinfo->local_dsn = pstrdup(TextDatumGetCString(tmp));

		tmp = fastgetattr(tuple, 7, desc, &isnull);
		if (!isnull)
			nodeinfo->init_from_dsn = pstrdup(TextDatumGetCString(tmp));

		nodeinfo->read_only = DatumGetBool(fastgetattr(tuple, 8, desc, &isnull));
		/* Readonly will be null on upgrade from an older pgactive */
		if (isnull)
			nodeinfo->read_only = false;

		nodeinfo->seq_id = DatumGetInt16(fastgetattr(tuple, 9, desc, &isnull));
		/* seq_id will be null on upgrade from an older pgactive */
		if (isnull)
			nodeinfo->seq_id = -1;

		nodeinfo->valid = true;
	}

	systable_endscan(scan);
	table_close(rel, RowExclusiveLock);

	return nodeinfo;
}

/* Free the pgactiveNodeInfo pointer including its properties. */
void
pgactive_pgactive_node_free(pgactiveNodeInfo * node)
{
	if (node == NULL)
		return;

	if (node->local_dsn)
		pfree(node->local_dsn);
	if (node->init_from_dsn)
		pfree(node->init_from_dsn);
	pfree(node);
}

void
pgactive_nodes_set_local_status(pgactiveNodeStatus status, pgactiveNodeStatus fromstatus)
{
	pgactive_nodes_set_local_attrs(status, fromstatus, NULL);
}

/*
 * Update mutable fields on the local pgactive.pgactive_nodes entry as identified by
 * current sysid,tlid,dboid. The node record must already exist and have the
 * specified old status.
 *
 * TODO: should do this with catalog access routines, not SPI.
 *
 * If seq_id is passed as non-null a sequence ID is assigned. node_seq_id
 * cannot be set back to null from this interface.
 *
 * Unlike pgactive_nodes_get_local_status, this inteface does not accept
 * sysid, tlid and dboid input but can only set the status of the local node.
 */
void
pgactive_nodes_set_local_attrs(pgactiveNodeStatus status, pgactiveNodeStatus oldstatus, const int *seq_id)
{
	int			spi_ret;
	Oid			argtypes[] = {CHAROID, TEXTOID, OIDOID, OIDOID, CHAROID, INT4OID};
	char		nulls[] = {' ', ' ', ' ', ' ', ' ', ' '};
	Datum		values[6];
	char		sysid_str[33];
	bool		tx_started = false;
	pgactiveNodeId myid;

	pgactive_make_my_nodeid(&myid);

	Assert(status != pgactive_NODE_STATUS_NONE);	/* Cannot pass \0 */
	/* Cannot have replication apply state set in this tx */
	Assert(replorigin_session_origin == InvalidRepOriginId);

	if (!IsTransactionState())
	{
		tx_started = true;
		StartTransactionCommand();
	}
	SPI_connect();
	PushActiveSnapshot(GetTransactionSnapshot());

	snprintf(sysid_str, sizeof(sysid_str), UINT64_FORMAT, myid.sysid);

	values[0] = CharGetDatum((char) status);
	values[1] = CStringGetTextDatum(sysid_str);
	values[2] = ObjectIdGetDatum(myid.timeline);
	values[3] = ObjectIdGetDatum(myid.dboid);
	values[4] = CharGetDatum((char) oldstatus);
	if (seq_id != NULL)
		values[5] = Int32GetDatum(*seq_id);
	else
		nulls[5] = 'n';

	spi_ret = SPI_execute_with_args(
									"UPDATE pgactive.pgactive_nodes"
									"   SET node_status = $1,"
									"       node_seq_id = coalesce($6, node_seq_id)"
									" WHERE node_sysid = $2"
									"   AND node_timeline = $3"
									"   AND node_dboid = $4"
									"   AND node_status = $5;",
									6, argtypes, values, nulls, false, 0);

	if (spi_ret != SPI_OK_UPDATE)
		elog(ERROR, "unable to set status=%c of row (node_sysid="
			 UINT64_FORMAT ", node_timeline=%u, node_dboid=%u) "
			 "in pgactive.pgactive_nodes: SPI error %d",
			 status, myid.sysid, myid.timeline, myid.dboid, spi_ret);

	SPI_finish();
	PopActiveSnapshot();
	if (tx_started)
		CommitTransactionCommand();
}

/*
 * Given a node's local RepOriginId, get its globally unique identifier (sysid,
 * timeline id, database oid). Ignore identifiers local to databases other than
 * the active DB.
 */
bool
pgactive_fetch_sysid_via_node_id_ifexists(RepOriginId node_id, pgactiveNodeId * node, bool missing_ok)
{
	if (node_id == InvalidRepOriginId || node_id == DoNotReplicateId)
	{
		/* It's the local node */
		pgactive_make_my_nodeid(node);
	}
	else
	{
		char	   *riname;

		Oid			local_dboid;

		replorigin_by_oid(node_id, missing_ok, &riname);
		if (riname == NULL)
			return false;

		pgactive_parse_replident_name(riname, node, &local_dboid);
		pfree(riname);

		if (local_dboid != MyDatabaseId)
		{
			ereport(ERROR,
					(errmsg("lookup failed for replication identifier %u", node_id),
					 errdetail("Replication identifier %u exists but is owned by another pgactive node in the same PostgreSQL instance, with dboid %u. Current node oid is %u.",
							   node_id, local_dboid, MyDatabaseId)));
		}
	}
	return true;
}

void
pgactive_fetch_sysid_via_node_id(RepOriginId node_id, pgactiveNodeId * node)
{
	(void) pgactive_fetch_sysid_via_node_id_ifexists(node_id, node, false);
}

/*
 * Get node identifiers from a replication identifier (replident, riident) name
 *
 * This isn't in pgactive_common.c because it uses elog().
 */
void
pgactive_parse_replident_name(const char *riname, pgactiveNodeId * node, Oid *local_dboid)
{
	NameData	replication_name;

	if (sscanf(riname, pgactive_REPORIGIN_ID_FORMAT,
			   &node->sysid, &node->timeline, &node->dboid, local_dboid,
			   NameStr(replication_name)) != 4)
	{
		/* Note: the test above excludes non-empty replication names */
		elog(ERROR, "could not parse slot name: %s", riname);
	}
}

/*
 * Get node identifiers from a slot name
 *
 * This isn't in pgactive_common.c because it uses elog().
 */
void
pgactive_parse_slot_name(const char *sname, pgactiveNodeId * remote, Oid *local_dboid)
{
	NameData	replication_name;

	if (sscanf(sname, pgactive_SLOT_NAME_FORMAT,
			   local_dboid, &remote->sysid, &remote->timeline, &remote->dboid,
			   NameStr(replication_name)) != 4)
	{
		/* Note: the test above excludes non-empty replication names */
		elog(ERROR, "could not parse slot name: %s", sname);
	}
}

/*
 * Format a replication origin / replication identifier (riident, replident)
 * name from a (sysid,timeline,dboid tuple).
 *
 * This isn't in pgactive_common.c because it uses StringInfo.
 */
char *
pgactive_replident_name(const pgactiveNodeId * const remote, Oid local_dboid)
{
	StringInfoData si;

	initStringInfo(&si);

	appendStringInfo(&si, pgactive_REPORIGIN_ID_FORMAT,
					 remote->sysid, remote->timeline, remote->dboid, local_dboid,
					 EMPTY_REPLICATION_NAME);

	/* stringinfo's data is palloc'd, can be returned directly */
	return si.data;
}

RepOriginId
pgactive_fetch_node_id_via_sysid(const pgactiveNodeId * const node)
{
	char	   *ident;
	RepOriginId id;

	ident = pgactive_replident_name(node, MyDatabaseId);
	id = replorigin_by_name(ident, false);
	pfree(ident);

	return id;
}

/*
 * Read connection configuration data from the DB and return zero or more
 * matching palloc'd pgactiveConnectionConfig results in a list.
 *
 * A transaction must be open.
 *
 * The list and values are allocated in the calling memory context. By default
 * this is the transaction memory context, but you can switch to contexts
 * before calling.
 *
 * Each pgactiveConnectionConfig's char* fields are palloc'd values.
 *
 * Uses the SPI, so push/pop caller's SPI state if needed.
 *
 * May raise exceptions from queries, SPI errors, etc.
 *
 * If both an entry with conn_origin for this node and one with null
 * conn_origin are found, only the one specific to this node is returned,
 * as it takes precedence over any generic configuration entry.
 *
 * Connections for nodes with state 'k'illed are not returned.
 * Connections in other states are, since we should fail (and retry)
 * until they're ready to accept slot creation. Connections with
 * no corresponding pgactive.pgactive_nodes row also get ignored.
 */
List *
pgactive_read_connection_configs()
{
	HeapTuple	tuple;
	StringInfoData query;
	int			i;
	int			ret;
	List	   *configs = NIL;
	MemoryContext caller_ctx,
				saved_ctx;
	char		sysid_str[33];
	Datum		values[3];
	Oid			types[3] = {TEXTOID, OIDOID, OIDOID};
	pgactiveNodeId myid;

	pgactive_make_my_nodeid(&myid);

	Assert(IsTransactionState());

	/* Save the calling memory context, which we'll allocate results in */
	caller_ctx = MemoryContextSwitchTo(CurTransactionContext);

	initStringInfo(&query);

	/* Configurations for all nodes, including the local node, are read. */
	appendStringInfo(&query, "SELECT DISTINCT ON (conn_sysid, conn_timeline, conn_dboid) "
					 "  conn_sysid, conn_timeline, conn_dboid, "
					 "  conn_dsn, conn_apply_delay, "
					 "  conn_replication_sets, node_name "
					 "FROM pgactive.pgactive_connections "
					 "INNER JOIN pgactive.pgactive_nodes "
					 "  ON (conn_sysid = node_sysid AND "
					 "      conn_timeline = node_timeline AND "
					 "      conn_dboid = node_dboid) "
					 "WHERE node_status <> " pgactive_NODE_STATUS_KILLED_S " "
					 "ORDER BY conn_sysid, conn_timeline, conn_dboid, "
					 "         conn_timeline ASC NULLS LAST, "
					 "         conn_dboid ASC NULLS LAST "
		);

	snprintf(sysid_str, sizeof(sysid_str), UINT64_FORMAT, myid.sysid);

	values[0] = CStringGetTextDatum(&sysid_str[0]);
	values[1] = ObjectIdGetDatum(myid.timeline);
	values[2] = ObjectIdGetDatum(myid.dboid);

	SPI_connect();
	PushActiveSnapshot(GetTransactionSnapshot());

	ret = SPI_execute_with_args(query.data, 3, types, values, NULL, false, 0);

	if (ret != SPI_OK_SELECT)
		elog(ERROR, "SPI error while querying pgactive.pgactive_connections");

	/* Switch to calling memory context to copy results */
	saved_ctx = MemoryContextSwitchTo(caller_ctx);

	for (i = 0; i < SPI_processed; i++)
	{
		Datum		tmp_datum;
		bool		isnull;
		ArrayType  *conn_replication_sets;
		char	   *tmp_sysid;

		pgactiveConnectionConfig *cfg = palloc(sizeof(pgactiveConnectionConfig));

		tuple = SPI_tuptable->vals[i];

		/*
		 * Fetch tuple attributes
		 *
		 * Note: SPI_getvalue calls the output function for the type, so the
		 * string is allocated in our memory context and doesn't need copying.
		 */
		tmp_sysid = SPI_getvalue(tuple, SPI_tuptable->tupdesc,
								 getattno("conn_sysid"));

		if (sscanf(tmp_sysid, UINT64_FORMAT, &cfg->remote_node.sysid) != 1)
			elog(ERROR, "parsing sysid uint64 from %s failed", tmp_sysid);

		tmp_datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc,
								  getattno("conn_timeline"),
								  &isnull);
		Assert(!isnull);
		cfg->remote_node.timeline = DatumGetObjectId(tmp_datum);

		tmp_datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc,
								  getattno("conn_dboid"),
								  &isnull);
		Assert(!isnull);
		cfg->remote_node.dboid = DatumGetObjectId(tmp_datum);

		cfg->origin_is_my_id = false;


		cfg->dsn = SPI_getvalue(tuple,
								SPI_tuptable->tupdesc,
								getattno("conn_dsn"));

		tmp_datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc,
								  getattno("conn_apply_delay"), &isnull);
		if (isnull)
			cfg->apply_delay = -1;
		else
			cfg->apply_delay = DatumGetInt32(tmp_datum);

		/*
		 * Replication sets are stored in the catalogs as a text[] of
		 * identifiers, so we'll want to unpack that.
		 */

		tmp_datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc,
								  getattno("conn_replication_sets"),
								  &isnull);

		conn_replication_sets = DatumGetArrayTypeP(tmp_datum);

		if (isnull)
			cfg->replication_sets = NULL;
		else
		{
			cfg->replication_sets =
				pgactive_textarr_to_identliststr(conn_replication_sets);
		}

		tmp_datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc,
								  getattno("node_name"), &isnull);
		if (isnull)
			cfg->node_name = NULL;
		else
		{
			cfg->node_name = text_to_cstring(DatumGetTextP(tmp_datum));
		}

		configs = lcons(cfg, configs);

	}

	MemoryContextSwitchTo(saved_ctx);

	SPI_finish();
	PopActiveSnapshot();

	MemoryContextSwitchTo(caller_ctx);

	return configs;
}

/*
 * Get the list of node_dsn and node_name from pgactive_nodes table of all
 * nodes but excluding local node or only local node.
 */
List *
pgactive_get_node_dsns(bool only_local_node)
{
	HeapTuple	tuple;
	StringInfoData query;
	int			i;
	int			ret;
	List	   *node_dsns = NIL;
	MemoryContext caller_ctx,
				saved_ctx;
	char		sysid_str[33];
	Datum		values[3];
	Oid			types[3] = {TEXTOID, OIDOID, OIDOID};
	pgactiveNodeId myid;

	Assert(IsTransactionState());

	pgactive_make_my_nodeid(&myid);

	/* Save the calling memory context, which we'll allocate results in */
	caller_ctx = MemoryContextSwitchTo(CurTransactionContext);

	initStringInfo(&query);

	if (only_local_node)
		appendStringInfo(&query, "SELECT node_dsn, node_name "
						 "FROM pgactive.pgactive_nodes "
						 "WHERE (node_sysid, node_timeline, node_dboid) = ($1, $2, $3) "
						 "AND node_status <> " pgactive_NODE_STATUS_KILLED_S " ");
	else
		appendStringInfo(&query, "SELECT node_dsn, node_name "
						 "FROM pgactive.pgactive_nodes "
						 "WHERE (node_sysid, node_timeline, node_dboid) <> ($1, $2, $3) "
						 "AND node_status <> " pgactive_NODE_STATUS_KILLED_S " ");

	snprintf(sysid_str, sizeof(sysid_str), UINT64_FORMAT, myid.sysid);

	values[0] = CStringGetTextDatum(&sysid_str[0]);
	values[1] = ObjectIdGetDatum(myid.timeline);
	values[2] = ObjectIdGetDatum(myid.dboid);

	SPI_connect();
	PushActiveSnapshot(GetTransactionSnapshot());

	ret = SPI_execute_with_args(query.data, 3, types, values, NULL, false, 0);

	if (ret != SPI_OK_SELECT)
		elog(ERROR, "SPI error while querying pgactive.pgactive_nodes");

	/* Switch to calling memory context to copy results */
	saved_ctx = MemoryContextSwitchTo(caller_ctx);

	for (i = 0; i < SPI_processed; i++)
	{
		pgactiveNodeDSNsInfo *info;

		info = palloc(sizeof(pgactiveNodeDSNsInfo));
		tuple = SPI_tuptable->vals[i];
		info->node_dsn = SPI_getvalue(tuple, SPI_tuptable->tupdesc,
									  getattno("node_dsn"));
		info->node_name = SPI_getvalue(tuple, SPI_tuptable->tupdesc,
									   getattno("node_name"));
		node_dsns = lcons(info, node_dsns);
	}

	MemoryContextSwitchTo(saved_ctx);
	SPI_finish();
	PopActiveSnapshot();
	MemoryContextSwitchTo(caller_ctx);

	return node_dsns;
}

void
pgactive_free_connection_config(pgactiveConnectionConfig * cfg)
{
	if (cfg->dsn != NULL)
		pfree(cfg->dsn);
	if (cfg->replication_sets != NULL)
		pfree(cfg->replication_sets);
}

/*
 * Fetch the connection configuration for the specified node
 */
pgactiveConnectionConfig *
pgactive_get_connection_config(const pgactiveNodeId * const node, bool missing_ok)
{
	List	   *configs;
	ListCell   *lc;
	MemoryContext saved_ctx;
	pgactiveConnectionConfig *found_config = NULL;
	bool		tx_started = false;

	Assert(MyDatabaseId != InvalidOid);

	if (!IsTransactionState())
	{
		tx_started = true;
		StartTransactionCommand();
	}

	saved_ctx = MemoryContextSwitchTo(TopMemoryContext);
	configs = pgactive_read_connection_configs();
	MemoryContextSwitchTo(saved_ctx);

	/*
	 * TODO DYNCONF Instead of reading all configs and then discarding all but
	 * the interesting one, we should really be doing a different query that
	 * returns only the configuration of interest. As this runs only during
	 * apply worker startup the impact is negligible.
	 */
	foreach(lc, configs)
	{
		pgactiveConnectionConfig *cfg = (pgactiveConnectionConfig *) lfirst(lc);

		if (pgactive_nodeid_eq(&cfg->remote_node, node))
		{
			found_config = cfg;
			break;
		}
		else
		{
			pgactive_free_connection_config(cfg);
		}
	}

	if (found_config == NULL && !missing_ok)
		elog(ERROR, "failed to find expected pgactive.connections row "
			 "(conn_sysid,conn_timeline,conn_dboid) = "
			 "(" UINT64_FORMAT ",%u,%u) "
			 "in pgactive.pgactive_connections",
			 node->sysid, node->timeline, node->dboid);

	if (tx_started)
		CommitTransactionCommand();

	list_free(configs);

	return found_config;
}

pgactiveConnectionConfig *
pgactive_get_my_connection_config(bool missing_ok)
{
	pgactiveNodeId ni;

	pgactive_make_my_nodeid(&ni);

	return pgactive_get_connection_config(&ni, missing_ok);
}


static int
getattno(const char *colname)
{
	int			attno;

	attno = SPI_fnumber(SPI_tuptable->tupdesc, colname);
	if (attno == SPI_ERROR_NOATTRIBUTE)
		elog(ERROR, "SPI error while reading %s from pgactive.pgactive_connections", colname);

	return attno;
}

/*
 * Given a text[] Datum guaranteed to contain no nulls, return an
 * identifier-quoted comma-separated string allocated in the current memory
 * context.
 */
static char *
pgactive_textarr_to_identliststr(ArrayType *textarray)
{
	Datum	   *elems;
	int			nelems,
				i;
	StringInfoData si;

	deconstruct_array(textarray,
					  TEXTOID, -1, false, 'i',
					  &elems, NULL, &nelems);

	if (nelems == 0)
		return pstrdup("");

	initStringInfo(&si);

	appendStringInfoString(&si,
						   quote_identifier(TextDatumGetCString(elems[0])));
	for (i = 1; i < nelems; i++)
	{
		appendStringInfoString(&si, ",");
		appendStringInfoString(&si,
							   quote_identifier(TextDatumGetCString(elems[i])));
	}

	/*
	 * The stringinfo is on the stack, but its data element is palloc'd in the
	 * caller's context and can be returned safely.
	 */
	return si.data;

}

/*
 * Helper to format node identity info into buffers, which must already be
 * allocated and big enough to hold a unit64 + terminator (33 bytes).
 */
void
stringify_node_identity(char *sysid_str, Size sysid_str_size,
						char *timeline_str, Size timeline_str_size,
						char *dboid_str, Size dboid_str_size,
						const pgactiveNodeId * const nodeid)
{
	snprintf(sysid_str, sysid_str_size, UINT64_FORMAT, nodeid->sysid);
	snprintf(timeline_str, timeline_str_size, "%u", nodeid->timeline);
	snprintf(dboid_str, dboid_str_size, "%u", nodeid->dboid);
}

void
stringify_my_node_identity(char *sysid_str, Size sysid_str_size,
						   char *timeline_str, Size timeline_str_size,
						   char *dboid_str, Size dboid_str_size)
{
	pgactiveNodeId myid;

	pgactive_make_my_nodeid(&myid);
	return stringify_node_identity(sysid_str, sysid_str_size, timeline_str,
								   timeline_str_size, dboid_str, dboid_str_size,
								   &myid);
}

Datum
pgactive_node_status_from_char(PG_FUNCTION_ARGS)
{
	pgactiveNodeStatus status = (pgactiveNodeStatus) PG_GETARG_CHAR(0);
	const char *result = NULL;

#define pgactive_NODE_STATUS_TOSTR(teststatus) \
	case teststatus: \
		result = #teststatus; \
		break;

	switch (status)
	{
			pgactive_NODE_STATUS_TOSTR(pgactive_NODE_STATUS_NONE)
				pgactive_NODE_STATUS_TOSTR(pgactive_NODE_STATUS_BEGINNING_INIT)
				pgactive_NODE_STATUS_TOSTR(pgactive_NODE_STATUS_COPYING_INITIAL_DATA)
				pgactive_NODE_STATUS_TOSTR(pgactive_NODE_STATUS_CATCHUP)
				pgactive_NODE_STATUS_TOSTR(pgactive_NODE_STATUS_CREATING_OUTBOUND_SLOTS)
				pgactive_NODE_STATUS_TOSTR(pgactive_NODE_STATUS_READY)
				pgactive_NODE_STATUS_TOSTR(pgactive_NODE_STATUS_KILLED)
	};

	if (result == NULL)
		elog(ERROR, "unrcognised status char %c", PG_GETARG_CHAR(0));

	PG_RETURN_TEXT_P(cstring_to_text(pstrdup(result)));
}

Datum
pgactive_node_status_to_char(PG_FUNCTION_ARGS)
{
	char	   *status = text_to_cstring(PG_GETARG_TEXT_P(0));
	pgactiveNodeStatus result;

#define pgactive_NODE_STATUS_FROMSTR(teststatus) \
	if (strcmp(status, #teststatus) == 0) \
	{ \
		result = teststatus; \
		continue; \
	}

	do
	{
		pgactive_NODE_STATUS_FROMSTR(pgactive_NODE_STATUS_NONE)
			pgactive_NODE_STATUS_FROMSTR(pgactive_NODE_STATUS_BEGINNING_INIT)
			pgactive_NODE_STATUS_FROMSTR(pgactive_NODE_STATUS_COPYING_INITIAL_DATA)
			pgactive_NODE_STATUS_FROMSTR(pgactive_NODE_STATUS_CATCHUP)
			pgactive_NODE_STATUS_FROMSTR(pgactive_NODE_STATUS_CREATING_OUTBOUND_SLOTS)
			pgactive_NODE_STATUS_FROMSTR(pgactive_NODE_STATUS_READY)
			pgactive_NODE_STATUS_FROMSTR(pgactive_NODE_STATUS_KILLED)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("string '%s' isn't recognised as a pgactive status", status)));
	} while (false);

	PG_RETURN_CHAR((char) result);
}

bool
pgactive_nodeid_eq(const pgactiveNodeId * const left, const pgactiveNodeId * const right)
{
	if (left == right)
		return true;

	if ((left == NULL) != (right == NULL))
		return false;

	return left->sysid == right->sysid
		&& left->timeline == right->timeline
		&& left->dboid == right->dboid;
}

void
pgactive_nodeid_cpy(pgactiveNodeId * const dest, const pgactiveNodeId * const src)
{
	Assert(dest != NULL && src != NULL);
	dest->sysid = src->sysid;
	dest->timeline = src->timeline;
	dest->dboid = src->dboid;
}

void
pgactive_make_my_nodeid(pgactiveNodeId * const ni)
{
	Assert(ni != NULL);
	ni->sysid = pgactive_get_nid_internal();
	ni->timeline = pgactiveThisTimeLineID;
	ni->dboid = MyDatabaseId;

	/*
	 * We use zero sysid as a special value in conflict reporting etc so we'd
	 * better not have it for a nodeid.
	 */
	Assert(ni->sysid != 0);

	Assert(ni->timeline == 0);

	/* Current database must be known */
	Assert(ni->dboid != InvalidOid);
}

/*
 * Get the remote node's node_seq_id from pgactive.pgactive_nodes table via SPI.
 */
int
pgactive_remote_node_seq_id(void)
{
	int			spi_ret;
	Oid			argtypes[] = {TEXTOID, OIDOID, OIDOID};
	Datum		values[3];
	bool		isnull;
	char		sysid_str[33];
	Oid			schema_oid;
	pgactiveNodeId *node;
	bool		tx_started = false;
	int			node_seq_id;

	Assert(IspgactiveApplyWorker());

	node = &(GetpgactiveApplyWorkerShmemPtr()->remote_node);

	if (!IsTransactionState())
	{
		tx_started = true;
		StartTransactionCommand();
	}

	SPI_connect();
	PushActiveSnapshot(GetTransactionSnapshot());

	snprintf(sysid_str, sizeof(sysid_str), UINT64_FORMAT, node->sysid);

	/*
	 * Determine if pgactive is present on this DB. The output plugin can be
	 * started on a db that doesn't actually have pgactive active, but we
	 * don't want to allow that.
	 *
	 * Check for a pgactive schema.
	 */
	schema_oid = pgactiveGetSysCacheOid1(NAMESPACENAME, Anum_pg_namespace_oid,
										 CStringGetDatum("pgactive"));
	if (schema_oid == InvalidOid)
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("no pgactive schema is present in database %s, cannot create a pgactive slot",
						get_database_name(MyDatabaseId)),
				 errhint("There is no pgactive.connections entry for this database on the target node or pgactive is not in shared_preload_libraries.")));
	}

	values[0] = CStringGetTextDatum(sysid_str);
	values[1] = ObjectIdGetDatum(node->timeline);
	values[2] = ObjectIdGetDatum(node->dboid);

	spi_ret = SPI_execute_with_args(
									"SELECT node_seq_id FROM pgactive.pgactive_nodes "
									"WHERE node_sysid = $1 AND node_timeline = $2 AND node_dboid = $3",
									3, argtypes, values, NULL, false, 1);

	if (spi_ret != SPI_OK_SELECT)
		elog(ERROR, "unable to query pgactive.pgactive_nodes, SPI error %d", spi_ret);

	if (SPI_processed == 0)
		elog(ERROR, "unable to fetch rows from pgactive.pgactive_nodes");

	node_seq_id = DatumGetInt16(SPI_getbinval(SPI_tuptable->vals[0],
											  SPI_tuptable->tupdesc, 1,
											  &isnull));

	if (isnull)
		elog(ERROR, "node_seq_id in pgactive.pgactive_nodes table cannot be null");

	SPI_finish();
	PopActiveSnapshot();

	if (tx_started)
		CommitTransactionCommand();

	return node_seq_id;
}
