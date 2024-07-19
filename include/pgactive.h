/*
 * pgactive.h
 *
 * Active-active Replication
 *
 * Copyright (c) 2012-2015, PostgreSQL Global Development Group
 *
 * pgactive.h
 */
#ifndef pgactive_H
#define pgactive_H

#include "miscadmin.h"
#include "access/xlogdefs.h"
#include "postmaster/bgworker.h"

/* Postgres commit 7dbfea3c455e introduced SIGHUP handler in version 13. */
#if PG_VERSION_NUM >= 130000
#include "postmaster/interrupt.h"
#endif

#include "replication/logical.h"
#include "utils/resowner.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lock.h"
#include "tcop/utility.h"

#include "lib/ilist.h"

#include "libpq-fe.h"

#include "pgactive_config.h"
#include "pgactive_internal.h"
#include "pgactive_version.h"
#include "pgactive_compat.h"
#include "nodes/execnodes.h"

#define NODEID_BITS		10
#define MAX_NODE_ID		((1 << NODEID_BITS) - 1)

/* Right now replication_name isn't used; make it easily found for later */
#define EMPTY_REPLICATION_NAME ""

/*
 * pgactive_NODEID_FORMAT is used in fallback_application_name. It's distinct from
 * pgactive_NODE_ID_FORMAT in that it doesn't include the remote dboid as that may
 * not be known yet, just (sysid,tlid,dboid,replication_name) .
 *
 * Use pgactive_LOCALID_FORMAT_ARGS to sub it in to format strings, or pgactive_NODEID_FORMAT_ARGS(node)
 * to format a pgactiveNodeId.
 *
 * The WITHNAME variant does a cache lookup to add the node name. It's only safe where
 * the nodecache exists.
 */
#define pgactive_NODEID_FORMAT "("UINT64_FORMAT",%u,%u,%s)"
#define pgactive_NODEID_FORMAT_WITHNAME "%s ("UINT64_FORMAT",%u,%u,%s)"

#if PG_VERSION_NUM >= 150000
#define ThisTimeLineID	GetWALInsertionTimeLine()
#endif

#define pgactive_LOCALID_FORMAT_ARGS \
	pgactive_get_nid_internal(), pgactiveThisTimeLineID, MyDatabaseId, EMPTY_REPLICATION_NAME

/*
 * For use with pgactive_NODEID_FORMAT_WITHNAME, print our node id tuple and name.
 * The node name used is stored in the pgactive nodecache and is accessible outside
 * transaction scope when in a pgactive bgworker. For a normal backend a syscache
 * lookup may be performed to find the node name if we're already in a
 * transaction, otherwise (none) is returned.
 */
#define pgactive_LOCALID_FORMAT_WITHNAME_ARGS \
	pgactive_get_my_cached_node_name(), pgactive_LOCALID_FORMAT_ARGS

/*
 * print helpers for node IDs, for use with pgactive_NODEID_FORMAT.
 *
 * MULTIPLE EVALUATION HAZARD.
 */
#define pgactive_NODEID_FORMAT_ARGS(node) \
	(node).sysid, (node).timeline, (node).dboid, EMPTY_REPLICATION_NAME

/*
 * This argument set is for pgactive_NODE_ID_FORMAT_WITHNAME, for use within an
 * apply worker or a walsender output plugin. The argument name should be the
 * peer node's ID. Since it's for use outside transaction scope we can't look
 * up other node IDs, and will print (none) if the node ID passed isn't the
 * peer node ID.
 *
 * TODO: If we add an eager nodecache that reloads on invalidations we can
 * print all node names and get rid of this hack.
 *
 * MULTIPLE EVALUATION HAZARD.
 */
#define pgactive_NODEID_FORMAT_WITHNAME_ARGS(node) \
	pgactive_get_my_cached_remote_name(&(node)), pgactive_NODEID_FORMAT_ARGS(node)

#define pgactive_LIBRARY_NAME "pgactive"
#define pgactive_RESTORE_CMD "pg_restore"
#define pgactive_DUMP_CMD "pgactive_dump"

#define pgactive_SUPERVISOR_DBNAME "pgactive_supervisordb"

#define pgactive_LOGICAL_MSG_PREFIX "pgactive"

#define pgactive_SECLABEL_PROVIDER "pgactive"

#define pgactive_INCLUDE_REPLICATION_SET_NAME "include_set"
#define pgactive_EXCLUDE_REPLICATION_SET_NAME "exclude_set"

static const struct config_enum_entry pgactive_message_level_options[] = {
	{"debug5", DEBUG5, false},
	{"debug4", DEBUG4, false},
	{"debug3", DEBUG3, false},
	{"debug2", DEBUG2, false},
	{"debug1", DEBUG1, false},
	{"debug", DEBUG2, true},
	{"info", INFO, false},
	{"notice", NOTICE, false},
	{"warning", WARNING, false},
	{"error", ERROR, false},
	{"log", LOG, false},
	{"fatal", FATAL, false},
	{"panic", PANIC, false},
	{NULL, 0, false}
};

/*
 * Don't include libpq here, msvc infrastructure requires linking to libpq
 * otherwise.
 */
struct pg_conn;

/* Forward declarations */
struct TupleTableSlot;			/* from executor/tuptable.h */
struct EState;					/* from nodes/execnodes.h */
struct ScanKeyData;				/* from access/skey.h for ScanKey */
enum LockTupleMode;				/* from access/heapam.h */

#if PG_VERSION_NUM >= 150000
extern shmem_request_hook_type pgactive_prev_shmem_request_hook;
#endif

/*
 * Flags to indicate which fields are present in a begin record sent by the
 * output plugin.
 */
typedef enum pgactiveOutputBeginFlags
{
	pgactive_OUTPUT_TRANSACTION_HAS_ORIGIN = 1
} pgactiveOutputBeginFlags;

/*
 * pgactive conflict detection: type of conflict that was identified.
 *
 * Must correspond to pgactive.pgactive_conflict_type SQL enum and
 * pgactive_conflict_type_get_datum (...)
 */
typedef enum pgactiveConflictType
{
	pgactiveConflictType_InsertInsert,
	pgactiveConflictType_InsertUpdate,
	pgactiveConflictType_UpdateUpdate,
	pgactiveConflictType_UpdateDelete,
	pgactiveConflictType_DeleteDelete,
	pgactiveConflictType_UnhandledTxAbort
}			pgactiveConflictType;

/*
 * pgactive conflict detection: how the conflict was resolved (if it was).
 *
 * Must correspond to pgactive.pgactive_conflict_resolution SQL enum and
 * pgactive_conflict_resolution_get_datum(...)
 */
typedef enum pgactiveConflictResolution
{
	pgactiveConflictResolution_ConflictTriggerSkipChange,
	pgactiveConflictResolution_ConflictTriggerReturnedTuple,
	pgactiveConflictResolution_LastUpdateWins_KeepLocal,
	pgactiveConflictResolution_LastUpdateWins_KeepRemote,
	pgactiveConflictResolution_DefaultApplyChange,
	pgactiveConflictResolution_DefaultSkipChange,
	pgactiveConflictResolution_UnhandledTxAbort
}			pgactiveConflictResolution;

typedef struct pgactiveConflictHandler
{
	Oid			handler_oid;
	pgactiveConflictType handler_type;
	uint64		timeframe;
}			pgactiveConflictHandler;

/* How detailed logging of DDL locks is */
enum pgactiveDDLLockTraceLevel
{
	/* Everything */
	DDL_LOCK_TRACE_DEBUG,
	/* Report acquire/release on peers, not just node doing DDL */
	DDL_LOCK_TRACE_PEERS,
	/* When locks are acquired/released */
	DDL_LOCK_TRACE_ACQUIRE_RELEASE,
	/* Only statements requesting DDL lock */
	DDL_LOCK_TRACE_STATEMENT,
	/* No DDL lock tracing */
	DDL_LOCK_TRACE_NONE
};

/*
 * This structure is for caching relation specific information, such as
 * conflict handlers.
 */
typedef struct pgactiveRelation
{
	/* hash key */
	Oid			reloid;

	bool		valid;

	Relation	rel;

	pgactiveConflictHandler *conflict_handlers;
	size_t		conflict_handlers_len;

	/* ordered list of replication sets of length num_* */
	char	  **replication_sets;
	/* -1 for no configured set */
	int			num_replication_sets;

	bool		computed_repl_valid;
	bool		computed_repl_insert;
	bool		computed_repl_update;
	bool		computed_repl_delete;
}			pgactiveRelation;

typedef struct pgactiveTupleData
{
	Datum		values[MaxTupleAttributeNumber];
	bool		isnull[MaxTupleAttributeNumber];
	bool		changed[MaxTupleAttributeNumber];
}			pgactiveTupleData;

/*
 * pgactiveApplyWorker describes a pgactive worker connection.
 *
 * This struct is stored in an array in shared memory, so it can't have any
 * pointers.
 */
typedef struct pgactiveApplyWorker
{
	/* oid of the database this worker is applying changes to */
	Oid			dboid;

	/* assigned perdb worker slot */
	struct pgactiveWorker *perdb;

	/*
	 * Identification for the remote db we're connecting to; used to find the
	 * appropriate pgactive.connections row, etc.
	 */
	pgactiveNodeId remote_node;

	/*
	 * If not InvalidXLogRecPtr, stop replay at this point and exit.
	 *
	 * To save shmem space in apply workers, this is reset to
	 * InvalidXLogRecPtr if replay is successfully completed instead of
	 * setting a separate flag.
	 */
	XLogRecPtr	replay_stop_lsn;

	/* Request that the remote forward all changes from other nodes */
	bool		forward_changesets;

	/*
	 * The apply worker's latch from the PROC array, for use from other
	 * backends
	 *
	 * Must only be accessed with the pgactive worker shmem control segment
	 * lock held.
	 */
	Latch	   *proclatch;

	/* last applied transaction id */
	TransactionId last_applied_xact_id;

	/* last applied transaction commit timestamp */
	TimestampTz last_applied_xact_committs;

	/* timestamp at which last change was applied */
	TimestampTz last_applied_xact_at;
}			pgactiveApplyWorker;

/*
 * pgactivePerdbCon describes a per-database worker, a static bgworker that manages
 * pgactive for a given DB.
 */
typedef struct pgactivePerdbWorker
{
	/* Oid of the local database to connect to */
	Oid			c_dboid;

	/*
	 * Number of 'r'eady peer nodes not including self. -1 if not initialized
	 * yet.
	 *
	 * Note that we may have more connections than this due to nodes that are
	 * still joining, or fewer due to nodes that are beginning to detach.
	 */
	int			nnodes;

	/*
	 * The perdb worker's latch from the PROC array, for use from other
	 * backends
	 *
	 * Must only be accessed with the pgactive worker shmem control segment
	 * lock held.
	 */
	Latch	   *proclatch;

	/* Oid of the database the worker is attached to - populated after start */
	Oid			p_dboid;
}			pgactivePerdbWorker;

/*
 * Walsender worker. These are only allocated while a output plugin is active.
 */
typedef struct pgactiveWalsenderWorker
{
	struct WalSnd *walsender;
	struct ReplicationSlot *slot;

	/* Identification for the remote the connection comes from. */
	pgactiveNodeId remote_node;

	/* last sent transaction id */
	TransactionId last_sent_xact_id;

	/* last sent transaction commit timestamp */
	TimestampTz last_sent_xact_committs;

	/* timestamp at which last change was sent */
	TimestampTz last_sent_xact_at;
}			pgactiveWalsenderWorker;

/*
 * Type of pgactive worker in a pgactiveWorker struct
 *
 * Note that the supervisor worker doesn't appear here, it has its own
 * dedicated entry in the shmem segment.
 */
typedef enum
{
	/*
	 * This shm array slot is unused and may be allocated. Must be zero, as
	 * it's set by memset(...) during shm segment init.
	 */
	pgactive_WORKER_EMPTY_SLOT,
	/* This shm array slot contains data for a pgactiveApplyWorker */
	pgactive_WORKER_APPLY,
	/* This is data for a per-database worker pgactivePerdbWorker */
	pgactive_WORKER_PERDB,
	/* This is data for a walsenders currently streaming data out */
	pgactive_WORKER_WALSENDER
}			pgactiveWorkerType;

/*
 * pgactiveWorker entries describe shared memory slots that keep track of
 * all pgactive worker types. A slot may contain data for a number of different
 * kinds of worker; this union makes sure each slot is the same size and
 * is easily accessed via an array.
 */
typedef struct pgactiveWorker
{
	/* Type of worker. Also used to determine if this shm slot is free. */
	pgactiveWorkerType worker_type;

	/* pid worker if running, or 0 */
	pid_t		worker_pid;

	/* proc entry of worker if running, or NULL */
	PGPROC	   *worker_proc;

	union data
	{
		pgactiveApplyWorker apply;
		pgactivePerdbWorker perdb;
		pgactiveWalsenderWorker walsnd;
	}			data;

}			pgactiveWorker;

/*
 * Attribute numbers for pgactive.pgactive_nodes and pgactive.pgactive_connections
 *
 * This must only ever be appended to, since modifications that change attnos
 * will break upgrades. It must match the column attnos reported by the regression
 * tests in results/schema.out .
 */
typedef enum pgactiveNodesAttno
{
	pgactive_NODES_ATT_SYSID = 1,
	pgactive_NODES_ATT_TIMELINE,
	pgactive_NODES_ATT_DBOID,
	pgactive_NODES_ATT_STATUS,
	pgactive_NODES_ATT_NAME,
	pgactive_NODES_ATT_LOCAL_DSN,
	pgactive_NODES_ATT_INIT_FROM_DSN,
	pgactive_NODES_ATT_READ_ONLY,
	pgactive_NODES_ATT_SEQ_ID
}			pgactiveNodesAttno;

typedef enum pgactiveConnectionsAttno
{
	pgactive_CONN_ATT_SYSID = 1,
	pgactive_CONN_ATT_TIMELINE,
	pgactive_CONN_ATT_DBOID,
	pgactive_CONN_ATT_ORIGIN_SYSID,
	pgactive_CONN_ATT_ORIGIN_TIMELINE,
	pgactive_CONN_ATT_ORIGIN_DBOID,
	pgactive_CONN_DSN,
	pgactive_CONN_APPLY_DELAY,
	pgactive_CONN_REPLICATION_SETS
}			pgactiveConnectionsAttno;

typedef struct pgactiveFlushPosition
{
	dlist_node	node;
	XLogRecPtr	local_end;
	XLogRecPtr	remote_end;
}			pgactiveFlushPosition;

/* GUCs */
extern int	pgactive_debug_apply_delay;
extern int	pgactive_max_workers;
extern int	pgactive_max_databases;
extern char *pgactive_temp_dump_directory;
extern bool pgactive_log_conflicts_to_table;
extern bool pgactive_log_conflicts_to_logfile;
extern bool pgactive_conflict_logging_include_tuples;

/* replaced by pgactive_skip_ddl_replication for now
extern bool pgactive_permit_ddl_locking;
extern bool pgactive_permit_unsafe_commands;
extern bool pgactive_skip_ddl_locking;
*/
extern bool pgactive_skip_ddl_replication;
extern bool prev_pgactive_skip_ddl_replication;
extern bool pgactive_do_not_replicate;
extern bool pgactive_discard_mismatched_row_attributes;
extern int	pgactive_max_ddl_lock_delay;
extern int	pgactive_ddl_lock_timeout;
#ifdef USE_ASSERT_CHECKING
extern int	pgactive_ddl_lock_acquire_timeout;
#endif
extern bool pgactive_debug_trace_replay;
extern int	pgactive_debug_trace_ddl_locks_level;
extern char *pgactive_extra_apply_connection_options;
extern int	pgactive_init_node_parallel_jobs;
extern int	pgactive_max_nodes;
extern bool pgactive_permit_node_identifier_getter_function_creation;
extern bool pgactive_debug_trace_connection_errors;

static const char *const pgactive_default_apply_connection_options =
"connect_timeout=30 "
"keepalives=1 "
"keepalives_idle=20 "
"keepalives_interval=20 "
"keepalives_count=5 ";

/* size of the buffer allocated for error message. */
#define MAX_ERRORMSG_LEN 1000

/*
 * Header for the shared memory segment ref'd by the pgactiveWorkerCtl ptr,
 * containing pgactive_max_workers pgactiveWorkerControl entries.
 */
typedef struct pgactiveWorkerControl
{
	/* Must hold this lock when writing to pgactiveWorkerControl members */
	LWLockId	lock;
	/* Worker generation number, incremented on postmaster restart */
	uint16		worker_generation;
	/* Set/unset by pgactive_apply_pause()/_replay(). */
	bool		pause_apply;
	/* Is this the first startup of the supervisor? */
	bool		is_supervisor_restart;
	/* Pause worker management (used in testing) */
	bool		worker_management_paused;
	/* Latch for the supervisor worker */
	Latch	   *supervisor_latch;
	/* Buffer to hold error message */
	char		errormsg_buf[MAX_ERRORMSG_LEN + 1];
	/* Array members, of size pgactive_max_workers */
	pgactiveWorker slots[FLEXIBLE_ARRAY_MEMBER];
}			pgactiveWorkerControl;

extern pgactiveWorkerControl * pgactiveWorkerCtl;
extern pgactiveWorker * pgactive_worker_slot;

extern ResourceOwner pgactive_saved_resowner;

/* DDL executor/filtering support */
extern bool in_pgactive_replicate_ddl_command;

/* cached oids, setup by pgactive_maintain_schema() */
extern Oid	pgactiveSchemaOid;
extern Oid	pgactiveNodesRelid;
extern Oid	pgactiveConnectionsRelid;
extern Oid	QueuedDDLCommandsRelid;
extern Oid	pgactiveConflictHistoryRelId;
extern Oid	pgactiveReplicationSetConfigRelid;
extern Oid	pgactiveLocksRelid;
extern Oid	pgactiveLocksByOwnerRelid;
extern Oid	QueuedDropsRelid;
extern Oid	pgactiveSupervisorDbOid;

typedef struct pgactiveNodeInfo
{
	/* hash key */
	pgactiveNodeId id;

	/* is this entry valid */
	bool		valid;

	char	   *name;

	pgactiveNodeStatus status;

	char	   *local_dsn;
	char	   *init_from_dsn;

	bool		read_only;

	/* sequence ID if assigned or -1 if null in nodes table */
	int			seq_id;
}			pgactiveNodeInfo;

extern Oid	pgactive_lookup_relid(const char *relname, Oid schema_oid);

extern bool pgactive_in_extension;
extern int	pgactive_log_min_messages;

/* apply support */
extern void pgactive_fetch_sysid_via_node_id(RepOriginId node_id, pgactiveNodeId * out_nodeid);
extern bool pgactive_fetch_sysid_via_node_id_ifexists(RepOriginId node_id, pgactiveNodeId * out_nodeid, bool missing_ok);
extern RepOriginId pgactive_fetch_node_id_via_sysid(const pgactiveNodeId * const node);

/* Index maintenance, heap access, etc */
extern struct EState *pgactive_create_rel_estate(Relation rel, ResultRelInfo *resultRelInfo);
extern void UserTableUpdateIndexes(struct EState *estate,
								   struct TupleTableSlot *slot,
								   ResultRelInfo *relinfo);
extern void UserTableUpdateOpenIndexes(struct EState *estate,
									   struct TupleTableSlot *slot,
									   ResultRelInfo *relinfo, bool update);
extern void build_index_scan_keys(ResultRelInfo *relinfo,
								  struct ScanKeyData **scan_keys,
								  pgactiveTupleData * tup);
extern bool build_index_scan_key(struct ScanKeyData *skey, Relation rel,
								 Relation idxrel,
								 pgactiveTupleData * tup);
extern bool find_pkey_tuple(struct ScanKeyData *skey, pgactiveRelation * rel,
							Relation idxrel, struct TupleTableSlot *slot,
							bool lock, enum LockTupleMode mode);

/* conflict logging (usable in apply only) */

/*
 * Details of a conflict detected by an apply process, destined for logging
 * output and/or conflict triggers.
 *
 * Closely related to pgactive.pgactive_conflict_history SQL table.
 */
typedef struct pgactiveApplyConflict
{
	TransactionId local_conflict_txid;
	XLogRecPtr	local_conflict_lsn;
	TimestampTz local_conflict_time;
	const char *object_schema;	/* unused if apply_error */
	const char *object_name;	/* unused if apply_error */
	pgactiveNodeId remote_node;
	TransactionId remote_txid;
	TimestampTz remote_commit_time;
	XLogRecPtr	remote_commit_lsn;
	pgactiveConflictType conflict_type;
	pgactiveConflictResolution conflict_resolution;
	bool		local_tuple_null;
	Datum		local_tuple;	/* composite */
	TransactionId local_tuple_xmin;
	pgactiveNodeId local_tuple_origin_node; /* sysid 0 if unknown */
	TimestampTz local_commit_time;
	bool		remote_tuple_null;
	Datum		remote_tuple;	/* composite */
	ErrorData  *apply_error;
}			pgactiveApplyConflict;

typedef struct pgactiveNodeDSNsInfo
{
	char	   *node_dsn;
	char	   *node_name;
}			pgactiveNodeDSNsInfo;

extern void pgactive_conflict_logging_startup(void);
extern void pgactive_conflict_logging_cleanup(void);

extern pgactiveApplyConflict * pgactive_make_apply_conflict(pgactiveConflictType conflict_type,
															pgactiveConflictResolution resolution,
															TransactionId remote_txid,
															pgactiveRelation * conflict_relation,
															struct TupleTableSlot *local_tuple,
															RepOriginId local_tuple_origin_id,
															struct TupleTableSlot *remote_tuple,
															TimestampTz local_commit_ts,
															struct ErrorData *apply_error);

extern void pgactive_conflict_log_serverlog(pgactiveApplyConflict * conflict);
extern void pgactive_conflict_log_table(pgactiveApplyConflict * conflict);

extern void tuple_to_stringinfo(StringInfo s, TupleDesc tupdesc, HeapTuple tuple);

/* statistic functions */
extern void pgactive_count_shmem_init(int nnodes);
extern void pgactive_count_set_current_node(RepOriginId node_id);
extern void pgactive_count_commit(void);
extern void pgactive_count_rollback(void);
extern void pgactive_count_insert(void);
extern void pgactive_count_insert_conflict(void);
extern void pgactive_count_update(void);
extern void pgactive_count_update_conflict(void);
extern void pgactive_count_delete(void);
extern void pgactive_count_delete_conflict(void);
extern void pgactive_count_disconnect(void);

/* compat check functions */
extern bool pgactive_get_float4byval(void);
extern bool pgactive_get_float8byval(void);
extern bool pgactive_get_integer_timestamps(void);
extern bool pgactive_get_bigendian(void);

/* initialize a new pgactive member */
extern void pgactive_init_replica(pgactiveNodeInfo * local_node);

extern void pgactive_maintain_schema(bool update_extensions);

/* shared memory management */
extern void pgactive_shmem_init(void);

extern pgactiveWorker * pgactive_worker_shmem_alloc(pgactiveWorkerType worker_type,
													uint32 *ctl_idx);
extern void pgactive_worker_shmem_free(pgactiveWorker * worker,
									   BackgroundWorkerHandle *handle,
									   bool need_lock);
extern void pgactive_worker_shmem_acquire(pgactiveWorkerType worker_type,
										  uint32 worker_idx,
										  bool free_at_rel);
extern void pgactive_worker_shmem_release(void);

extern bool pgactive_is_pgactive_activated_db(Oid dboid);
extern pgactiveWorker * pgactive_worker_get_entry(const pgactiveNodeId * nodeid,
												  pgactiveWorkerType worker_type);

/* forbid commands we do not support currently (or never will) */
extern void init_pgactive_commandfilter(void);
extern void pgactive_commandfilter_always_allow_ddl(bool always_allow);

extern void pgactive_executor_init(void);
extern void pgactive_executor_always_allow_writes(bool always_allow);
extern void pgactive_queue_ddl_command(const char *command_tag, const char *command, const char *search_path);
extern void pgactive_execute_ddl_command(char *cmdstr, char *perpetrator, char *search_path, bool tx_just_started);
extern void pgactive_start_truncate(void);
extern void pgactive_finish_truncate(void);

extern void pgactive_capture_ddl(Node *parsetree, const char *queryString,
								 ProcessUtilityContext context, ParamListInfo params,
								 DestReceiver *dest, CommandTag completionTag);

extern void pgactive_locks_shmem_init(void);
extern void pgactive_locks_check_dml(void);

/* background workers and supporting functions for them */
PGDLLEXPORT extern void pgactive_apply_main(Datum main_arg);
PGDLLEXPORT extern void pgactive_perdb_worker_main(Datum main_arg);
PGDLLEXPORT extern void pgactive_supervisor_worker_main(Datum main_arg);

extern void pgactive_bgworker_init(uint32 worker_arg, pgactiveWorkerType worker_type);
extern void pgactive_supervisor_register(void);
extern bool IspgactiveApplyWorker(void);
extern bool IspgactivePerdbWorker(void);
extern pgactiveApplyWorker * GetpgactiveApplyWorkerShmemPtr(void);

extern Oid	pgactive_get_supervisordb_oid(bool missing_ok);

/* Postgres commit 7dbfea3c455e introduced SIGHUP handler in version 13. */
#if PG_VERSION_NUM < 130000
extern volatile sig_atomic_t ConfigReloadPending;
extern void SignalHandlerForConfigReload(SIGNAL_ARGS);
#endif

extern int	find_perdb_worker_slot(Oid dboid,
								   pgactiveWorker * *worker_found);

extern void pgactive_maintain_db_workers(void);

extern Datum pgactive_connections_changed(PG_FUNCTION_ARGS);

/* Information functions */
extern int	pgactive_parse_version(const char *pgactive_version_str, int *o_major,
								   int *o_minor, int *o_rev, int *o_subrev);

/* manipulation of pgactive catalogs */
extern pgactiveNodeStatus pgactive_nodes_get_local_status(const pgactiveNodeId * const node,
														  bool missing_ok);
extern pgactiveNodeInfo * pgactive_nodes_get_local_info(const pgactiveNodeId * const node);
extern void pgactive_pgactive_node_free(pgactiveNodeInfo * node);
extern void pgactive_nodes_set_local_status(pgactiveNodeStatus status, pgactiveNodeStatus oldstatus);
extern void pgactive_nodes_set_local_attrs(pgactiveNodeStatus status, pgactiveNodeStatus oldstatus, const int *seq_id);
extern List *pgactive_read_connection_configs(void);
extern List *pgactive_get_node_dsns(bool only_local_node);
extern int	pgactive_remote_node_seq_id(void);

/* return a node name or (none) if unknown for given nodeid */
extern const char *pgactive_nodeid_name(const pgactiveNodeId * const node,
										bool missing_ok);

extern void
			stringify_my_node_identity(char *sysid_str, Size sysid_str_size,
									   char *timeline_str, Size timeline_str_size,
									   char *dboid_str, Size dboid_str_size);

extern void
			stringify_node_identity(char *sysid_str, Size sysid_str_size,
									char *timeline_str, Size timeline_str_size,
									char *dboid_str, Size dboid_str_size,
									const pgactiveNodeId * const nodeid);

extern void
			pgactive_copytable(PGconn *copyfrom_conn, PGconn *copyto_conn,
							   const char *copyfrom_query, const char *copyto_query);

/* local node info cache (pgactive_nodecache.c) */
extern void pgactive_nodecache_invalidate(void);
extern bool pgactive_local_node_read_only(void);
extern char pgactive_local_node_status(void);
extern int32 pgactive_local_node_seq_id(void);
extern const char *pgactive_local_node_name(void);

extern void pgactive_set_node_read_only_guts(char *node_name, bool read_only, bool force);
extern void pgactive_setup_my_cached_node_names(void);
extern void pgactive_setup_cached_remote_name(const pgactiveNodeId * const remote_nodeid);
extern const char *pgactive_get_my_cached_node_name(void);
extern const char *pgactive_get_my_cached_remote_name(const pgactiveNodeId * const remote_nodeid);

/* helpers shared by multiple worker types */
extern struct pg_conn *pgactive_connect(const char *conninfo, Name appname,
										pgactiveNodeId * out_nodeid);

extern struct pg_conn *pgactive_establish_connection_and_slot(const char *dsn,
															  const char *application_name_suffix,
															  Name out_slot_name,
															  pgactiveNodeId * out_nodeid,
															  RepOriginId *out_rep_origin_id,
															  char *out_snapshot);

extern PGconn *pgactive_connect_nonrepl(const char *connstring,
										const char *appnamesuffix,
										bool report_fatal);

/* Helper for PG_ENSURE_ERROR_CLEANUP to close a PGconn */
extern void pgactive_cleanup_conn_close(int code, Datum offset);

/* use instead of table_open()/table_close() */
extern pgactiveRelation * pgactive_table_open(Oid reloid, LOCKMODE lockmode);
extern void pgactive_table_close(pgactiveRelation * rel, LOCKMODE lockmode);
extern void pgactive_heap_compute_replication_settings(
													   pgactiveRelation * rel,
													   int num_replication_sets,
													   char **replication_sets);
extern void pgactiveRelcacheHashInvalidateCallback(Datum arg, Oid relid);

extern void pgactive_parse_relation_options(const char *label, pgactiveRelation * rel);
extern void pgactive_parse_database_options(const char *label, bool *is_active);

/* conflict handlers API */
extern void pgactive_conflict_handlers_init(void);

extern HeapTuple pgactive_conflict_handlers_resolve(pgactiveRelation * rel,
													const HeapTuple local,
													const HeapTuple remote,
													const char *command_tag,
													pgactiveConflictType event_type,
													uint64 timeframe, bool *skip);

/* replication set stuff */
void		pgactive_validate_replication_set_name(const char *name, bool allow_implicit);

/* Helpers to probe remote nodes */

typedef struct remote_node_info
{
	pgactiveNodeId nodeid;
	char	   *sysid_str;
	char	   *variant;
	char	   *version;
	int			version_num;
	int			min_remote_version_num;
	bool		has_required_privs;
	char		node_status;
	char	   *node_name;
	char	   *dbname;
	int64		dbsize;			/* database size in bytes */
	/* total size of indexes present in database in bytes */
	int64		indexessize;
	int			max_nodes;
	bool		skip_ddl_replication;
	int			cur_nodes;

	/* collation related info */
	char	   *datcollate;
	char	   *datctype;
}			remote_node_info;

extern void pgactive_get_remote_nodeinfo_internal(PGconn *conn, remote_node_info * ri);

extern void free_remote_node_info(remote_node_info * ri);

extern void pgactive_ensure_ext_installed(PGconn *pgconn);

/*
 * Global to identify the type of pgactive worker the current process is. Primarily
 * useful for assertions and debugging.
 */
extern pgactiveWorkerType pgactive_worker_type;

extern void pgactive_make_my_nodeid(pgactiveNodeId * const node);
extern void pgactive_nodeid_cpy(pgactiveNodeId * const dest, const pgactiveNodeId * const src);
extern bool pgactive_nodeid_eq(const pgactiveNodeId * const left, const pgactiveNodeId * const right);
extern const char *pgactive_error_severity(int elevel);

/*
 * sequencer support
 */

/*
 * Protocol
 */
extern void pgactive_getmsg_nodeid(StringInfo message, pgactiveNodeId * const nodeid, bool expect_empty_nodename);
extern void pgactive_send_nodeid(StringInfo s, const pgactiveNodeId * const nodeid, bool include_empty_nodename);
extern void pgactive_sendint64(int64 i, char *buf);

/*
 * Postgres commit 9e98583898c3/a19e5cee635d introduced this function in
 * version 15.
 */
#if PG_VERSION_NUM < 150000
/* flag bits for InitMaterializedSRF() */
#define MAT_SRF_USE_EXPECTED_DESC	0x01	/* use expectedDesc as tupdesc. */
#define MAT_SRF_BLESS				0x02	/* "Bless" a tuple descriptor with
											 * BlessTupleDesc(). */
extern void InitMaterializedSRF(FunctionCallInfo fcinfo, bits32 flags);
#endif

/* Postgres commit 6f6f284c7ee4 introduced this macro in version 14. */
#if PG_VERSION_NUM < 140000
/*
 * Handy macro for printing XLogRecPtr in conventional format, e.g.,
 *
 * printf("%X/%X", LSN_FORMAT_ARGS(lsn));
 */
#define LSN_FORMAT_ARGS(lsn) (AssertVariableIsOfTypeMacro((lsn), XLogRecPtr), (uint32) ((lsn) >> 32)), ((uint32) (lsn))
#endif

/*
 * Shared memory structure for caching per-db pgactive node identifiers.
 */
typedef struct pgactiveNodeIdentifier
{
	Oid			dboid;
	uint64		nid;

	/*
	 * Sets --data-only option for dump while logical join of a node. When
	 * set, user must ensure node has all required schema before logically
	 * joining it to pgactive group.
	 */
	bool		data_only_node_init;
}			pgactiveNodeIdentifier;

typedef struct pgactiveNodeIdentifierControl
{
	/*
	 * Must hold this lock when writing to pgactiveNodeIdentifierControl
	 * members
	 */
	LWLockId	lock;
	pgactiveNodeIdentifier nids[FLEXIBLE_ARRAY_MEMBER];
}			pgactiveNodeIdentifierControl;

extern pgactiveNodeIdentifierControl * pgactiveNodeIdentifierCtl;

extern void pgactive_nid_shmem_init(void);
extern uint64 pgactive_get_nid_internal(void);
extern bool is_pgactive_creating_nid_getter_function(void);
extern Oid	find_pgactive_nid_getter_function(void);
extern bool is_pgactive_nid_getter_function_create(CreateFunctionStmt *stmt);
extern bool is_pgactive_nid_getter_function_drop(DropStmt *stmt);
extern bool is_pgactive_nid_getter_function_alter(AlterFunctionStmt *stmt);
extern bool is_pgactive_nid_getter_function_alter_owner(AlterOwnerStmt *stmt);
extern bool is_pgactive_nid_getter_function_alter_rename(RenameStmt *stmt);
extern void pgactive_set_data_only_node_init(Oid dboid, bool val);
extern bool pgactive_get_data_only_node_init(Oid dboid);

/* Postgres commit cfdf4dc4fc96 introduced this pseudo-event in version 12. */
#if PG_VERSION_NUM >= 120000
static inline int
pgactiveWaitLatch(Latch *latch, int wakeEvents, long timeout,
				  uint32 wait_event_info)
{
	return WaitLatch(latch, wakeEvents, timeout, wait_event_info);
}
static inline int
pgactiveWaitLatchOrSocket(Latch *latch, int wakeEvents, pgsocket sock,
						  long timeout, uint32 wait_event_info)
{
	return WaitLatchOrSocket(latch, wakeEvents, sock, timeout,
							 wait_event_info);
}
#else
#define WL_EXIT_ON_PM_DEATH	 (1 << 5)

static inline int
pgactiveWaitLatch(Latch *latch, int wakeEvents, long timeout,
				  uint32 wait_event_info)
{
	int			events;
	int			rc;

	events = wakeEvents;
	if (events & WL_EXIT_ON_PM_DEATH)
	{
		events &= ~WL_EXIT_ON_PM_DEATH;
		events |= WL_POSTMASTER_DEATH;
	}

	rc = WaitLatch(latch, events, timeout, wait_event_info);

	if ((wakeEvents & WL_EXIT_ON_PM_DEATH) &&
		(rc & WL_POSTMASTER_DEATH))
		proc_exit(1);

	return rc;
}

static inline int
pgactiveWaitLatchOrSocket(Latch *latch, int wakeEvents, pgsocket sock,
						  long timeout, uint32 wait_event_info)
{
	int			events;
	int			rc;

	events = wakeEvents;
	if (events & WL_EXIT_ON_PM_DEATH)
	{
		events &= ~WL_EXIT_ON_PM_DEATH;
		events |= WL_POSTMASTER_DEATH;
	}

	rc = WaitLatchOrSocket(latch, events, sock, timeout, wait_event_info);

	if ((wakeEvents & WL_EXIT_ON_PM_DEATH) &&
		(rc & WL_POSTMASTER_DEATH))
		proc_exit(1);

	return rc;
}
#endif

#define TEMP_DUMP_DIR_PREFIX "pgactive-dump"
extern void destroy_temp_dump_dirs(int code, Datum arg);
extern void destroy_temp_dump_dir(int code, Datum arg);

extern int	find_apply_worker_slot(const pgactiveNodeId * const remote,
								   pgactiveWorker * *worker_found);

/*
 * Emit a generic connection failure message based on GUC setting to help not
 * emit sensitive info like hostname/hostaddress, username, password etc. of
 * the connection string used for establishing connection. Note that this
 * function is supposed to be used for connection failures only i.e., for
 * PQstatus(conn) != CONNECTION_OK cases after PQconnectdb or its friends.
 */
static inline char *
GetPQerrorMessage(const PGconn *conn)
{
	Assert(PQstatus(conn) != CONNECTION_OK);

	if (pgactive_debug_trace_connection_errors)
		return PQerrorMessage(conn);
	else
		return "connection failed";
}

#endif							/* pgactive_H */
