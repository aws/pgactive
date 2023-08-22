/*
 * bdr.h
 *
 * BiDirectionalReplication
 *
 * Copyright (c) 2012-2015, PostgreSQL Global Development Group
 *
 * bdr.h
 */
#ifndef BDR_H
#define BDR_H

#include "miscadmin.h"
#include "access/xlogdefs.h"
#include "postmaster/bgworker.h"
#include "replication/logical.h"
#include "utils/resowner.h"
#include "storage/latch.h"
#include "storage/lock.h"
#include "tcop/utility.h"

#include "lib/ilist.h"

#include "libpq-fe.h"

#include "bdr_config.h"
#include "bdr_internal.h"
#include "bdr_version.h"
#include "bdr_compat.h"
#include "nodes/execnodes.h"

#define NODEID_BITS		10
#define MAX_NODE_ID		((1 << NODEID_BITS) - 1)

/* Right now replication_name isn't used; make it easily found for later */
#define EMPTY_REPLICATION_NAME ""

/*
 * BDR_NODEID_FORMAT is used in fallback_application_name. It's distinct from
 * BDR_NODE_ID_FORMAT in that it doesn't include the remote dboid as that may
 * not be known yet, just (sysid,tlid,dboid,replication_name) .
 *
 * Use BDR_LOCALID_FORMAT_ARGS to sub it in to format strings, or BDR_NODEID_FORMAT_ARGS(node)
 * to format a BDRNodeId.
 *
 * The WITHNAME variant does a cache lookup to add the node name. It's only safe where
 * the nodecache exists.
 */
#define BDR_NODEID_FORMAT "("UINT64_FORMAT",%u,%u,%s)"
#define BDR_NODEID_FORMAT_WITHNAME "%s ("UINT64_FORMAT",%u,%u,%s)"

#if PG_VERSION_NUM >= 150000
#define ThisTimeLineID	GetWALInsertionTimeLine()
#endif

#define BDR_LOCALID_FORMAT_ARGS \
	bdr_get_nid_internal(), BDRThisTimeLineID, MyDatabaseId, EMPTY_REPLICATION_NAME

/*
 * For use with BDR_NODEID_FORMAT_WITHNAME, print our node id tuple and name.
 * The node name used is stored in the bdr nodecache and is accessible outside
 * transaction scope when in a BDR bgworker. For a normal backend a syscache
 * lookup may be performed to find the node name if we're already in a
 * transaction, otherwise (none) is returned.
 */
#define BDR_LOCALID_FORMAT_WITHNAME_ARGS \
	bdr_get_my_cached_node_name(), BDR_LOCALID_FORMAT_ARGS

/*
 * print helpers for node IDs, for use with BDR_NODEID_FORMAT.
 *
 * MULTIPLE EVALUATION HAZARD.
 */
#define BDR_NODEID_FORMAT_ARGS(node) \
	(node).sysid, (node).timeline, (node).dboid, EMPTY_REPLICATION_NAME

/*
 * This argument set is for BDR_NODE_ID_FORMAT_WITHNAME, for use within an
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
#define BDR_NODEID_FORMAT_WITHNAME_ARGS(node) \
	bdr_get_my_cached_remote_name(&(node)), BDR_NODEID_FORMAT_ARGS(node)

#define BDR_LIBRARY_NAME "bdr"
#define BDR_RESTORE_CMD "pg_restore"
#define BDR_DUMP_CMD "bdr_dump"

#define BDR_SUPERVISOR_DBNAME "bdr_supervisordb"

#define BDR_LOGICAL_MSG_PREFIX "bdr"

#define BDR_SECLABEL_PROVIDER "bdr"

static const struct config_enum_entry bdr_message_level_options[] = {
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
extern shmem_request_hook_type bdr_prev_shmem_request_hook;
#endif

/*
 * Flags to indicate which fields are present in a begin record sent by the
 * output plugin.
 */
typedef enum BdrOutputBeginFlags
{
	BDR_OUTPUT_TRANSACTION_HAS_ORIGIN = 1
} BdrOutputBeginFlags;

/*
 * BDR conflict detection: type of conflict that was identified.
 *
 * Must correspond to bdr.bdr_conflict_type SQL enum and
 * bdr_conflict_type_get_datum (...)
 */
typedef enum BdrConflictType
{
	BdrConflictType_InsertInsert,
	BdrConflictType_InsertUpdate,
	BdrConflictType_UpdateUpdate,
	BdrConflictType_UpdateDelete,
	BdrConflictType_DeleteDelete,
	BdrConflictType_UnhandledTxAbort
}			BdrConflictType;

/*
 * BDR conflict detection: how the conflict was resolved (if it was).
 *
 * Must correspond to bdr.bdr_conflict_resolution SQL enum and
 * bdr_conflict_resolution_get_datum(...)
 */
typedef enum BdrConflictResolution
{
	BdrConflictResolution_ConflictTriggerSkipChange,
	BdrConflictResolution_ConflictTriggerReturnedTuple,
	BdrConflictResolution_LastUpdateWins_KeepLocal,
	BdrConflictResolution_LastUpdateWins_KeepRemote,
	BdrConflictResolution_DefaultApplyChange,
	BdrConflictResolution_DefaultSkipChange,
	BdrConflictResolution_UnhandledTxAbort
}			BdrConflictResolution;

typedef struct BDRConflictHandler
{
	Oid			handler_oid;
	BdrConflictType handler_type;
	uint64		timeframe;
}			BDRConflictHandler;

/* How detailed logging of DDL locks is */
enum BdrDDLLockTraceLevel
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
typedef struct BDRRelation
{
	/* hash key */
	Oid			reloid;

	bool		valid;

	Relation	rel;

	BDRConflictHandler *conflict_handlers;
	size_t		conflict_handlers_len;

	/* ordered list of replication sets of length num_* */
	char	  **replication_sets;
	/* -1 for no configured set */
	int			num_replication_sets;

	bool		computed_repl_valid;
	bool		computed_repl_insert;
	bool		computed_repl_update;
	bool		computed_repl_delete;
}			BDRRelation;

typedef struct BDRTupleData
{
	Datum		values[MaxTupleAttributeNumber];
	bool		isnull[MaxTupleAttributeNumber];
	bool		changed[MaxTupleAttributeNumber];
}			BDRTupleData;

/*
 * BdrApplyWorker describes a BDR worker connection.
 *
 * This struct is stored in an array in shared memory, so it can't have any
 * pointers.
 */
typedef struct BdrApplyWorker
{
	/* oid of the database this worker is applying changes to */
	Oid			dboid;

	/* assigned perdb worker slot */
	struct BdrWorker *perdb;

	/*
	 * Identification for the remote db we're connecting to; used to find the
	 * appropriate bdr.connections row, etc.
	 */
	BDRNodeId	remote_node;

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
	 * Must only be accessed with the bdr worker shmem control segment lock
	 * held.
	 */
	Latch	   *proclatch;
}			BdrApplyWorker;

/*
 * BDRPerdbCon describes a per-database worker, a static bgworker that manages
 * BDR for a given DB.
 */
typedef struct BdrPerdbWorker
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
	 * Must only be accessed with the bdr worker shmem control segment lock
	 * held.
	 */
	Latch	   *proclatch;

	/* Oid of the database the worker is attached to - populated after start */
	Oid			p_dboid;
}			BdrPerdbWorker;

/*
 * Walsender worker. These are only allocated while a output plugin is active.
 */
typedef struct BdrWalsenderWorker
{
	struct WalSnd *walsender;
	struct ReplicationSlot *slot;

	/* Identification for the remote the connection comes from. */
	BDRNodeId	remote_node;

}			BdrWalsenderWorker;

/*
 * Type of BDR worker in a BdrWorker struct
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
	BDR_WORKER_EMPTY_SLOT = 0,
	/* This shm array slot contains data for a BdrApplyWorker */
	BDR_WORKER_APPLY,
	/* This is data for a per-database worker BdrPerdbWorker */
	BDR_WORKER_PERDB,
	/* This is data for a walsenders currently streaming data out */
	BDR_WORKER_WALSENDER
}			BdrWorkerType;

/*
 * BDRWorker entries describe shared memory slots that keep track of
 * all BDR worker types. A slot may contain data for a number of different
 * kinds of worker; this union makes sure each slot is the same size and
 * is easily accessed via an array.
 */
typedef struct BdrWorker
{
	/* Type of worker. Also used to determine if this shm slot is free. */
	BdrWorkerType worker_type;

	/* pid worker if running, or 0 */
	pid_t		worker_pid;

	/* proc entry of worker if running, or NULL */
	PGPROC	   *worker_proc;

	union data
	{
		BdrApplyWorker apply;
		BdrPerdbWorker perdb;
		BdrWalsenderWorker walsnd;
	}			data;

}			BdrWorker;

/*
 * Attribute numbers for bdr.bdr_nodes and bdr.bdr_connections
 *
 * This must only ever be appended to, since modifications that change attnos
 * will break upgrades. It must match the column attnos reported by the regression
 * tests in results/schema.out .
 */
typedef enum BdrNodesAttno
{
	BDR_NODES_ATT_SYSID = 1,
	BDR_NODES_ATT_TIMELINE = 2,
	BDR_NODES_ATT_DBOID = 3,
	BDR_NODES_ATT_STATUS = 4,
	BDR_NODES_ATT_NAME = 5,
	BDR_NODES_ATT_LOCAL_DSN = 6,
	BDR_NODES_ATT_INIT_FROM_DSN = 7,
	BDR_NODES_ATT_READ_ONLY = 8,
	BDR_NODES_ATT_SEQ_ID = 9
} BdrNodesAttno;

typedef enum BdrConnectionsAttno
{
	BDR_CONN_ATT_SYSID = 1,
	BDR_CONN_ATT_TIMELINE = 2,
	BDR_CONN_ATT_DBOID = 3,
	BDR_CONN_ATT_ORIGIN_SYSID = 4,
	BDR_CONN_ATT_ORIGIN_TIMELINE = 5,
	BDR_CONN_ATT_ORIGIN_DBOID = 6,
	BDR_CONN_ATT_IS_UNIDIRECTIONAL = 7,
	BDR_CONN_DSN = 8,
	BDR_CONN_APPLY_DELAY = 9,
	BDR_CONN_REPLICATION_SETS = 10
} BdrConnectionsAttno;

typedef struct BdrFlushPosition
{
	dlist_node	node;
	XLogRecPtr	local_end;
	XLogRecPtr	remote_end;
}			BdrFlushPosition;

/* GUCs */
extern int	bdr_default_apply_delay;
extern int	bdr_max_workers;
extern int	bdr_max_databases;
extern char *bdr_temp_dump_directory;
extern bool bdr_log_conflicts_to_table;
extern bool bdr_conflict_logging_include_tuples;

/* replaced by bdr_skip_ddl_replication for now
extern bool bdr_permit_ddl_locking;
extern bool bdr_permit_unsafe_commands;
extern bool bdr_skip_ddl_locking;
*/
extern bool bdr_skip_ddl_replication;
extern bool prev_bdr_skip_ddl_replication;
extern bool bdr_do_not_replicate;
extern bool bdr_discard_mismatched_row_attributes;
extern int	bdr_max_ddl_lock_delay;
extern int	bdr_ddl_lock_timeout;
#ifdef USE_ASSERT_CHECKING
extern int	bdr_ddl_lock_acquire_timeout;
#endif
extern bool bdr_trace_replay;
extern int	bdr_trace_ddl_locks_level;
extern char *bdr_extra_apply_connection_options;
extern int	bdr_init_node_parallel_jobs;
extern int	bdr_max_nodes;
extern bool bdr_permit_node_identifier_getter_function_creation;

static const char *const bdr_default_apply_connection_options =
"connect_timeout=30 "
"keepalives=1 "
"keepalives_idle=20 "
"keepalives_interval=20 "
"keepalives_count=5 ";

/*
 * Header for the shared memory segment ref'd by the BdrWorkerCtl ptr,
 * containing bdr_max_workers BdrWorkerControl entries.
 */
typedef struct BdrWorkerControl
{
	/* Must hold this lock when writing to BdrWorkerControl members */
	LWLockId	lock;
	/* Worker generation number, incremented on postmaster restart */
	uint16		worker_generation;
	/* Set/unset by bdr_apply_pause()/_replay(). */
	bool		pause_apply;
	/* Is this the first startup of the supervisor? */
	bool		is_supervisor_restart;
	/* Pause worker management (used in testing) */
	bool		worker_management_paused;
	/* Is local node restoring dump of remote node? */
	bool		in_init_exec_dump_restore;
	/* Latch for the supervisor worker */
	Latch	   *supervisor_latch;
	/* Array members, of size bdr_max_workers */
	BdrWorker	slots[FLEXIBLE_ARRAY_MEMBER];
}			BdrWorkerControl;

extern BdrWorkerControl * BdrWorkerCtl;
extern BdrWorker * bdr_worker_slot;

extern ResourceOwner bdr_saved_resowner;

/* DDL executor/filtering support */
extern bool in_bdr_replicate_ddl_command;

/* cached oids, setup by bdr_maintain_schema() */
extern Oid	BdrSchemaOid;
extern Oid	BdrNodesRelid;
extern Oid	BdrConnectionsRelid;
extern Oid	QueuedDDLCommandsRelid;
extern Oid	BdrConflictHistoryRelId;
extern Oid	BdrReplicationSetConfigRelid;
extern Oid	BdrLocksRelid;
extern Oid	BdrLocksByOwnerRelid;
extern Oid	QueuedDropsRelid;
extern Oid	BdrSupervisorDbOid;

typedef struct BDRNodeInfo
{
	/* hash key */
	BDRNodeId	id;

	/* is this entry valid */
	bool		valid;

	char	   *name;

	BdrNodeStatus status;

	char	   *local_dsn;
	char	   *init_from_dsn;

	bool		read_only;

	/* sequence ID if assigned or -1 if null in nodes table */
	int			seq_id;
}			BDRNodeInfo;

extern Oid	bdr_lookup_relid(const char *relname, Oid schema_oid);

extern bool bdr_in_extension;
extern int	bdr_log_min_messages;

/* apply support */
extern void bdr_fetch_sysid_via_node_id(RepOriginId node_id, BDRNodeId * out_nodeid);
extern bool bdr_fetch_sysid_via_node_id_ifexists(RepOriginId node_id, BDRNodeId * out_nodeid, bool missing_ok);
extern RepOriginId bdr_fetch_node_id_via_sysid(const BDRNodeId * const node);

/* Index maintenance, heap access, etc */
extern struct EState *bdr_create_rel_estate(Relation rel, ResultRelInfo *resultRelInfo);
extern void UserTableUpdateIndexes(struct EState *estate,
								   struct TupleTableSlot *slot,
								   ResultRelInfo *relinfo);
extern void UserTableUpdateOpenIndexes(struct EState *estate,
									   struct TupleTableSlot *slot,
									   ResultRelInfo *relinfo, bool update);
extern void build_index_scan_keys(ResultRelInfo *relinfo,
								  struct ScanKeyData **scan_keys,
								  BDRTupleData * tup);
extern bool build_index_scan_key(struct ScanKeyData *skey, Relation rel,
								 Relation idxrel,
								 BDRTupleData * tup);
extern bool find_pkey_tuple(struct ScanKeyData *skey, BDRRelation * rel,
							Relation idxrel, struct TupleTableSlot *slot,
							bool lock, enum LockTupleMode mode);

/* conflict logging (usable in apply only) */

/*
 * Details of a conflict detected by an apply process, destined for logging
 * output and/or conflict triggers.
 *
 * Closely related to bdr.bdr_conflict_history SQL table.
 */
typedef struct BdrApplyConflict
{
	TransactionId local_conflict_txid;
	XLogRecPtr	local_conflict_lsn;
	TimestampTz local_conflict_time;
	const char *object_schema;	/* unused if apply_error */
	const char *object_name;	/* unused if apply_error */
	BDRNodeId	remote_node;
	TransactionId remote_txid;
	TimestampTz remote_commit_time;
	XLogRecPtr	remote_commit_lsn;
	BdrConflictType conflict_type;
	BdrConflictResolution conflict_resolution;
	bool		local_tuple_null;
	Datum		local_tuple;	/* composite */
	TransactionId local_tuple_xmin;
	BDRNodeId	local_tuple_origin_node;	/* sysid 0 if unknown */
	TimestampTz local_commit_time;
	bool		remote_tuple_null;
	Datum		remote_tuple;	/* composite */
	ErrorData  *apply_error;
}			BdrApplyConflict;

extern void bdr_conflict_logging_startup(void);
extern void bdr_conflict_logging_cleanup(void);

extern BdrApplyConflict * bdr_make_apply_conflict(BdrConflictType conflict_type,
												  BdrConflictResolution resolution,
												  TransactionId remote_txid,
												  BDRRelation * conflict_relation,
												  struct TupleTableSlot *local_tuple,
												  RepOriginId local_tuple_origin_id,
												  struct TupleTableSlot *remote_tuple,
												  TimestampTz local_commit_ts,
												  struct ErrorData *apply_error);

extern void bdr_conflict_log_serverlog(BdrApplyConflict * conflict);
extern void bdr_conflict_log_table(BdrApplyConflict * conflict);

extern void tuple_to_stringinfo(StringInfo s, TupleDesc tupdesc, HeapTuple tuple);

/* statistic functions */
extern void bdr_count_shmem_init(int nnodes);
extern void bdr_count_set_current_node(RepOriginId node_id);
extern void bdr_count_commit(void);
extern void bdr_count_rollback(void);
extern void bdr_count_insert(void);
extern void bdr_count_insert_conflict(void);
extern void bdr_count_update(void);
extern void bdr_count_update_conflict(void);
extern void bdr_count_delete(void);
extern void bdr_count_delete_conflict(void);
extern void bdr_count_disconnect(void);

/* compat check functions */
extern bool bdr_get_float4byval(void);
extern bool bdr_get_float8byval(void);
extern bool bdr_get_integer_timestamps(void);
extern bool bdr_get_bigendian(void);

/* initialize a new bdr member */
extern void bdr_init_replica(BDRNodeInfo * local_node);

extern void bdr_maintain_schema(bool update_extensions);

/* shared memory management */
extern void bdr_shmem_init(void);

extern BdrWorker * bdr_worker_shmem_alloc(BdrWorkerType worker_type,
										  uint32 *ctl_idx);
extern void bdr_worker_shmem_free(BdrWorker * worker, BackgroundWorkerHandle *handle);
extern void bdr_worker_shmem_acquire(BdrWorkerType worker_type,
									 uint32 worker_idx,
									 bool free_at_rel);
extern void bdr_worker_shmem_release(void);

extern bool bdr_is_bdr_activated_db(Oid dboid);
extern BdrWorker * bdr_worker_get_entry(const BDRNodeId * nodeid,
										BdrWorkerType worker_type);

/* forbid commands we do not support currently (or never will) */
extern void init_bdr_commandfilter(void);
extern void bdr_commandfilter_always_allow_ddl(bool always_allow);

extern void bdr_executor_init(void);
extern void bdr_executor_always_allow_writes(bool always_allow);
extern void bdr_queue_ddl_command(const char *command_tag, const char *command, const char *search_path);
extern void bdr_execute_ddl_command(char *cmdstr, char *perpetrator, char *search_path, bool tx_just_started);
extern void bdr_start_truncate(void);
extern void bdr_finish_truncate(void);

extern void bdr_capture_ddl(Node *parsetree, const char *queryString,
							ProcessUtilityContext context, ParamListInfo params,
							DestReceiver *dest, CommandTag completionTag);

extern void bdr_locks_shmem_init(void);
extern void bdr_locks_check_dml(void);

/* background workers and supporting functions for them */
PGDLLEXPORT extern void bdr_apply_main(Datum main_arg);
PGDLLEXPORT extern void bdr_perdb_worker_main(Datum main_arg);
PGDLLEXPORT extern void bdr_supervisor_worker_main(Datum main_arg);

extern void bdr_bgworker_init(uint32 worker_arg, BdrWorkerType worker_type);
extern void bdr_supervisor_register(void);
extern bool IsBdrApplyWorker(void);
extern bool IsBdrPerdbWorker(void);
extern BdrApplyWorker * GetBdrApplyWorkerShmemPtr(void);

extern Oid	bdr_get_supervisordb_oid(bool missing_ok);

extern void bdr_sighup(SIGNAL_ARGS);
extern void bdr_sigterm(SIGNAL_ARGS);

extern int	find_perdb_worker_slot(Oid dboid,
								   BdrWorker * *worker_found);

extern void bdr_maintain_db_workers(void);

extern Datum bdr_connections_changed(PG_FUNCTION_ARGS);

/* Information functions */
extern int	bdr_parse_version(const char *bdr_version_str, int *o_major,
							  int *o_minor, int *o_rev, int *o_subrev);

/* manipulation of bdr catalogs */
extern BdrNodeStatus bdr_nodes_get_local_status(const BDRNodeId * const node,
												bool missing_ok);
extern BDRNodeInfo * bdr_nodes_get_local_info(const BDRNodeId * const node);
extern void bdr_bdr_node_free(BDRNodeInfo * node);
extern void bdr_nodes_set_local_status(BdrNodeStatus status, BdrNodeStatus oldstatus);
extern void bdr_nodes_set_local_attrs(BdrNodeStatus status, BdrNodeStatus oldstatus, const int *seq_id);
extern List *bdr_read_connection_configs(void);
extern int	bdr_remote_node_seq_id(void);

/* return a node name or (none) if unknown for given nodeid */
extern const char *bdr_nodeid_name(const BDRNodeId * const node,
								   bool missing_ok,
								   bool only_cache_lookup);

extern void
			stringify_my_node_identity(char *sysid_str, Size sysid_str_size,
									   char *timeline_str, Size timeline_str_size,
									   char *dboid_str, Size dboid_str_size);

extern void
			stringify_node_identity(char *sysid_str, Size sysid_str_size,
									char *timeline_str, Size timeline_str_size,
									char *dboid_str, Size dboid_str_size,
									const BDRNodeId * const nodeid);

extern void
			bdr_copytable(PGconn *copyfrom_conn, PGconn *copyto_conn,
						  const char *copyfrom_query, const char *copyto_query);

/* local node info cache (bdr_nodecache.c) */
extern void bdr_nodecache_invalidate(void);
extern bool bdr_local_node_read_only(void);
extern char bdr_local_node_status(void);
extern int32 bdr_local_node_seq_id(void);
extern const char *bdr_local_node_name(bool only_cache_lookup);

extern void bdr_set_node_read_only_guts(char *node_name, bool read_only, bool force);
extern void bdr_setup_my_cached_node_names(void);
extern void bdr_setup_cached_remote_name(const BDRNodeId * const remote_nodeid);
extern const char *bdr_get_my_cached_node_name(void);
extern const char *bdr_get_my_cached_remote_name(const BDRNodeId * const remote_nodeid);

/* helpers shared by multiple worker types */
extern struct pg_conn *bdr_connect(const char *conninfo, Name appname,
								   BDRNodeId * out_nodeid);

extern struct pg_conn *bdr_establish_connection_and_slot(const char *dsn,
														 const char *application_name_suffix,
														 Name out_slot_name,
														 BDRNodeId * out_nodeid,
														 RepOriginId *out_replication_identifier,
														 char **out_snapshot);

extern PGconn *bdr_connect_nonrepl(const char *connstring,
								   const char *appnamesuffix);

/* Helper for PG_ENSURE_ERROR_CLEANUP to close a PGconn */
extern void bdr_cleanup_conn_close(int code, Datum offset);

/* use instead of table_open()/table_close() */
extern BDRRelation * bdr_table_open(Oid reloid, LOCKMODE lockmode);
extern void bdr_table_close(BDRRelation * rel, LOCKMODE lockmode);
extern void bdr_heap_compute_replication_settings(
												  BDRRelation * rel,
												  int num_replication_sets,
												  char **replication_sets);
extern void BDRRelcacheHashInvalidateCallback(Datum arg, Oid relid);

extern void bdr_parse_relation_options(const char *label, BDRRelation * rel);
extern void bdr_parse_database_options(const char *label, bool *is_active);

/* conflict handlers API */
extern void bdr_conflict_handlers_init(void);

extern HeapTuple bdr_conflict_handlers_resolve(BDRRelation * rel,
											   const HeapTuple local,
											   const HeapTuple remote,
											   const char *command_tag,
											   BdrConflictType event_type,
											   uint64 timeframe, bool *skip);

/* replication set stuff */
void		bdr_validate_replication_set_name(const char *name, bool allow_implicit);

/* Helpers to probe remote nodes */

typedef struct remote_node_info
{
	BDRNodeId	nodeid;
	char	   *sysid_str;
	char	   *variant;
	char	   *version;
	int			version_num;
	int			min_remote_version_num;
	bool		is_superuser;
	char		node_status;
	char	   *node_name;
	char	   *dbname;
	int64		dbsize;			/* database size in bytes */
	int			max_nodes;
	int			cur_nodes;

	/* collation related info */
	char	   *datcollate;
	char	   *datctype;
}			remote_node_info;

extern void bdr_get_remote_nodeinfo_internal(PGconn *conn, remote_node_info * ri);

extern void free_remote_node_info(remote_node_info * ri);

extern void bdr_ensure_ext_installed(PGconn *pgconn);

extern void bdr_test_remote_connectback_internal(PGconn *conn,
												 struct remote_node_info *ri, const char *my_dsn);

/*
 * Global to identify the type of BDR worker the current process is. Primarily
 * useful for assertions and debugging.
 */
extern BdrWorkerType bdr_worker_type;

extern void bdr_make_my_nodeid(BDRNodeId * const node);
extern void bdr_nodeid_cpy(BDRNodeId * const dest, const BDRNodeId * const src);
extern bool bdr_nodeid_eq(const BDRNodeId * const left, const BDRNodeId * const right);
extern const char *bdr_error_severity(int elevel);

/*
 * sequencer support
 */

/*
 * Protocol
 */
extern void bdr_getmsg_nodeid(StringInfo message, BDRNodeId * const nodeid, bool expect_empty_nodename);
extern void bdr_send_nodeid(StringInfo s, const BDRNodeId * const nodeid, bool include_empty_nodename);
extern void bdr_sendint64(int64 i, char *buf);

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
 * Shared memory structure for caching per-db BDR node identifiers.
 */
typedef struct BdrNodeIdentifier
{
	Oid			dboid;
	uint64		nid;
}			BdrNodeIdentifier;

typedef struct BdrNodeIdentifierControl
{
	/* Must hold this lock when writing to BdrNodeIdentifierControl members */
	LWLockId	lock;
	BdrNodeIdentifier nids[FLEXIBLE_ARRAY_MEMBER];
}			BdrNodeIdentifierControl;

extern BdrNodeIdentifierControl * BdrNodeIdentifierCtl;

extern void bdr_nid_shmem_init(void);
extern uint64 bdr_get_nid_internal(void);
extern bool is_bdr_creating_nid_getter_function(void);
extern Oid	find_bdr_nid_getter_function(void);
extern bool is_bdr_nid_getter_function_create(CreateFunctionStmt *stmt);
extern bool is_bdr_nid_getter_function_drop(DropStmt *stmt);
extern bool is_bdr_nid_getter_function_alter(AlterFunctionStmt *stmt);
extern bool is_bdr_nid_getter_function_alter_owner(AlterOwnerStmt *stmt);
extern bool is_bdr_nid_getter_function_alter_rename(RenameStmt *stmt);

#endif							/* BDR_H */
