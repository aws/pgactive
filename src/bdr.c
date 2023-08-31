/* -------------------------------------------------------------------------
 *
 * bdr.c
 *		Replication!!!
 *
 * Replication???
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		bdr.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include <sys/stat.h>
#include <unistd.h>

#include "bdr.h"
#include "bdr_locks.h"

#include "libpq-fe.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "port.h"

#include "access/commit_ts.h"
#include "access/heapam.h"
#include "access/xact.h"

#include "catalog/namespace.h"
#include "catalog/pg_extension.h"

#include "commands/dbcommands.h"
#include "commands/extension.h"
#include "commands/seclabel.h"

#include "executor/spi.h"

#include "lib/stringinfo.h"

#include "libpq/libpq-be.h"
#include "libpq/pqformat.h"

#include "nodes/execnodes.h"

#include "postmaster/bgworker.h"

#include "replication/origin.h"

#include "storage/latch.h"
#include "storage/lmgr.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/elog.h"
#include "utils/guc.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "catalog/pg_database.h"
#include "utils/pg_lsn.h"
#include "utils/snapmgr.h"
#include "utils/timestamp.h"

#define MAXCONNINFO		1024

/*
 * Maximum number of parallel jobs allowed.
 *
 * Per pg_dump and pg_restore's parallel job limit.
 */
#ifdef WIN32
#define PG_MAX_JOBS MAXIMUM_WAIT_OBJECTS
#else
#define PG_MAX_JOBS INT_MAX
#endif

/* Postgres commit 7dbfea3c455e introduced SIGHUP handler in version 13. */
#if PG_VERSION_NUM < 130000
volatile sig_atomic_t ConfigReloadPending = false;
#endif

ResourceOwner bdr_saved_resowner;
Oid			BdrSchemaOid = InvalidOid;
Oid			BdrNodesRelid = InvalidOid;
Oid			BdrConnectionsRelid = InvalidOid;
Oid			BdrConflictHistoryRelId = InvalidOid;
Oid			BdrLocksRelid = InvalidOid;
Oid			BdrLocksByOwnerRelid = InvalidOid;
Oid			BdrReplicationSetConfigRelid = InvalidOid;
Oid			BdrSupervisorDbOid = InvalidOid;

/* GUC storage */
static bool bdr_synchronous_commit;
int			bdr_debug_apply_delay;
int			bdr_max_workers;
int			bdr_max_databases;
bool		bdr_skip_ddl_replication;
bool		prev_bdr_skip_ddl_replication;

/* replaced by bdr_skip_ddl_replication for now
bool		bdr_skip_ddl_locking; */
bool		bdr_do_not_replicate;
bool		bdr_discard_mismatched_row_attributes;
bool		bdr_debug_trace_replay;
int			bdr_debug_trace_ddl_locks_level = DDL_LOCK_TRACE_STATEMENT;
char	   *bdr_extra_apply_connection_options;
int			bdr_log_min_messages = WARNING;
int			bdr_init_node_parallel_jobs;
int			bdr_max_nodes;
bool		bdr_permit_node_identifier_getter_function_creation;

PG_MODULE_MAGIC;

#if PG_VERSION_NUM >= 150000
shmem_request_hook_type bdr_prev_shmem_request_hook = NULL;
#endif

void		_PG_init(void);

PGDLLEXPORT Datum bdr_apply_pause(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_apply_resume(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_is_apply_paused(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_version(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_version_num(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_min_remote_version_num(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_variant(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_get_local_nodeid(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_parse_slot_name_sql(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_parse_replident_name_sql(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_format_slot_name_sql(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_format_replident_name_sql(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_get_workers_info(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_skip_changes(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_pause_worker_management(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_is_active_in_db(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_xact_replication_origin(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_conninfo_cmp(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum bdr_destroy_temporary_dump_directories(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum get_last_applied_xact_info(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum get_replication_lag_info(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(bdr_apply_pause);
PG_FUNCTION_INFO_V1(bdr_apply_resume);
PG_FUNCTION_INFO_V1(bdr_is_apply_paused);
PG_FUNCTION_INFO_V1(bdr_version);
PG_FUNCTION_INFO_V1(bdr_version_num);
PG_FUNCTION_INFO_V1(bdr_min_remote_version_num);
PG_FUNCTION_INFO_V1(bdr_variant);
PG_FUNCTION_INFO_V1(bdr_get_local_nodeid);
PG_FUNCTION_INFO_V1(bdr_parse_slot_name_sql);
PG_FUNCTION_INFO_V1(bdr_parse_replident_name_sql);
PG_FUNCTION_INFO_V1(bdr_format_slot_name_sql);
PG_FUNCTION_INFO_V1(bdr_format_replident_name_sql);
PG_FUNCTION_INFO_V1(bdr_get_workers_info);
PG_FUNCTION_INFO_V1(bdr_skip_changes);
PG_FUNCTION_INFO_V1(bdr_pause_worker_management);
PG_FUNCTION_INFO_V1(bdr_is_active_in_db);
PG_FUNCTION_INFO_V1(bdr_xact_replication_origin);
PG_FUNCTION_INFO_V1(bdr_conninfo_cmp);
PG_FUNCTION_INFO_V1(bdr_destroy_temporary_dump_directories);
PG_FUNCTION_INFO_V1(get_last_applied_xact_info);
PG_FUNCTION_INFO_V1(get_replication_lag_info);

static int	bdr_get_worker_pid_byid(const BDRNodeId * const nodeid, BdrWorkerType worker_type);

static bool bdr_terminate_workers_byid(const BDRNodeId * const nodeid, BdrWorkerType worker_type);

static void bdr_object_relabel(const ObjectAddress *object, const char *seclabel);

static void GetConnectionDSN(uint64 sysid, StringInfoData *dsn);
static void GetLastAppliedXactInfoFromRemoteNode(char *sysid_str,
												 BDRNodeId myid,
												 StringInfoData *dsn,
												 TransactionId *xid,
												 TimestampTz *committs,
												 TimestampTz *applied_at);

static const struct config_enum_entry bdr_debug_trace_ddl_locks_level_options[] = {
	{"debug", DDL_LOCK_TRACE_DEBUG, false},
	{"peers", DDL_LOCK_TRACE_PEERS, false},
	{"acquire_release", DDL_LOCK_TRACE_ACQUIRE_RELEASE, false},
	{"statement", DDL_LOCK_TRACE_STATEMENT, false},
	{"none", DDL_LOCK_TRACE_NONE, false},
	{NULL, 0, false}
};

/*
 * bdr_error_severity --- get string representing elevel
 */
const char *
bdr_error_severity(int elevel)
{
	const char *elevel_char;

	switch (elevel)
	{
		case DEBUG1:
			elevel_char = "DEBUG1";
			break;
		case DEBUG2:
			elevel_char = "DEBUG2";
			break;
		case DEBUG3:
			elevel_char = "DEBUG3";
			break;
		case DEBUG4:
			elevel_char = "DEBUG4";
			break;
		case DEBUG5:
			elevel_char = "DEBUG5";
			break;
		case LOG:
			elevel_char = "LOG";
			break;
		case INFO:
			elevel_char = "INFO";
			break;
		case NOTICE:
			elevel_char = "NOTICE";
			break;
		case WARNING:
			elevel_char = "WARNING";
			break;
		case ERROR:
			elevel_char = "ERROR";
			break;
		case FATAL:
			elevel_char = "FATAL";
			break;
		case PANIC:
			elevel_char = "PANIC";
			break;
		default:
			elevel_char = "???";
			break;
	}

	return elevel_char;
}

/* Postgres commit 7dbfea3c455e introduced SIGHUP handler in version 13. */
#if PG_VERSION_NUM < 130000
void
SignalHandlerForConfigReload(SIGNAL_ARGS)
{
	int			save_errno = errno;

	ConfigReloadPending = true;
	SetLatch(MyLatch);

	errno = save_errno;
}
#endif

/*
 * Get database Oid of the remotedb.
 */
static Oid
bdr_get_remote_dboid(const char *conninfo_db)
{
	PGconn	   *dbConn;
	PGresult   *res;
	char	   *remote_dboid;
	Oid			remote_dboid_i;

	elog(DEBUG3, "fetching database oid via standard connection");

	dbConn = PQconnectdb(conninfo_db);
	if (PQstatus(dbConn) != CONNECTION_OK)
	{
		ereport(FATAL,
				(errcode(ERRCODE_CONNECTION_FAILURE),
				 errmsg("get remote OID: %s", PQerrorMessage(dbConn)),
				 errdetail("Connection string is '%s'.", conninfo_db)));
	}

	res = PQexec(dbConn, "SELECT oid FROM pg_database WHERE datname = current_database()");
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		elog(FATAL, "could not fetch database oid: %s",
			 PQerrorMessage(dbConn));
	}
	if (PQntuples(res) != 1 || PQnfields(res) != 1)
	{
		elog(FATAL, "could not identify system: got %d rows and %d columns, expected 1 row and 1 column",
			 PQntuples(res), PQnfields(res));
	}

	remote_dboid = PQgetvalue(res, 0, 0);
	if (sscanf(remote_dboid, "%u", &remote_dboid_i) != 1)
		elog(ERROR, "could not parse remote database OID %s", remote_dboid);

	PQclear(res);
	PQfinish(dbConn);

	return remote_dboid_i;
}

/*
 * Establish a BDR connection
 *
 * Connects to the remote node, identifies it, and generates local and remote
 * replication identifiers and slot name. The conninfo string passed should
 * specify a dbname. It must not contain a replication= parameter.
 *
 * Does NOT enforce that the remote and local node identities must differ.
 *
 * appname may be NULL.
 *
 * The local replication identifier is not saved, the caller must do that.
 *
 * Returns the PGconn for the established connection.
 *
 * Sets out parameters:
 *   remote_ident
 *   slot_name
 *   remote_node (members)
 */
PGconn *
bdr_connect(const char *conninfo,
			Name appname,
			BDRNodeId * remote_node)
{
	PGconn	   *streamConn;
	PGconn	   *conn;
	PGresult   *res;
	StringInfoData conninfo_nrepl;
	StringInfoData conninfo_repl;
	char	   *remote_sysid;
	char	   *remote_tlid;
	char	   *servername;
	StringInfo	cmd;

	initStringInfo(&conninfo_nrepl);
	initStringInfo(&conninfo_repl);

	servername = get_connect_string(conninfo);
	appendStringInfo(&conninfo_nrepl, "application_name='%s' %s %s %s",
					 (appname == NULL ? "bdr" : NameStr(*appname)),
					 bdr_default_apply_connection_options,
					 bdr_extra_apply_connection_options,
					 (servername == NULL ? conninfo : servername));

	appendStringInfo(&conninfo_repl, "%s replication=database",
					 conninfo_nrepl.data);

	streamConn = PQconnectdb(conninfo_repl.data);
	if (PQstatus(streamConn) != CONNECTION_OK)
	{
		ereport(ERROR,
				(errcode(ERRCODE_CONNECTION_FAILURE),
				 errmsg("could not connect to the server in replication mode: %s",
						PQerrorMessage(streamConn)),
				 errdetail("Connection string is '%s'", conninfo_repl.data)));
	}

	elog(DEBUG3, "sending replication command: IDENTIFY_SYSTEM");

	res = PQexec(streamConn, "IDENTIFY_SYSTEM");
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		elog(ERROR, "could not send replication command \"%s\": %s",
			 "IDENTIFY_SYSTEM", PQerrorMessage(streamConn));
	}
	if (PQntuples(res) != 1 || PQnfields(res) < 4 || PQnfields(res) > 5)
	{
		elog(ERROR, "could not identify system: got %d rows and %d fields, expected %d rows and %d or %d fields",
			 PQntuples(res), PQnfields(res), 1, 4, 5);
	}

	if (PQnfields(res) == 5)
	{
		char	   *remote_dboid = PQgetvalue(res, 0, 4);

		if (sscanf(remote_dboid, "%u", &remote_node->dboid) != 1)
			elog(ERROR, "could not parse remote database OID %s", remote_dboid);
	}
	else
	{
		remote_node->dboid =
			bdr_get_remote_dboid((servername == NULL ? conninfo : servername));
	}

	remote_tlid = PQgetvalue(res, 0, 1);

	if (sscanf(remote_tlid, "%u", &remote_node->timeline) != 1)
		elog(ERROR, "could not parse remote tlid %s", remote_tlid);

	remote_node->timeline = BDRThisTimeLineID;

	PQclear(res);

	/* Make a non-replication connection to get the BDR node identifier. */
	conn = PQconnectdb(conninfo_nrepl.data);
	if (PQstatus(conn) != CONNECTION_OK)
	{
		ereport(ERROR,
				(errcode(ERRCODE_CONNECTION_FAILURE),
				 errmsg("could not connect to the server in non-replication mode: %s",
						PQerrorMessage(streamConn)),
				 errdetail("Connection string is '%s'", conninfo_nrepl.data)));
	}

	cmd = makeStringInfo();
	appendStringInfoString(cmd,
						   "SELECT bdr.bdr_get_node_identifier() AS node_id;");

	elog(DEBUG3, "sending command: \"%s\"", cmd->data);

	res = PQexec(conn, cmd->data);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		elog(ERROR, "could not send command \"%s\": %s",
			 cmd->data, PQerrorMessage(conn));
	}
	if (PQntuples(res) != 1 || PQnfields(res) != 1)
	{
		elog(ERROR, "could not fetch BDR node identifier: got %d rows and %d columns, expected 1 row and 1 column",
			 PQntuples(res), PQnfields(res));
	}

	remote_sysid = PQgetvalue(res, 0, 0);

	if (sscanf(remote_sysid, UINT64_FORMAT, &remote_node->sysid) != 1)
		elog(ERROR, "could not parse remote BDR node identifier %s", remote_sysid);

	pfree(cmd->data);
	pfree(cmd);
	PQclear(res);

	elog(DEBUG2, "local node " BDR_NODEID_FORMAT_WITHNAME ", remote node " BDR_NODEID_FORMAT_WITHNAME,
		 BDR_LOCALID_FORMAT_WITHNAME_ARGS, BDR_NODEID_FORMAT_WITHNAME_ARGS(*remote_node));

	pfree(conninfo_nrepl.data);
	pfree(conninfo_repl.data);
	PQfinish(conn);

	return streamConn;
}

/*
 * ----------
 * Create a slot on a remote node, and the corresponding local replication
 * identifier.
 *
 * Arguments:
 *   streamConn		Connection to use for slot creation
 *   slot_name		Name of the slot to create
 *   remote_ident	Identifier for the remote end
 *
 * Out parameters:
 *   replication_identifier		Created local replication identifier
 *   snapshot					If !NULL, snapshot ID of slot snapshot
 *
 * If a snapshot is returned it must be pfree()'d by the caller.
 * ----------
 */
/*
 * TODO we should really handle the case where the slot already exists but
 * there's no local replication identifier, by dropping and recreating the
 * slot.
 */
static void
bdr_create_slot(PGconn *streamConn, Name slot_name,
				char *remote_ident, RepOriginId *replication_identifier,
				char **snapshot)
{
	StringInfoData query;
	PGresult   *res;

	initStringInfo(&query);

	StartTransactionCommand();

	/* we want the new identifier on stable storage immediately */
	ForceSyncCommit();

	/* acquire remote decoding slot */
	appendStringInfo(&query, "CREATE_REPLICATION_SLOT \"%s\" LOGICAL %s",
					 NameStr(*slot_name), "bdr");

	elog(DEBUG3, "sending replication command: %s", query.data);

	res = PQexec(streamConn, query.data);

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		/*
		 * TODO: Should test whether this error is 'already exists' and carry
		 * on
		 */

		elog(FATAL, "could not send replication command \"%s\": status %s: %s",
			 query.data,
			 PQresStatus(PQresultStatus(res)), PQresultErrorMessage(res));
	}

	/* acquire new local identifier, but don't commit */
	*replication_identifier = replorigin_create(remote_ident);

	/* now commit local identifier */
	CommitTransactionCommand();
	CurrentResourceOwner = bdr_saved_resowner;
	elog(DEBUG1, "created replication identifier %u", *replication_identifier);

	if (snapshot)
		*snapshot = pstrdup(PQgetvalue(res, 0, 2));

	PQclear(res);
}

/*
 * Perform setup work common to all bdr worker types, such as:
 *
 * - set signal handers and unblock signals
 * - Establish db connection
 * - set search_path
 *
 */
void
bdr_bgworker_init(uint32 worker_arg, BdrWorkerType worker_type)
{
	uint16		worker_generation;
	uint16		worker_idx;
	Oid			dboid;
	BDRNodeId	myid;
	char		mystatus;

	Assert(IsBackgroundWorker);

	MyProcPort = (Port *) calloc(1, sizeof(Port));

	worker_generation = (uint16) (worker_arg >> 16);
	worker_idx = (uint16) (worker_arg & 0x0000FFFF);

	if (worker_generation != BdrWorkerCtl->worker_generation)
	{
		elog(DEBUG1, "BDR apply or perdb worker from generation %d exiting after finding shmem generation is %d",
			 worker_generation, BdrWorkerCtl->worker_generation);
		proc_exit(0);
	}

	bdr_worker_shmem_acquire(worker_type, worker_idx, false);

	/* figure out database to connect to */
	if (worker_type == BDR_WORKER_PERDB)
		dboid = bdr_worker_slot->data.perdb.c_dboid;
	else if (worker_type == BDR_WORKER_APPLY)
	{
		BdrApplyWorker *apply;
		BdrPerdbWorker *perdb;

		apply = &bdr_worker_slot->data.apply;
		apply->last_applied_xact_id = InvalidTransactionId;
		apply->last_applied_xact_committs = 0;
		apply->last_applied_xact_at = 0;
		Assert(apply->perdb != NULL);
		perdb = &apply->perdb->data.perdb;
		dboid = perdb->c_dboid;
	}
	else
		elog(FATAL, "don't know how to connect to this type of work: %u",
			 bdr_worker_type);

	Assert(OidIsValid(dboid));

	/* Establish signal handlers before unblocking signals. */
	pqsignal(SIGHUP, SignalHandlerForConfigReload);
	pqsignal(SIGTERM, die);

	/* We're now ready to receive signals */
	BackgroundWorkerUnblockSignals();

	/* Connect to our database */
	BackgroundWorkerInitializeConnectionByOid(dboid, InvalidOid, 0);
	Assert(ThisTimeLineID > 0);

	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);
	bdr_worker_slot->worker_pid = MyProcPid;
	bdr_worker_slot->worker_proc = MyProc;

	/* Check if we decided to unregister this worker. */
	if (!OidIsValid(find_bdr_nid_getter_function()))
	{
		elog(LOG, "unregistering %s worker due to missing BDR node identifier getter function",
			 worker_type == BDR_WORKER_PERDB ? "per-db" : "apply");

		LWLockRelease(BdrWorkerCtl->lock);
		goto unregister;
	}
	LWLockRelease(BdrWorkerCtl->lock);

	StartTransactionCommand();
	SPI_connect();
	PushActiveSnapshot(GetTransactionSnapshot());
	bdr_make_my_nodeid(&myid);
	mystatus = bdr_nodes_get_local_status(&myid, true);
	SPI_finish();
	PopActiveSnapshot();
	CommitTransactionCommand();

	/*
	 * We unregister per-db/apply worker when local node_status is killed or
	 * no row exists for the node in bdr_nodes. This can happen after a node
	 * is detached or BDR is removed from local node. Unregistering the worker
	 * prevents subsequent worker fail-and-restart cycles.
	 */
	if (mystatus == BDR_NODE_STATUS_KILLED)
	{
		elog(LOG, "unregistering %s worker due to node " BDR_NODEID_FORMAT " detach",
			 worker_type == BDR_WORKER_PERDB ? "per-db" : "apply",
			 BDR_NODEID_FORMAT_ARGS(myid));
		goto unregister;
	}
	else if (mystatus == '\0')
	{
		elog(LOG, "unregistering %s worker due to missing bdr.bdr_nodes row for node " BDR_NODEID_FORMAT "",
			 worker_type == BDR_WORKER_PERDB ? "per-db" : "apply",
			 BDR_NODEID_FORMAT_ARGS(myid));
		goto unregister;
	}

	/*
	 * Ensure BDR extension is up to date and get the name of the database
	 * this background is connected to.
	 */
	bdr_executor_always_allow_writes(true);
	StartTransactionCommand();
	bdr_maintain_schema(true);
	MyProcPort->database_name = MemoryContextStrdup(TopMemoryContext,
													get_database_name(MyDatabaseId));
	CommitTransactionCommand();
	bdr_executor_always_allow_writes(false);

	/* always work in our own schema */
	SetConfigOption("search_path", "bdr, pg_catalog",
					PGC_BACKEND, PGC_S_OVERRIDE);

	/* setup synchronous commit according to the user's wishes */
	SetConfigOption("synchronous_commit",
					bdr_synchronous_commit ? "local" : "off",
					PGC_BACKEND, PGC_S_OVERRIDE);	/* other context? */

	/* set log_min_messages */
	SetConfigOption("log_min_messages", bdr_error_severity(bdr_log_min_messages),
					PGC_POSTMASTER, PGC_S_OVERRIDE);

	if (worker_type == BDR_WORKER_APPLY)
	{
		/* Run as replica session replication role, this avoids FK checks. */
		SetConfigOption("session_replication_role", "replica",
						PGC_SUSET, PGC_S_OVERRIDE); /* other context? */
	}

	/*
	 * Copy our node name and, if relevant, our remote's node name into
	 * nodecache globals where we can access them later. This means we can
	 * find our node name without needing a running txn, say, for error
	 * output.
	 */
	StartTransactionCommand();
	bdr_setup_my_cached_node_names();
	if (worker_type == BDR_WORKER_APPLY)
	{
		BdrApplyWorker *apply = &bdr_worker_slot->data.apply;

		bdr_setup_cached_remote_name(&apply->remote_node);
	}
	else if (worker_type == BDR_WORKER_WALSENDER)
	{
		BdrWalsenderWorker *walsender = &bdr_worker_slot->data.walsnd;

		bdr_setup_cached_remote_name(&walsender->remote_node);
	}
	CommitTransactionCommand();

	/*
	 * Disable function body checks during replay. That's necessary because a)
	 * the creator of the function might have had it disabled b) the function
	 * might be search_path dependant and we don't fix the contents of
	 * functions.
	 */
	SetConfigOption("check_function_bodies", "off",
					PGC_INTERNAL, PGC_S_OVERRIDE);

	return;

unregister:
	bdr_worker_shmem_free(bdr_worker_slot, NULL);
	bdr_worker_slot = NULL;
	proc_exit(0);				/* unregister */
}

/*
 * Re-usable common error message
 */
void
bdr_error_nodeids_must_differ(const BDRNodeId * const nodeid)
{
	ereport(ERROR,
			(errcode(ERRCODE_INVALID_NAME),
			 errmsg("system identifier, timeline ID and/or database oid must differ between the nodes"),
			 errdetail("Both keys are (sysid, timelineid, dboid) = (" UINT64_FORMAT ",%u,%u).",
					   nodeid->sysid, nodeid->timeline, nodeid->dboid)));
}

/*
 *----------------------
 * Connect to the BDR remote end, IDENTIFY_SYSTEM, and CREATE_SLOT if necessary.
 * Generates slot name, replication identifier.
 *
 * Raises an error on failure, will not return null.
 *
 * Arguments:
 *	  connection_name:  bdr conn name from bdr.connections to get dsn from
 *
 * Returns:
 *    the libpq connection
 *
 * Out parameters:
 *    out_slot_name: the generated name of the slot on the remote end
 *    out_sysid:     the remote end's system identifier
 *    out_timeline:  the remote end's current timeline
 *    out_replication_identifier: The replication identifier for this connection
 *
 *----------------------
 */
PGconn *
bdr_establish_connection_and_slot(const char *dsn,
								  const char *application_name_suffix, Name out_slot_name,
								  BDRNodeId * out_nodeid,
								  RepOriginId *out_replication_identifier, char **out_snapshot)
{
	PGconn	   *streamConn;
	bool		tx_started = false;
	NameData	appname;
	char	   *remote_repident_name;
	BDRNodeId	myid;

	bdr_make_my_nodeid(&myid);

	snprintf(NameStr(appname), NAMEDATALEN, "%s:%s",
			 bdr_get_my_cached_node_name(), application_name_suffix);

	/*
	 * Establish BDR conn and IDENTIFY_SYSTEM, ERROR on things like connection
	 * failure.
	 */
	streamConn = bdr_connect(dsn, &appname, out_nodeid);

	bdr_slot_name(out_slot_name, &myid, out_nodeid->dboid);
	remote_repident_name = bdr_replident_name(out_nodeid, myid.dboid);
	Assert(remote_repident_name != NULL);

	if (!IsTransactionState())
	{
		tx_started = true;
		StartTransactionCommand();
	}
	*out_replication_identifier = replorigin_by_name(remote_repident_name, true);
	if (tx_started)
		CommitTransactionCommand();

	if (OidIsValid(*out_replication_identifier))
	{
		elog(DEBUG1, "found valid replication identifier %u",
			 *out_replication_identifier);
		if (out_snapshot)
			*out_snapshot = NULL;
	}
	else
	{
		/*
		 * Slot doesn't exist, create it.
		 *
		 * The per-db worker will create slots when we first init BDR, but new
		 * workers added afterwards are expected to create their own slots at
		 * connect time; that's when this runs.
		 */

		/* create local replication identifier and a remote slot */
		elog(DEBUG1, "creating new slot %s", NameStr(*out_slot_name));
		bdr_create_slot(streamConn, out_slot_name, remote_repident_name,
						out_replication_identifier, out_snapshot);
	}

	pfree(remote_repident_name);

	return streamConn;
}

static bool
bdr_do_not_replicate_check_hook(bool *newvalue, void **extra, GucSource source)
{
	if (!(*newvalue))
		/* False is always acceptable */
		return true;

	/*
	 * Only set bdr.do_not_replicate if configured via startup packet from the
	 * client application. This prevents possibly unsafe accesses to the
	 * replication identifier state in postmaster context, etc.
	 */
	if (source != PGC_S_CLIENT)
		return false;

	/*
	 * Allow bdr.do_not_replicate to be set only during local node is
	 * restoring from the dump of remote node.
	 */
	if (BdrWorkerCtl != NULL)
	{
		bool		in_init_exec_dump_restore;

		LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);
		in_init_exec_dump_restore = BdrWorkerCtl->in_init_exec_dump_restore;
		LWLockRelease(BdrWorkerCtl->lock);

		if (!in_init_exec_dump_restore)
			return false;
	}

	Assert(IsUnderPostmaster);

	return true;
}

/*
 * Override the origin replication identifier that this session will record for
 * its transactions. We need this mainly when applying dumps during
 * init_replica, so we cannot spew WARNINGs everywhere.
 */
static void
bdr_do_not_replicate_assign_hook(bool newvalue, void *extra)
{
	/* Mark these transactions as not to be replicated to other nodes */
	if (newvalue)
		replorigin_session_origin = DoNotReplicateId;
	else
		replorigin_session_origin = InvalidRepOriginId;
}

static void
bdr_discard_mismatched_row_attributes_assign_hook(bool newvalue, void *extra)
{
	if (newvalue)
	{
		/* To make sure it lands up in the log */
		elog(LOG, "WARNING: bdr.discard_missing_row_attributes has been enabled by the user");

		/* To make it more likey the user sees the message in the client */
		elog(WARNING, "WARNING: bdr.discard_missing_row_attributes has been enabled, data discrepencies may result");
	}
}

/*
 * We restrict the "unsafe" BDR settings so they can only be set in a
 * few contexts. Report whether this is such a context.
 */
static bool
bdr_guc_source_ok_for_unsafe(GucSource source)
{
	switch (source)
	{
		case PGC_S_DEFAULT:		/* hard-wired default ("boot_val") */
		case PGC_S_DYNAMIC_DEFAULT: /* default computed during initialization */
		case PGC_S_ENV_VAR:		/* postmaster environment variable */
		case PGC_S_FILE:		/* postgresql.conf */
		case PGC_S_ARGV:		/* postmaster command line */
			return true;

		case PGC_S_DATABASE_USER:	/* per-user-and-database setting */
		case PGC_S_USER:		/* per-user setting */
		case PGC_S_DATABASE:	/* per-database setting */
		case PGC_S_GLOBAL:		/* global in-database setting */
		case PGC_S_CLIENT:		/* from client connection request */
		case PGC_S_OVERRIDE:	/* special case to forcibly set default */
		case PGC_S_INTERACTIVE: /* dividing line for error reporting */
		case PGC_S_TEST:		/* test per-database or per-user setting */
		case PGC_S_SESSION:		/* SET command */
			return false;
	}
	elog(ERROR, "unreachable");
}

static bool
bdr_permit_unsafe_guc_check_hook(bool *newvalue, void **extra, GucSource source)
{
	if (!(*newvalue) && !bdr_guc_source_ok_for_unsafe(source))
	{
		/*
		 * guc.c will report an error, we just provide some more explanation
		 * first
		 */
		ereport(WARNING,
				(errmsg("unsafe BDR configuration options can not be disabled locally"),
				 errdetail("The BDR option bdr.skip_ddl_replication should only be disabled globally."),
				 errhint("See the manual for information on these options. Using them without care can break replication.")));
		return false;
	}

	return true;
}

/*
 * BDR security label implementation
 *
 * Provide object metadata for bdr using the security label infrastructure.
 */
static void
bdr_object_relabel(const ObjectAddress *object, const char *seclabel)
{
	switch (object->classId)
	{
		case RelationRelationId:

			if (!pg_class_ownercheck(object->objectId, GetUserId()))
				aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_TABLE,
							   get_rel_name(object->objectId));

			/* ensure bdr_relcache.c is coherent */
			CacheInvalidateRelcacheByRelid(object->objectId);

			bdr_parse_relation_options(seclabel, NULL);
			break;
		case DatabaseRelationId:

			if (!pg_database_ownercheck(object->objectId, GetUserId()))
				aclcheck_error(ACLCHECK_NOT_OWNER, ACL_ALL_RIGHTS_DATABASE,
							   get_database_name(object->objectId));

			/* ensure bdr_dbcache.c is coherent */
			CacheInvalidateCatalog(DatabaseRelationId);

			bdr_parse_database_options(seclabel, NULL);
			break;
		default:
			elog(ERROR, "unsupported object type: %s",
				 getObjectDescription(object));
			break;
	}
}

/*
 * Entrypoint of this module - called at shared_preload_libraries time in the
 * context of the postmaster.
 *
 * Can't use SPI, and should do as little as sensibly possible. Must initialize
 * any PGC_POSTMASTER custom GUCs, register static bgworkers, as that can't be
 * done later.
 */
void
_PG_init(void)
{
	if (!IsBinaryUpgrade)
	{
		if (!process_shared_preload_libraries_in_progress)
			ereport(ERROR,
					(errcode(ERRCODE_CONFIG_FILE_ERROR),
					 errmsg("bdr must be loaded via shared_preload_libraries")));

		if (!track_commit_timestamp)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("bdr requires track_commit_timestamp to be enabled")));

		if (wal_level < WAL_LEVEL_LOGICAL)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("bdr requires wal_level >= logical")));
	}

	/* XXX: make it changeable at SIGHUP? */
	DefineCustomBoolVariable("bdr.synchronous_commit",
							 "BDR specific synchronous commit setting.",
							 NULL,
							 &bdr_synchronous_commit,
							 false,
							 PGC_POSTMASTER,
							 0,
							 NULL, NULL, NULL);

	DefineCustomBoolVariable("bdr.log_conflicts_to_table",
							 "Log BDR conflicts to bdr.conflict_history table.",
							 NULL,
							 &bdr_log_conflicts_to_table,
							 false,
							 PGC_SIGHUP,
							 0,
							 NULL, NULL, NULL);

	DefineCustomBoolVariable("bdr.conflict_logging_include_tuples",
							 "Log whole tuples when logging BDR conflicts.",
							 NULL,
							 &bdr_conflict_logging_include_tuples,
							 true,
							 PGC_SIGHUP,
							 0,
							 NULL, NULL, NULL);
/* replaced by bdr_skip_ddl_replication for now
	DefineCustomBoolVariable("bdr.permit_ddl_locking",
							 "Allow commands that can acquire global DDL lock.",
							 NULL,
							 &bdr_permit_ddl_locking,
							 true,
							 PGC_USERSET,
							 0,
							 NULL, NULL, NULL);

	DefineCustomBoolVariable("bdr.permit_unsafe_ddl_commands",
							 "Allow commands that might cause data or " \
							 "replication problems under BDR to run.",
							 NULL,
							 &bdr_permit_unsafe_commands,
							 false,
							 PGC_SUSET,
							 0,
							 bdr_permit_unsafe_guc_check_hook, NULL, NULL);
*/

	DefineCustomBoolVariable("bdr.skip_ddl_replication",
							 "Internal. DDL replication in BDR is not a fully supported feature yet.",
							 "This parameter must be set to the same value on all BDR members, otherwise "
							 "a new node can't join BDR group or an existing node can't start BDR workers.",
							 &bdr_skip_ddl_replication,
							 true,
							 PGC_SUSET,
							 0,
							 bdr_permit_unsafe_guc_check_hook, NULL, NULL);
/* replaced by bdr_skip_ddl_replication for now
	DefineCustomBoolVariable("bdr.skip_ddl_locking",
							 "Don't acquire global DDL locks while performing DDL.",
							 "Note that it's quite dangerous to do so.",
							 &bdr_skip_ddl_locking,
							 false,
							 PGC_SUSET,
							 0,
							 bdr_permit_unsafe_guc_check_hook, NULL, NULL);
*/
	DefineCustomIntVariable("bdr.debug_apply_delay",
							"Sets apply delay for all configured BDR connections.",
							"A transaction won't be replayed until at least apply_delay "
							"milliseconds have elapsed since it was committed.",
							&bdr_debug_apply_delay,
							0, 0, INT_MAX,
							PGC_SIGHUP,
							GUC_UNIT_MS,
							NULL, NULL, NULL);

	DefineCustomIntVariable("bdr.max_ddl_lock_delay",
							"Sets maximum delay before canceling queries while waiting for global lock.",
							"If set to -1, max_standby_streaming_delay will be used.",
							&bdr_max_ddl_lock_delay,
							-1, -1, INT_MAX,
							PGC_SIGHUP,
							GUC_UNIT_MS,
							NULL, NULL, NULL);

	DefineCustomIntVariable("bdr.ddl_lock_timeout",
							"Sets maximum allowed duration of any wait for a global lock.",
							"If set to -1, lock_timeout will be used.",
							&bdr_ddl_lock_timeout,
							-1, -1, INT_MAX,
							PGC_SIGHUP,
							GUC_UNIT_MS,
							NULL, NULL, NULL);

#ifdef USE_ASSERT_CHECKING

	/*
	 * Note that this an assert-only GUC for now to avoid having tests
	 * possibly waiting forever while acquiring global lock.
	 *
	 * XXX: Might need this in production too?
	 */
	DefineCustomIntVariable("bdr.ddl_lock_acquire_timeout",
							"Sets maximum allowed duration of wait for global lock acquisition.",
							"If set to -1, the acquirer waits for global lock indefinitely.",
							&bdr_ddl_lock_acquire_timeout,
							-1, -1, INT_MAX,
							PGC_SUSET,
							GUC_UNIT_MS,
							NULL, NULL, NULL);
#endif

	/*
	 * We can't use the temp_tablespace safely for our dumps, because Pg's
	 * crash recovery is very careful to delete only particularly formatted
	 * files. Instead for now just allow user to specify dump storage.
	 */
	DefineCustomStringVariable("bdr.temp_dump_directory",
							   "Directory to store dumps for local restore.",
							   NULL,
							   &bdr_temp_dump_directory,
							   "/tmp",
							   PGC_POSTMASTER,
							   0,
							   NULL, NULL, NULL);

	DefineCustomBoolVariable("bdr.do_not_replicate",
							 "Internal. Set during local initialization from basebackup only.",
							 NULL,
							 &bdr_do_not_replicate,
							 false,
							 PGC_BACKEND,
							 0,
							 bdr_do_not_replicate_check_hook,
							 bdr_do_not_replicate_assign_hook,
							 NULL);

	DefineCustomBoolVariable("bdr.discard_mismatched_row_attributes",
							 "Internal. Only for use during recovery from faults.",
							 NULL,
							 &bdr_discard_mismatched_row_attributes,
							 false,
							 PGC_BACKEND,
							 0,
							 NULL, bdr_discard_mismatched_row_attributes_assign_hook, NULL);

	DefineCustomBoolVariable("bdr.debug_trace_replay",
							 "Log a message for each remote action processed "
							 "by a BDR apply worker.",
							 NULL,
							 &bdr_debug_trace_replay,
							 false,
							 PGC_SIGHUP,
							 0,
							 NULL, NULL, NULL);

	DefineCustomEnumVariable("bdr.debug_trace_ddl_locks_level",
							 "Log DDL locking activity at this log level.",
							 NULL,
							 &bdr_debug_trace_ddl_locks_level,
							 DDL_LOCK_TRACE_STATEMENT,
							 bdr_debug_trace_ddl_locks_level_options,
							 PGC_SIGHUP,
							 0,
							 NULL, NULL, NULL);

	DefineCustomStringVariable("bdr.extra_apply_connection_options",
							   "Connection options to add to all peer node connections.",
							   NULL,
							   &bdr_extra_apply_connection_options,
							   "",
							   PGC_SIGHUP,
							   0,
							   NULL, NULL, NULL);

	DefineCustomEnumVariable("bdr.log_min_messages",
							 "log_min_messages for BDR bgworkers.",
							 NULL,
							 &bdr_log_min_messages,
							 WARNING,
							 bdr_message_level_options,
							 PGC_SIGHUP,
							 GUC_SUPERUSER_ONLY,
							 NULL, NULL, NULL);

	DefineCustomIntVariable("bdr.init_node_parallel_jobs",
							"Sets parallel jobs to be used by dump and restore while logical join of a node.",
							"Set this to a reasonable value based on database size and number of objects it has.",
							&bdr_init_node_parallel_jobs,
							2, 1, PG_MAX_JOBS,
							PGC_SIGHUP,
							0,
							NULL, NULL, NULL);

	DefineCustomIntVariable("bdr.max_nodes",
							"Sets maximum allowed nodes in a BDR group.",
							"This parameter must be set to same value on all BDR members, otherwise "
							"a new node can't join BDR group or an existing node can't start BDR workers.",
							&bdr_max_nodes,
							4, 2, MAX_NODE_ID + 1,
							PGC_POSTMASTER,
							0,
							NULL, NULL, NULL);

	DefineCustomBoolVariable("bdr.permit_node_identifier_getter_function_creation",
							 "Internal. Set during physical node joining with bdr_init_copy only.",
							 NULL,
							 &bdr_permit_node_identifier_getter_function_creation,
							 false,
							 PGC_SUSET,
							 GUC_SUPERUSER_ONLY | GUC_DISALLOW_IN_FILE | GUC_DISALLOW_IN_AUTO_FILE,
							 bdr_permit_unsafe_guc_check_hook, NULL, NULL);

	EmitWarningsOnPlaceholders("bdr");

	/* Security label provider hook */
	register_label_provider(BDR_SECLABEL_PROVIDER, bdr_object_relabel);

	if (!IsBinaryUpgrade)
	{

		bdr_supervisor_register();

		/*
		 * Reserve shared memory segment to store bgworker connection
		 * information and hook into shmem initialization.
		 */
#if PG_VERSION_NUM >= 150000
		bdr_prev_shmem_request_hook = shmem_request_hook;
		shmem_request_hook = bdr_shmem_init;
#else
		bdr_shmem_init();
#endif

		bdr_executor_init();

		/* Set up a ProcessUtility_hook to stop unsupported commands being run */
		init_bdr_commandfilter();
	}
}

Oid
bdr_lookup_relid(const char *relname, Oid schema_oid)
{
	Oid			relid;

	relid = get_relname_relid(relname, schema_oid);

	if (!relid)
		elog(ERROR, "cache lookup failed for relation %s.%s",
			 get_namespace_name(schema_oid), relname);

	return relid;
}

/*
 * Make sure all required extensions are installed in the correct version for
 * the current database.
 *
 * Concurrent executions will block, but not fail.
 *
 * Must be called inside transaction.
 *
 * If update_extensions is true, ALTER EXTENSION commands will be issued to
 * ensure the required extension(s) are at the current version.
 */
void
bdr_maintain_schema(bool update_extensions)
{
	Relation	extrel;
	Oid			bdr_oid;
	Oid			schema_oid;

	Assert(IsTransactionState());

	PushActiveSnapshot(GetTransactionSnapshot());

	prev_bdr_skip_ddl_replication = bdr_skip_ddl_replication;
	set_config_option("bdr.skip_ddl_replication", "true",
					  PGC_SUSET, PGC_S_OVERRIDE, GUC_ACTION_LOCAL,
					  true, 0, false);

	/* make sure we're operating without other bdr workers interfering */
	extrel = table_open(ExtensionRelationId, ShareUpdateExclusiveLock);

	bdr_oid = get_extension_oid("bdr", true);
	if (bdr_oid == InvalidOid)
		elog(ERROR, "bdr extension is not installed in the current database");

	if (update_extensions)
	{
		AlterExtensionStmt alter_stmt;

		/* TODO: only do this if necessary */
		alter_stmt.options = NIL;
		alter_stmt.extname = (char *) "bdr";
		ExecAlterExtensionStmt(NULL, &alter_stmt);
	}

	table_close(extrel, NoLock);

	/* setup initial queued_cmds OID */
	schema_oid = get_namespace_oid("bdr", false);
	BdrSchemaOid = schema_oid;
	BdrNodesRelid =
		bdr_lookup_relid("bdr_nodes", schema_oid);
	BdrConnectionsRelid =
		bdr_lookup_relid("bdr_connections", schema_oid);
	QueuedDDLCommandsRelid =
		bdr_lookup_relid("bdr_queued_commands", schema_oid);
	BdrConflictHistoryRelId =
		bdr_lookup_relid("bdr_conflict_history", schema_oid);
	BdrReplicationSetConfigRelid =
		bdr_lookup_relid("bdr_replication_set_config", schema_oid);
	QueuedDropsRelid =
		bdr_lookup_relid("bdr_queued_drops", schema_oid);
	BdrLocksRelid =
		bdr_lookup_relid("bdr_global_locks", schema_oid);
	BdrLocksByOwnerRelid =
		bdr_lookup_relid("bdr_global_locks_byowner", schema_oid);
	BdrSupervisorDbOid = bdr_get_supervisordb_oid(false);

	bdr_conflict_handlers_init();

	PopActiveSnapshot();
}

Datum
bdr_apply_pause(PG_FUNCTION_ARGS)
{
	/*
	 * It's safe to pause without grabbing the segment lock; an overlapping
	 * resume won't do any harm.
	 */
	BdrWorkerCtl->pause_apply = true;
	PG_RETURN_VOID();
}

Datum
bdr_apply_resume(PG_FUNCTION_ARGS)
{
	int			i;

	LWLockAcquire(BdrWorkerCtl->lock, LW_SHARED);
	BdrWorkerCtl->pause_apply = false;

	/*
	 * To get apply workers to notice immediately we have to set all their
	 * latches. This will also force config reloads, but that's cheap and
	 * harmless.
	 */
	for (i = 0; i < bdr_max_workers; i++)
	{
		BdrWorker  *w = &BdrWorkerCtl->slots[i];

		if (w->worker_type == BDR_WORKER_APPLY)
		{
			BdrApplyWorker *apply = &w->data.apply;

			SetLatch(apply->proclatch);
		}
	}

	LWLockRelease(BdrWorkerCtl->lock);
	PG_RETURN_VOID();
}

Datum
bdr_is_apply_paused(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(BdrWorkerCtl->pause_apply);
}

Datum
bdr_version(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(BDR_VERSION_STR));
}

Datum
bdr_version_num(PG_FUNCTION_ARGS)
{
	PG_RETURN_INT32(BDR_VERSION_NUM);
}

Datum
bdr_min_remote_version_num(PG_FUNCTION_ARGS)
{
	PG_RETURN_INT32(BDR_MIN_REMOTE_VERSION_NUM);
}

Datum
bdr_variant(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(BDR_VARIANT));
}

/* Return a tuple of (sysid oid, tlid oid, dboid oid) */
Datum
bdr_get_local_nodeid(PG_FUNCTION_ARGS)
{
	Datum		values[3];
	bool		isnull[3];
	TupleDesc	tupleDesc;
	HeapTuple	returnTuple;
	char		sysid_str[33];
	BDRNodeId	myid;

	bdr_make_my_nodeid(&myid);

	if (get_call_result_type(fcinfo, NULL, &tupleDesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	memset(values, 0, sizeof(values));
	memset(isnull, 0, sizeof(isnull));

	snprintf(sysid_str, sizeof(sysid_str), UINT64_FORMAT, myid.sysid);

	values[0] = CStringGetTextDatum(sysid_str);
	values[1] = ObjectIdGetDatum(myid.timeline);
	values[2] = ObjectIdGetDatum(myid.dboid);

	returnTuple = heap_form_tuple(tupleDesc, values, isnull);

	PG_RETURN_DATUM(HeapTupleGetDatum(returnTuple));
}

Datum
bdr_parse_slot_name_sql(PG_FUNCTION_ARGS)
{
	const char *slot_name = NameStr(*PG_GETARG_NAME(0));
	Datum		values[5];
	bool		isnull[5];
	TupleDesc	tupleDesc;
	HeapTuple	returnTuple;
	char		remote_sysid_str[33];
	BDRNodeId	remote;
	Oid			local_dboid;

	if (get_call_result_type(fcinfo, NULL, &tupleDesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	memset(values, 0, sizeof(values));
	memset(isnull, 0, sizeof(isnull));

	bdr_parse_slot_name(slot_name, &remote, &local_dboid);

	snprintf(remote_sysid_str, sizeof(remote_sysid_str),
			 UINT64_FORMAT, remote.sysid);

	values[0] = CStringGetTextDatum(remote_sysid_str);
	values[1] = ObjectIdGetDatum(remote.timeline);
	values[2] = ObjectIdGetDatum(remote.dboid);
	values[3] = ObjectIdGetDatum(local_dboid);
	values[4] = CStringGetTextDatum(EMPTY_REPLICATION_NAME);

	returnTuple = heap_form_tuple(tupleDesc, values, isnull);

	PG_RETURN_DATUM(HeapTupleGetDatum(returnTuple));
}

Datum
bdr_parse_replident_name_sql(PG_FUNCTION_ARGS)
{
	const char *replident_name = text_to_cstring(PG_GETARG_TEXT_P(0));
	Datum		values[5];
	bool		isnull[5];
	TupleDesc	tupleDesc;
	HeapTuple	returnTuple;
	char		remote_sysid_str[33];
	BDRNodeId	remote;
	Oid			local_dboid;

	if (get_call_result_type(fcinfo, NULL, &tupleDesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	memset(values, 0, sizeof(values));
	memset(isnull, 0, sizeof(isnull));

	bdr_parse_replident_name(replident_name, &remote, &local_dboid);

	snprintf(remote_sysid_str, sizeof(remote_sysid_str),
			 UINT64_FORMAT, remote.sysid);

	values[0] = CStringGetTextDatum(remote_sysid_str);
	values[1] = ObjectIdGetDatum(remote.timeline);
	values[2] = ObjectIdGetDatum(remote.dboid);
	values[3] = ObjectIdGetDatum(local_dboid);
	values[4] = CStringGetTextDatum(EMPTY_REPLICATION_NAME);

	returnTuple = heap_form_tuple(tupleDesc, values, isnull);

	PG_RETURN_DATUM(HeapTupleGetDatum(returnTuple));
}

Datum
bdr_format_slot_name_sql(PG_FUNCTION_ARGS)
{
	BDRNodeId	remote;
	const char *remote_sysid_str = text_to_cstring(PG_GETARG_TEXT_P(0));
	Oid			local_dboid = PG_GETARG_OID(3);
	const char *replication_name = NameStr(*PG_GETARG_NAME(4));
	Name		slot_name;

	remote.timeline = PG_GETARG_OID(1);
	remote.dboid = PG_GETARG_OID(2);

	if (strlen(replication_name) != 0)
		elog(ERROR, "non-empty replication_name is not yet supported");

	if (sscanf(remote_sysid_str, UINT64_FORMAT, &remote.sysid) != 1)
		elog(ERROR, "parsing of remote sysid as uint64 failed");

	slot_name = (Name) palloc0(NAMEDATALEN);

	bdr_slot_name(slot_name, &remote, local_dboid);

	PG_RETURN_NAME(slot_name);
}

Datum
bdr_format_replident_name_sql(PG_FUNCTION_ARGS)
{
	BDRNodeId	remote;
	const char *remote_sysid_str = text_to_cstring(PG_GETARG_TEXT_P(0));
	Oid			local_dboid = PG_GETARG_OID(3);
	const char *replication_name = NameStr(*PG_GETARG_NAME(4));
	char	   *replident_name;

	remote.timeline = PG_GETARG_OID(1);
	remote.dboid = PG_GETARG_OID(2);

	if (strlen(replication_name) != 0)
		elog(ERROR, "non-empty replication_name is not yet supported");

	if (sscanf(remote_sysid_str, UINT64_FORMAT, &remote.sysid) != 1)
		elog(ERROR, "parsing of remote sysid as uint64 failed");

	replident_name = bdr_replident_name(&remote, local_dboid);

	PG_RETURN_TEXT_P(cstring_to_text(replident_name));
}


/*
 * You should prefer to use bdr_version_num but if you can't
 * then this will be handy.
 *
 * ERRORs if the major/minor/rev can't be parsed.
 *
 * If subrev is absent or cannot be parsed returns -1 for subrev.
 *
 * The return value is the bdr version in BDR_VERSION_NUM form.
 */
int
bdr_parse_version(const char *bdr_version_str,
				  int *o_major, int *o_minor, int *o_rev, int *o_subrev)
{
	int			nparsed,
				major,
				minor,
				rev,
				subrev;

	nparsed = sscanf(bdr_version_str, "%d.%d.%d.%d", &major, &minor, &rev, &subrev);

	if (nparsed < 3)
		elog(ERROR, "unable to parse '%s' as a BDR version number", bdr_version_str);
	else if (nparsed < 4)
		subrev = -1;

	if (o_major != NULL)
		*o_major = major;
	if (o_minor != NULL)
		*o_minor = minor;
	if (o_rev != NULL)
		*o_rev = rev;
	if (o_subrev != NULL)
		*o_subrev = subrev;

	return major * 10000 + minor * 100 + rev;
}

static void
bdr_skip_changes_cleanup(int code, Datum arg)
{
	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);
	BdrWorkerCtl->worker_management_paused = false;
	LWLockRelease(BdrWorkerCtl->lock);
}

Datum
bdr_skip_changes(PG_FUNCTION_ARGS)
{
	const char *remote_sysid_str = text_to_cstring(PG_GETARG_TEXT_P(0));
	XLogRecPtr	upto_lsn = PG_GETARG_LSN(3);
	RepOriginId nodeid;
	BDRNodeId	myid,
				remote;

	remote.timeline = PG_GETARG_OID(1);
	remote.dboid = PG_GETARG_OID(2);

	bdr_make_my_nodeid(&myid);

	/* replace bdr_permit_unsafe_commands by bdr_skip_ddl_replication for now */
	if (!bdr_skip_ddl_replication)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("skipping changes is unsafe and will cause replicas to be out of sync"),
				 errhint("Set bdr.skip_ddl_replication if you are sure you want to do this.")));

	if (upto_lsn == InvalidXLogRecPtr)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("target LSN must be nonzero")));

	if (sscanf(remote_sysid_str, UINT64_FORMAT, &remote.sysid) != 1)
		elog(ERROR, "parsing of remote sysid as uint64 failed");

	if (bdr_nodeid_eq(&myid, &remote))
		elog(ERROR, "passed ID is for the local node, can't skip changes from self");

	/* Only ever matches a replnode id owned by the local BDR node */
	nodeid = bdr_fetch_node_id_via_sysid(&remote);

	if (nodeid == InvalidRepOriginId)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("no replication identifier found for node")));

	Assert(nodeid != DoNotReplicateId);

	/*
	 * If there's a local apply worker using this origin we must terminate it
	 * before trying to advance the ID, otherwise we'll fail to advance it.
	 *
	 * We have to pause worker management so the terminated worker doesn't get
	 * restarted before we continue. We also need to make sure we re-enable
	 * worker management on exit. We don't try to stop someone else
	 * re-enabling worker management at this time; at worst, we'll just fail
	 * to advance the replication identifier with an error.
	 */
	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);
	BdrWorkerCtl->worker_management_paused = true;
	LWLockRelease(BdrWorkerCtl->lock);

	PG_ENSURE_ERROR_CLEANUP(bdr_skip_changes_cleanup, (Datum) 0);
	{
		/*
		 * We can't advance the replication identifier until we terminate any
		 * apply worker that might currently hold it at a session level.
		 *
		 * There's no way to ask an apply worker to release its session
		 * identifier. The best thing we can do is terminate the worker and
		 * wait for it to exit. Because we're blocked worker management it
		 * can't be relaunched until we give the go-ahead.
		 */
		bdr_terminate_workers_byid(&remote, BDR_WORKER_APPLY);

		/*
		 * The worker is signaled, but if it was actually running it might not
		 * have exited yet, and we need it to release its hold on the
		 * replication origin. Wait until it does.
		 */
		while (bdr_get_worker_pid_byid(&remote, BDR_WORKER_APPLY) != 0)
		{
			(void) BDRWaitLatch(&MyProc->procLatch,
								WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
								500L, PG_WAIT_EXTENSION);
			ResetLatch(&MyProc->procLatch);
			CHECK_FOR_INTERRUPTS();
		}

		/*
		 * We need a RowExclusiveLock on pg_replication_origin per docs for
		 * replorigin_advance(...).
		 */
		LockRelationOid(ReplicationOriginRelationId, RowExclusiveLock);

		/*
		 * upto_lsn is documented as being exclusive, i.e. we skip a commit
		 * starting exactly at upto_lsn. But replication starts replay at the
		 * passed LSN inclusive, so we need to increment it.
		 */
		replorigin_advance(nodeid, upto_lsn + 1, XactLastCommitEnd, false, true);

		UnlockRelationOid(ReplicationOriginRelationId, RowExclusiveLock);
	}
	PG_END_ENSURE_ERROR_CLEANUP(bdr_skip_changes_cleanup, (Datum) 0);

	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);
	BdrWorkerCtl->worker_management_paused = false;
	LWLockRelease(BdrWorkerCtl->lock);

	PG_RETURN_VOID();
}

/*
 * Look up bdr worker by sysid/timeline/dboid and get its pid if it is running,
 * or 0 if not.
 */
static int
bdr_get_worker_pid_byid(const BDRNodeId * const node, BdrWorkerType worker_type)
{
	int			pid = 0;
	BdrWorker  *worker;

	/*
	 * Right now there can only be one worker for any given remote, so we
	 * don't really have to deal with multiple workers at all.
	 */
	LWLockAcquire(BdrWorkerCtl->lock, LW_SHARED);
	worker = bdr_worker_get_entry(node, worker_type);

	if (worker != NULL && worker->worker_proc != NULL)
		pid = worker->worker_proc->pid;

	LWLockRelease(BdrWorkerCtl->lock);

	return pid;
}

Datum
bdr_get_workers_info(PG_FUNCTION_ARGS)
{
#define BDR_GET_WORKERS_PID_COLS	5
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	int			i;

	/* Construct the tuplestore and tuple descriptor */
	InitMaterializedSRF(fcinfo, 0);

	LWLockAcquire(BdrWorkerCtl->lock, LW_SHARED);
	for (i = 0; i < bdr_max_workers; i++)
	{
		BdrWorker  *w = &BdrWorkerCtl->slots[i];
		Datum		values[BDR_GET_WORKERS_PID_COLS] = {0};
		bool		nulls[BDR_GET_WORKERS_PID_COLS] = {0};
		uint64		sysid = 0;	/* keep compiler quiet */
		TimeLineID	timeline = 0;	/* keep compiler quiet */
		Oid			dboid = InvalidOid; /* keep compiler quiet */
		char		sysid_str[33];
		text	   *worker_type = NULL; /* keep compiler quiet */

		/* unused slot */
		if (w->worker_type == BDR_WORKER_EMPTY_SLOT)
			continue;

		/* unconnected slot */
		if (w->worker_proc == NULL)
			continue;

		if (w->worker_type == BDR_WORKER_APPLY)
		{
			BdrApplyWorker *aw = &w->data.apply;

			sysid = aw->remote_node.sysid;
			timeline = aw->remote_node.timeline;
			dboid = aw->remote_node.dboid;
			worker_type = cstring_to_text("apply");
		}
		else if (w->worker_type == BDR_WORKER_PERDB)
		{
			BdrPerdbWorker *pw = &w->data.perdb;

			nulls[0] = true;
			nulls[1] = true;
			dboid = pw->p_dboid;
			worker_type = cstring_to_text("per-db");
		}
		else if (w->worker_type == BDR_WORKER_WALSENDER)
		{
			BdrWalsenderWorker *ws = &w->data.walsnd;

			sysid = ws->remote_node.sysid;
			timeline = ws->remote_node.timeline;
			dboid = ws->remote_node.dboid;
			worker_type = cstring_to_text("walsender");
		}

		if (w->worker_type != BDR_WORKER_PERDB)
		{
			snprintf(sysid_str, sizeof(sysid_str), UINT64_FORMAT, sysid);
			values[0] = CStringGetTextDatum(sysid_str);
			values[1] = ObjectIdGetDatum(timeline);
		}
		values[2] = ObjectIdGetDatum(dboid);
		values[3] = PointerGetDatum(worker_type);
		values[4] = Int32GetDatum(w->worker_pid);

		tuplestore_putvalues(rsinfo->setResult, rsinfo->setDesc,
							 values, nulls);
	}
	LWLockRelease(BdrWorkerCtl->lock);

	PG_RETURN_VOID();
#undef BDR_GET_WORKERS_PID_COLS
}

/*
 * Terminate the worker with the identified role and remote peer that
 * is operating on the current database.
 */
static bool
bdr_terminate_workers_byid(const BDRNodeId * const node, BdrWorkerType worker_type)
{
	int			pid = bdr_get_worker_pid_byid(node, worker_type);

	if (pid == 0)
		return false;

	/*
	 * We could call kill() directly but this way we do the permissions
	 * checks, get pgroup handling, etc. It means we look the pid up in PGPROC
	 * again, but that's harmless enough. There's an unavoidable race with pid
	 * recycling no matter what we do and it's no worse whether or not we go
	 * via pg_terminate_backend.
	 */
#if PG_VERSION_NUM >= 140000
	return DatumGetBool(DirectFunctionCall2(pg_terminate_backend, Int32GetDatum(pid), Int64GetDatum(0)));
#else
	return DatumGetBool(DirectFunctionCall1(pg_terminate_backend, Int32GetDatum(pid)));
#endif
}

/*
 * This function is used for debugging and tests, mainly to make unit tests more
 * predictable. It pauses BDR worker management and stops new worker launches
 * until unpaused.
 *
 * The pause applies across all BDR nodes on the current instance. When unpaused,
 * the caller should signal bdr_connections_changed() on every node.
 *
 * This function is intentionally undocumented and isn't for normal use.
 */
Datum
bdr_pause_worker_management(PG_FUNCTION_ARGS)
{
	bool		pause = PG_GETARG_BOOL(0);

	/* replace bdr_permit_unsafe_commands by bdr_skip_ddl_replication for now */
	if (pause && !bdr_skip_ddl_replication)
		elog(ERROR, "this function is for internal test use only");

	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);
	BdrWorkerCtl->worker_management_paused = pause;
	LWLockRelease(BdrWorkerCtl->lock);

	elog(LOG, "BDR worker management %s", pause ? "paused" : "unpaused");

	PG_RETURN_VOID();
}

/*
 * Report whether BDR is active on the DB.
 */
Datum
bdr_is_active_in_db(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(bdr_is_bdr_activated_db(MyDatabaseId));
}

Datum
bdr_xact_replication_origin(PG_FUNCTION_ARGS)
{
	TransactionId xid = PG_GETARG_UINT32(0);
	RepOriginId data;
	TimestampTz ts;

	TransactionIdGetCommitTsData(xid, &ts, &data);

	PG_RETURN_INT32((int32) data);
}

/*
 * Postgres commit 9e98583898c3/a19e5cee635d introduced this function in
 * version 15.
 */
#if PG_VERSION_NUM < 150000
void
InitMaterializedSRF(FunctionCallInfo fcinfo, bits32 flags)
{
	bool		random_access;
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	Tuplestorestate *tupstore;
	MemoryContext old_context,
				per_query_ctx;
	TupleDesc	stored_tupdesc;

	/* check to see if caller supports returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize) ||
		((flags & MAT_SRF_USE_EXPECTED_DESC) != 0 && rsinfo->expectedDesc == NULL))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not allowed in this context")));

	/*
	 * Store the tuplestore and the tuple descriptor in ReturnSetInfo.  This
	 * must be done in the per-query memory context.
	 */
	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	old_context = MemoryContextSwitchTo(per_query_ctx);

	/* build a tuple descriptor for our result type */
	if ((flags & MAT_SRF_USE_EXPECTED_DESC) != 0)
		stored_tupdesc = CreateTupleDescCopy(rsinfo->expectedDesc);
	else
	{
		if (get_call_result_type(fcinfo, NULL, &stored_tupdesc) != TYPEFUNC_COMPOSITE)
			elog(ERROR, "return type must be a row type");
	}

	/* If requested, bless the tuple descriptor */
	if ((flags & MAT_SRF_BLESS) != 0)
		BlessTupleDesc(stored_tupdesc);

	random_access = (rsinfo->allowedModes & SFRM_Materialize_Random) != 0;

	tupstore = tuplestore_begin_heap(random_access, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = stored_tupdesc;
	MemoryContextSwitchTo(old_context);
}
#endif

/*
 * Compare two passed-in connection strings and return true if they are
 * equivalent, regardless of the order of the connection string entries. Return
 * error if any of the passed-in connection string is invalid.
 */
Datum
bdr_conninfo_cmp(PG_FUNCTION_ARGS)
{
	char	   *conninfo1 = text_to_cstring(PG_GETARG_TEXT_PP(0));
	char	   *conninfo2 = text_to_cstring(PG_GETARG_TEXT_PP(1));
	PQconninfoOption *opts1 = NULL;
	PQconninfoOption *opts2 = NULL;
	char	   *err = NULL;
	PQconninfoOption *opt1;
	PQconninfoOption *opt2;

	opts1 = PQconninfoParse(conninfo1, &err);
	if (opts1 == NULL)
	{
		/* The error string is malloc'd, so we must free it explicitly */
		char	   *errcopy = err ? pstrdup(err) : "out of memory";

		PQfreemem(err);
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("invalid connection string syntax: %s", errcopy)));
	}

	opts2 = PQconninfoParse(conninfo2, &err);
	if (opts2 == NULL)
	{
		/* The error string is malloc'd, so we must free it explicitly */
		char	   *errcopy = err ? pstrdup(err) : "out of memory";

		PQfreemem(err);
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("invalid connection string syntax: %s", errcopy)));
	}

	for (opt1 = opts1; opt1->keyword != NULL; ++opt1)
	{
		bool		found = false;

		for (opt2 = opts2; opt2->keyword != NULL; ++opt2)
		{
			if (pg_strcasecmp(opt1->keyword, opt2->keyword) == 0)
			{
				if (opt1->val == NULL && opt2->val == NULL)
				{
					found = true;
					break;
				}

				if ((opt1->val == NULL && opt2->val != NULL) ||
					(opt1->val != NULL && opt2->val == NULL))
					break;

				if (pg_strcasecmp(opt1->val, opt2->val) == 0)
				{
					found = true;
					break;
				}
				else
					break;
			}
		}

		if (found == false)
		{
			PQconninfoFree(opts1);
			PQconninfoFree(opts2);
			PG_RETURN_BOOL(false);
		}
	}

	PQconninfoFree(opts1);
	PQconninfoFree(opts2);
	PG_RETURN_BOOL(true);
}

void
destroy_temp_dump_dirs(int code, Datum arg)
{
	DIR		   *dir;
	struct dirent *de;
	char		prefix[MAXPGPATH];

	snprintf(prefix, sizeof(prefix), "%s/%s-" UINT64_FORMAT "-",
			 bdr_temp_dump_directory, TEMP_DUMP_DIR_PREFIX,
			 GetSystemIdentifier());

	dir = AllocateDir(bdr_temp_dump_directory);
	while ((de = ReadDir(dir, bdr_temp_dump_directory)) != NULL)
	{
		char		path[MAXPGPATH];
		struct stat st;

		CHECK_FOR_INTERRUPTS();

		/* Skip special stuff */
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s", bdr_temp_dump_directory,
				 de->d_name);

		if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
		{
			if (strncmp(de->d_name, prefix, strlen(prefix)) == 0)
				destroy_temp_dump_dir(0, CStringGetDatum(path));
		}
	}
	FreeDir(dir);
}

void
destroy_temp_dump_dir(int code, Datum arg)
{
	struct stat st;
	const char *dir = DatumGetCString(arg);

	if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode))
	{
		if (!rmtree(dir, true))
			elog(WARNING, "failed to clean up BDR dump temporary directory %s", dir);
	}
}

Datum
bdr_destroy_temporary_dump_directories(PG_FUNCTION_ARGS)
{
	destroy_temp_dump_dirs(0, 0);

	PG_RETURN_VOID();
}

Datum
get_last_applied_xact_info(PG_FUNCTION_ARGS)
{
	Datum		values[3];
	bool		isnull[3];
	TupleDesc	tupleDesc;
	HeapTuple	returnTuple;
	BDRNodeId	target;
	char	   *sysid_str = text_to_cstring(PG_GETARG_TEXT_PP(0));
	BdrWorker  *worker;
	bool		lock_acquired = false;
	TransactionId xid = InvalidTransactionId;
	TimestampTz committs = 0;
	TimestampTz applied_at = 0;

	if (!bdr_is_bdr_activated_db(MyDatabaseId))
		PG_RETURN_VOID();

	if (sscanf(sysid_str, UINT64_FORMAT, &target.sysid) != 1)
		elog(ERROR, "parsing of sysid as uint64 failed");

	target.timeline = PG_GETARG_OID(1);
	target.dboid = PG_GETARG_OID(2);

	if (get_call_result_type(fcinfo, NULL, &tupleDesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	memset(values, 0, sizeof(values));
	memset(isnull, 0, sizeof(isnull));

	if (!LWLockHeldByMe(BdrWorkerCtl->lock))
	{
		LWLockAcquire(BdrWorkerCtl->lock, LW_SHARED);
		lock_acquired = true;
	}

	if (find_apply_worker_slot(&target, &worker) != -1)
	{
		BdrApplyWorker *apply;

		apply = &worker->data.apply;
		xid = apply->last_applied_xact_id;
		committs = apply->last_applied_xact_committs;
		applied_at = apply->last_applied_xact_at;
	}
	else
		elog(LOG, "could not find apply worker for a given node " BDR_NODEID_FORMAT "",
			 BDR_NODEID_FORMAT_ARGS(target));

	values[0] = ObjectIdGetDatum(xid);
	values[1] = TimestampTzGetDatum(committs);
	values[2] = TimestampTzGetDatum(applied_at);

	if (lock_acquired)
		LWLockRelease(BdrWorkerCtl->lock);

	returnTuple = heap_form_tuple(tupleDesc, values, isnull);
	PG_RETURN_DATUM(HeapTupleGetDatum(returnTuple));
}

static void
GetConnectionDSN(uint64 sysid, StringInfoData *dsn)
{
	char		sysid_str[33];
	char	   *result;
	StringInfoData cmd;

	snprintf(sysid_str, sizeof(sysid_str), UINT64_FORMAT, sysid);

	initStringInfo(&cmd);
	appendStringInfo(&cmd, "SELECT conn_dsn FROM bdr.bdr_connections WHERE conn_sysid = '%s';",
					 sysid_str);

	if (SPI_connect() != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed");

	if (SPI_execute(cmd.data, false, 0) != SPI_OK_SELECT)
		elog(ERROR, "SPI_execute failed: %s", cmd.data);

	Assert(SPI_processed == 1);
	Assert(SPI_tuptable->tupdesc->natts == 1);

	result = SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1);

	appendStringInfo(dsn, "%s", result);

	if (SPI_finish() != SPI_OK_FINISH)
		elog(ERROR, "SPI_finish failed");

	pfree(cmd.data);
}

static void
GetLastAppliedXactInfoFromRemoteNode(char *sysid_str,
									 BDRNodeId myid,
									 StringInfoData *dsn,
									 TransactionId *xid,
									 TimestampTz *committs,
									 TimestampTz *applied_at)
{
	PGconn	   *conn;
	PGresult   *res;
	StringInfoData cmd;

	conn = bdr_connect_nonrepl(dsn->data, "apply_info");

	/* Make sure BDR is actually present and active on the remote */
	bdr_ensure_ext_installed(conn);

	*xid = InvalidTransactionId;
	*committs = 0;
	*applied_at = 0;

	PG_ENSURE_ERROR_CLEANUP(bdr_cleanup_conn_close,
							PointerGetDatum(&conn));
	{
		initStringInfo(&cmd);
		appendStringInfo(&cmd, "SELECT * FROM bdr.get_last_applied_xact_info('%s', %u, %u);",
						 sysid_str, myid.timeline, myid.dboid);

		res = PQexec(conn, cmd.data);

		if (PQresultStatus(res) != PGRES_TUPLES_OK)
		{
			elog(ERROR, "unable to fetch apply info: status %s: %s",
				 PQresStatus(PQresultStatus(res)), PQresultErrorMessage(res));
		}

		if (PQntuples(res) == 0)
			goto done;

		if (PQntuples(res) != 1 || PQnfields(res) != 3)
		{
			elog(ERROR, "could not fetch apply info: got %d rows and %d columns, expected 1 row and 3 columns",
				 PQntuples(res), PQnfields(res));
		}

		*xid = DatumGetObjectId(DirectFunctionCall1(oidin,
													CStringGetDatum(PQgetvalue(res, 0, 0))));
		*committs = DatumGetTimestampTz(
										DirectFunctionCall3(timestamptz_in,
															CStringGetDatum(PQgetvalue(res, 0, 1)),
															ObjectIdGetDatum(InvalidOid),
															Int32GetDatum(-1)));
		*applied_at = DatumGetTimestampTz(
										  DirectFunctionCall3(timestamptz_in,
															  CStringGetDatum(PQgetvalue(res, 0, 2)),
															  ObjectIdGetDatum(InvalidOid),
															  Int32GetDatum(-1)));
	}
	PG_END_ENSURE_ERROR_CLEANUP(bdr_cleanup_conn_close,
								PointerGetDatum(&conn));

done:
	pfree(cmd.data);
	PQclear(res);
	PQfinish(conn);
}

Datum
get_replication_lag_info(PG_FUNCTION_ARGS)
{
#define GET_REPLICATION_LAG_INFO_COLS	7
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	int			i;
	BDRNodeId	myid;
	char		local_sysid_str[33];

	if (!bdr_is_bdr_activated_db(MyDatabaseId))
		PG_RETURN_VOID();

	bdr_make_my_nodeid(&myid);
	snprintf(local_sysid_str, sizeof(local_sysid_str), UINT64_FORMAT,
			 myid.sysid);

	/* Construct the tuplestore and tuple descriptor */
	InitMaterializedSRF(fcinfo, 0);

	LWLockAcquire(BdrWorkerCtl->lock, LW_SHARED);
	for (i = 0; i < bdr_max_workers; i++)
	{
		BdrWorker  *w = &BdrWorkerCtl->slots[i];
		Datum		values[GET_REPLICATION_LAG_INFO_COLS] = {0};
		bool		nulls[GET_REPLICATION_LAG_INFO_COLS] = {0};
		BdrWalsenderWorker *ws;
		StringInfoData conn_dsn;
		TransactionId last_applied_xact_id;
		TimestampTz last_applied_xact_committs;
		TimestampTz last_applied_xact_at;

		/* unused slot */
		if (w->worker_type == BDR_WORKER_EMPTY_SLOT)
			continue;

		/* unconnected slot */
		if (w->worker_proc == NULL)
			continue;

		/* we'll deal with walsender workers only */
		if (w->worker_type == BDR_WORKER_APPLY ||
			w->worker_type == BDR_WORKER_PERDB)
			continue;

		Assert(w->worker_type == BDR_WORKER_WALSENDER);
		ws = &w->data.walsnd;

		initStringInfo(&conn_dsn);
		GetConnectionDSN(ws->remote_node.sysid, &conn_dsn);
		GetLastAppliedXactInfoFromRemoteNode(local_sysid_str, myid, &conn_dsn,
											 &last_applied_xact_id,
											 &last_applied_xact_committs,
											 &last_applied_xact_at);
		pfree(conn_dsn.data);

		values[0] = NameGetDatum(&ws->slot->data.name);
		values[1] = ObjectIdGetDatum(ws->last_sent_xact_id);
		values[2] = TimestampTzGetDatum(ws->last_sent_xact_committs);
		values[3] = TimestampTzGetDatum(ws->last_sent_xact_at);
		values[4] = ObjectIdGetDatum(last_applied_xact_id);
		values[5] = TimestampTzGetDatum(last_applied_xact_committs);
		values[6] = TimestampTzGetDatum(last_applied_xact_at);
		tuplestore_putvalues(rsinfo->setResult, rsinfo->setDesc,
							 values, nulls);
	}
	LWLockRelease(BdrWorkerCtl->lock);

	PG_RETURN_VOID();
#undef GET_REPLICATION_LAG_INFO_COLS
}
