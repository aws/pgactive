/* -------------------------------------------------------------------------
 *
 * bdr_locks.c
 *		global ddl/dml interlocking locks
 *
 *
 * Copyright (C) 2014-2015, PostgreSQL Global Development Group
 *
 * NOTES
 *
 *    A relatively simple distributed DDL locking implementation:
 *
 *    Locks are acquired on a database granularity and can only be held by a
 *    single node. That choice was made to reduce both, the complexity of the
 *    implementation, and to reduce the likelihood of inter node deadlocks.
 *
 *    Because DDL locks have to acquired inside transactions the inter-node
 *    communication can't be done via a queue table streamed out via logical
 *    decoding - other nodes would only see the result once the the
 *    transaction commits. We don't have autonomous tx's or suspendable tx's
 *    so we can't do a tx while another is in progress. Instead the 'messaging'
 *    feature is used which allows to inject transactional and nontransactional
 *    messages in the changestream. (We could instead make an IPC request to
 *    another worker to do the required transaction, but since we have
 *    non-transactional messaging it's simpler to use it).
 *
 *    There are really two levels of DDL lock - the global lock that only
 *    one node can hold, and individual local DDL locks on each node. If
 *    a node holds the global DDL lock then it owns the local DDL locks on each
 *    node.
 *
 *    A global 'ddl' lock may be upgraded to the stronger 'write' lock. This
 *    carries no deadlock hazard because the weakest lock mode is still an
 *    exclusive lock.
 *
 *    Note that DDL locking in 'write' mode flushes the queues of all edges in
 *    the node graph, not just those between the acquiring node and its peers.
 *    If node A requests the lock, then it must have fully replayed from B and
 *    C and vice versa. But B and C must also have fully replayed each others'
 *    outstanding replication queues. This ensures that no row changes that
 *    might conflict with the DDL can be in flight anywhere.
 *
 *
 *    DDL lock acquisition basically works like this:
 *
 *    1) A utility command notices that it needs the global ddl lock and the local
 *       node doesn't already hold it. If there already is a local ddl lock
 *       it'll ERROR out, as this indicates another node already holds or is
 *       trying to acquire the global DDL lock.
 *
 *       (We could wait, but would need to internally release, back off, and
 *       retry, and we'd likely land up getting cancelled anyway so it's not
 *       worth it.)
 *
 *    2) It sends out a 'acquire_lock' message to all other nodes and sets
 *    	 local state BDR_LOCKSTATE_ACQUIRE_TALLYING_CONFIRMATIONS
 *
 *
 *    Now, on each other node:
 *
 *    3) When another node receives a 'acquire_lock' message it checks whether
 *       the local ddl lock is already held. If so it'll send a 'decline_lock'
 *       message back causing the lock acquiration to fail.
 *
 *    4) If a 'acquire_lock' message is received and the local DDL lock is not
 *       held it'll be acquired and an entry into the 'bdr_global_locks' table
 *       will be made marking the lock to be in the 'catchup' phase. Set lock
 *       state BDR_LOCKSTATE_PEER_BEGIN_CATCHUP.
 *
 *    For 'write' mode locks:
 *
 *    5a) All concurrent user transactions will be cancelled (after a grace
 *        period, for 'write' mode locks only).
 *        State BDR_LOCKSTATE_PEER_CANCEL_XACTS.
 *
 *    5b) A 'request_replay_confirm' message will be sent to all other nodes
 *        containing a lsn that has to be replayed.
 *        State BDR_LOCKSTATE_PEER_CATCHUP
 *
 *    5c) When a 'request_replay_confirm' message is received, a
 *        'replay_confirm' message will be sent back.
 *
 *    5d) Once all other nodes have replied with 'replay_confirm' the local DDL lock
 *        has been successfully acquired on the node reading the 'acquire_lock'
 *        message (from 3)).
 *
 *    or for 'ddl' mode locks:
 *
 *    5) The replay confirmation process is skipped.
 *
 *    then for both 'ddl' and 'write' mode locks:
 *
 *	  6) The local bdr_global_locks entry will be updated to the 'acquired'
 *	     state and a 'confirm_lock' message will be sent out indicating that
 *	     the local ddl lock is fully acquired. Set lockstate
 *	     BDR_LOCKSTATE_PEER_CONFIRMED.
 *
 *
 *    On the node requesting the global lock
 *    (state still BDR_LOCKSTATE_ACQUIRE_TALLYING_CONFIRMATIONS):
 *
 *    9) Apply workers receive confirm_lock and decline_lock messages and tally
 *       them in the local DB's BdrLocksDBState in shared memory.
 *
 *    In the user backend that tried to get the lock:
 *
 *    10a) Once all nodes have replied with 'confirm_lock' messages the global
 *         ddl lock has been acquired. Set lock state
 *         BDR_LOCKSTATE_ACQUIRE_ACQUIRED.  Wait for the xact to commit or
 *         abort.
 *
 *      or
 *
 *    10b) If any 'decline_lock' message is received, the global lock acquisition
 *        has failed. Abort the acquiring transaction.
 *
 *    11) Send a release_lock message. Set lock state BDR_LOCKSTATE_NOLOCK
 *
 *
 *    on all peers:
 *
 *    12) When 'release_lock' is received, release local DDL lock and remove
 *        entry from global locks table. Ignore if not acquired. Set lock state
 *        BDR_LOCKSTATE_NOLOCK.
 *
 *
 *    There's some additional complications to handle crash safety:
 *
 *    Everytime a node starts up (after crash or clean shutdown) it sends out a
 *    'startup' message causing all other nodes to release locks held by it
 *    before shutdown/crash. Then the bdr_global_locks table is read. All
 *    existing local DDL locks held on behalf of other peers are acquired. If a
 *    lock still is in 'catchup' phase the local lock acquiration process is
 *    re-started at step 6)
 *
 *    Because only one decline is sufficient to stop a DDL lock acquisition,
 *    it's likely that two concurrent attempts to acquire the DDL lock from
 *    different nodes will both fail as each declines the other's request, or
 *    one or more of their peers acquire locks in different orders. Apps that
 *    do DDL from multiple nodes must be prepared to retry DDL.
 *
 *    DDL locks are transaction-level but do not respect subtransactions.
 *    They are not released if a subtransaction rolls back.
 *    (2ndQuadrant/bdr-private#77).
 *
 * IDENTIFICATION
 *		bdr_locks.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "bdr.h"

#include "bdr_locks.h"
#include "bdr_messaging.h"

#include "fmgr.h"
#include "funcapi.h"

#include "miscadmin.h"

#include "access/xact.h"
#include "access/xlog.h"

#include "commands/dbcommands.h"
#include "catalog/indexing.h"

#include "executor/executor.h"

#include "libpq/pqformat.h"

#include "replication/message.h"
#include "replication/origin.h"
#include "replication/slot.h"

#include "storage/barrier.h"
#include "storage/ipc.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/procarray.h"
#include "storage/shmem.h"
#include "storage/sinvaladt.h"

#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/memutils.h"
#include "utils/pg_lsn.h"
#include "utils/rel.h"
#include "utils/guc.h"
#include "utils/snapmgr.h"
#include "pgstat.h"

#define LOCKTRACE "DDL LOCK TRACE: "

extern Datum bdr_get_global_locks_info(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(bdr_get_global_locks_info);

/* GUCs */
/* replaced by !bdr_skip_ddl_replication for now
bool           bdr_permit_ddl_locking = false; */

/* -1 means use max_standby_streaming_delay */
int			bdr_max_ddl_lock_delay = -1;

/* -1 means use lock_timeout/statement_timeout */
int			bdr_ddl_lock_timeout = -1;

typedef enum BDRLockState
{
	BDR_LOCKSTATE_NOLOCK,

	/* States on acquiring node */
	BDR_LOCKSTATE_ACQUIRE_TALLY_CONFIRMATIONS,
	BDR_LOCKSTATE_ACQUIRE_ACQUIRED,

	/* States on peer nodes */
	BDR_LOCKSTATE_PEER_BEGIN_CATCHUP,
	BDR_LOCKSTATE_PEER_CANCEL_XACTS,
	BDR_LOCKSTATE_PEER_CATCHUP,
	BDR_LOCKSTATE_PEER_CONFIRMED

}			BDRLockState;

typedef struct BDRLockWaiter
{
	PGPROC	   *proc;
	slist_node	node;
}			BDRLockWaiter;

typedef struct BdrLocksDBState
{
	/* db slot used */
	bool		in_use;

	/* db this slot is reserved for */
	Oid			dboid;

	/* number of nodes we're connected to */
	int			nnodes;

	/* has startup progressed far enough to allow writes? */
	bool		locked_and_loaded;

	/*
	 * despite the use of a lock counter, currently only one lock may exist at
	 * a time.
	 */
	int			lockcount;

	/*
	 * If the lock is held by a peer, the node ID of the peer.
	 * InvalidRepOriginId represents the local node, like usual. Lock may be
	 * in the process of being acquired not fully held.
	 */
	RepOriginId lock_holder;

	/* pid of lock holder if it's a backend of on local node */
	int			lock_holder_local_pid;

	/* Type of lock held or being acquired */
	BDRLockType lock_type;

	/*
	 * Progress of lock acquisition. We need this so that if we set lock_type
	 * then rollback a subxact, or if we start a lock upgrade, we know we're
	 * not in fully acquired state.
	 */
	BDRLockState lock_state;

	/* progress of lock acquiration */
	int			acquire_confirmed;
	int			acquire_declined;

	/* progress of replay confirmation */
	int			replay_confirmed;
	XLogRecPtr	replay_confirmed_lsn;

	Latch	   *requestor;
	slist_head	waiters;		/* list of waiting PGPROCs */
}			BdrLocksDBState;

typedef struct BdrLocksCtl
{
	LWLockId	lock;
	BdrLocksDBState *dbstate;
	BDRLockWaiter *waiters;
}			BdrLocksCtl;

typedef struct BDRLockXactCallbackInfo
{
	/* Lock state to apply at commit time */
	BDRLockState commit_pending_lock_state;
	bool		pending;
}			BDRLockXactCallbackInfo;

static BDRLockXactCallbackInfo bdr_lock_state_xact_callback_info
=
{
	BDR_LOCKSTATE_NOLOCK, false
};

static void bdr_lock_holder_xact_callback(XactEvent event, void *arg);
static void bdr_lock_state_xact_callback(XactEvent event, void *arg);

static BdrLocksDBState * bdr_locks_find_database(Oid dbid, bool create);
static void bdr_locks_find_my_database(bool create);

static char *bdr_lock_state_to_name(BDRLockState lock_state);

static void bdr_request_replay_confirmation(void);
static void bdr_send_confirm_lock(void);

static void bdr_locks_addwaiter(PGPROC *proc);
static void bdr_locks_on_unlock(void);
static int	ddl_lock_log_level(int);
static void register_holder_xact_callback(void);
static void register_state_xact_callback(void);
static void bdr_locks_release_local_ddl_lock(const BDRNodeId * const lock);

static BdrLocksCtl * bdr_locks_ctl;

/* shmem init hook to chain to on startup, if any */
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

/* this database's state */
static BdrLocksDBState * bdr_my_locks_database = NULL;

static bool this_xact_acquired_lock = false;


/* SQL function to explcitly acquire global DDL lock */
PGDLLIMPORT extern Datum bdr_acquire_global_lock(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(bdr_acquire_global_lock);


static size_t
bdr_locks_shmem_size(void)
{
	Size		size = 0;
	uint32		TotalProcs = MaxBackends + NUM_AUXILIARY_PROCS;

	size = add_size(size, sizeof(BdrLocksCtl));
	size = add_size(size, mul_size(sizeof(BdrLocksDBState), bdr_max_databases));
	size = add_size(size, mul_size(sizeof(BDRLockWaiter), TotalProcs));

	return size;
}

static void
bdr_locks_shmem_startup(void)
{
	bool		found;

	if (prev_shmem_startup_hook != NULL)
		prev_shmem_startup_hook();

	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
	bdr_locks_ctl = ShmemInitStruct("bdr_locks",
									bdr_locks_shmem_size(),
									&found);
	if (!found)
	{
		memset(bdr_locks_ctl, 0, bdr_locks_shmem_size());
		bdr_locks_ctl->lock = &(GetNamedLWLockTranche("bdr_locks")->lock);
		bdr_locks_ctl->dbstate = (BdrLocksDBState *) bdr_locks_ctl + sizeof(BdrLocksCtl);
		bdr_locks_ctl->waiters = (BDRLockWaiter *) bdr_locks_ctl + sizeof(BdrLocksCtl) +
			mul_size(sizeof(BdrLocksDBState), bdr_max_databases);
	}
	LWLockRelease(AddinShmemInitLock);
}

/* Needs to be called from a shared_preload_library _PG_init() */
void
bdr_locks_shmem_init()
{
	/* Must be called from postmaster its self */
	Assert(IsPostmasterEnvironment && !IsUnderPostmaster);

	bdr_locks_ctl = NULL;

	RequestAddinShmemSpace(bdr_locks_shmem_size());
	RequestNamedLWLockTranche("bdr_locks", 1);

	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = bdr_locks_shmem_startup;
}

/* Waiter manipulation. */
void
bdr_locks_addwaiter(PGPROC *proc)
{
	BDRLockWaiter *waiter = &bdr_locks_ctl->waiters[proc->pgprocno];
	slist_iter	iter;

	waiter->proc = proc;

	/*
	 * The waiter list shouldn't be huge, and compared to the expense of a DDL
	 * lock it's cheap to check if we're already registered. After all, we're
	 * just adding ourselves to a wait-notification list. slist has no guard
	 * against adding a cycle, and we'd infinite-loop in bdr_locks_on_unlock
	 * otherwise. See 2ndQuadrant/bdr-private#130.
	 */
	slist_foreach(iter, &bdr_my_locks_database->waiters)
	{
		if (iter.cur == &waiter->node)
		{
			elog(WARNING, LOCKTRACE "backend %d already registered as waiter for DDL lock release",
				 MyProcPid);
			Assert(false);		/* crash in debug builds */
			return;
		}
	}
	slist_push_head(&bdr_my_locks_database->waiters, &waiter->node);

	elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG), LOCKTRACE "backend started waiting on DDL lock");
}

void
bdr_locks_on_unlock(void)
{
	while (!slist_is_empty(&bdr_my_locks_database->waiters))
	{
		slist_node *node;
		BDRLockWaiter *waiter;
		PGPROC	   *proc;

		node = slist_pop_head_node(&bdr_my_locks_database->waiters);

		/*
		 * Detect a self-referencing node and bail out by tossing the rest of
		 * the list. This shouldn't be necessary, it's an emergency bailout to
		 * stop us going into an infinite loop while holding a LWLock.
		 *
		 * We have to PANIC here so we force shmem and lwlock state to be
		 * re-inited. We could possibly just clobber the list and exit,
		 * leaving waiters dangling. But since this should be guarded against
		 * by bdr_locks_addwaiter, it shouldn't happen anyway. (See:
		 * 2ndQuadrant/bdr-private#130)
		 */
		if (slist_has_next(&bdr_my_locks_database->waiters, node)
			&& slist_next_node(&bdr_my_locks_database->waiters, node) == node)
			elog(PANIC, "cycle detected in DDL lock waiter linked list");

		waiter = slist_container(BDRLockWaiter, node, node);
		proc = waiter->proc;

		SetLatch(&proc->procLatch);
	}
}

/*
 * Set up a new lock_state to be applied on commit. No prior pending state may
 * be set.
 */
static void
bdr_locks_set_commit_pending_state(BDRLockState state)
{
	register_state_xact_callback();

	Assert(!bdr_lock_state_xact_callback_info.pending);

	bdr_lock_state_xact_callback_info.commit_pending_lock_state = state;
	bdr_lock_state_xact_callback_info.pending = true;
}

/*
 * Turn a DDL lock level into an elog level using the bdr.ddl_lock_trace_level
 * setting.
 */
static int
ddl_lock_log_level(int ddl_lock_trace_level)
{
	return ddl_lock_trace_level >= bdr_trace_ddl_locks_level ? LOG : DEBUG1;
}

/*
 * Find, and create if necessary, the lock state entry for dboid.
 */
static BdrLocksDBState *
bdr_locks_find_database(Oid dboid, bool create)
{
	int			off;
	int			free_off = -1;

	for (off = 0; off < bdr_max_databases; off++)
	{
		BdrLocksDBState *db = &bdr_locks_ctl->dbstate[off];

		if (db->in_use && db->dboid == MyDatabaseId)
		{
			bdr_my_locks_database = db;
			return db;

		}
		if (!db->in_use && free_off == -1)
			free_off = off;
	}

	if (!create)

		/*
		 * We can't call get_databse_name here as the catalogs may not be
		 * accessible, so we can only report the oid of the database.
		 */
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("database with oid=%u is not configured for BDR or BDR is still starting up",
						dboid)));

	if (free_off != -1)
	{
		BdrLocksDBState *db = &bdr_locks_ctl->dbstate[free_off];

		memset(db, 0, sizeof(BdrLocksDBState));
		db->dboid = MyDatabaseId;
		db->in_use = true;
		return db;
	}

	ereport(ERROR,
			(errcode(ERRCODE_CONFIGURATION_LIMIT_EXCEEDED),
			 errmsg("too many databases BDR-enabled for bdr.max_databases"),
			 errhint("Increase bdr.max_databases above the current limit of %d.", bdr_max_databases)));
}

static void
bdr_locks_find_my_database(bool create)
{
	Assert(IsUnderPostmaster);
	Assert(OidIsValid(MyDatabaseId));

	if (bdr_my_locks_database != NULL)
		return;

	bdr_my_locks_database = bdr_locks_find_database(MyDatabaseId, create);
	Assert(bdr_my_locks_database != NULL);
}

/*
 * This node has just started up. Init its local state and send a startup
 * announcement message.
 *
 * Called from the per-db worker.
 */
void
bdr_locks_startup(void)
{
	Relation	rel;
	ScanKey		key;
	SysScanDesc scan;
	Snapshot	snap;
	HeapTuple	tuple;
	StringInfoData s;

	Assert(IsUnderPostmaster);
	Assert(!IsTransactionState());
	Assert(bdr_worker_type == BDR_WORKER_PERDB);

	bdr_locks_find_my_database(true);

	/*
	 * Don't initialize database level lock state twice. An crash requiring
	 * that has to be severe enough to trigger a crash-restart cycle.
	 */
	if (bdr_my_locks_database->locked_and_loaded)
		return;

	slist_init(&bdr_my_locks_database->waiters);

	/* We haven't yet established how many nodes we're connected to. */
	bdr_my_locks_database->nnodes = -1;

	initStringInfo(&s);

	/*
	 * Send restart message causing all other backends to release global locks
	 * possibly held by us. We don't necessarily remember sending the request
	 * out.
	 */
	bdr_prepare_message(&s, BDR_MESSAGE_START);

	elog(DEBUG1, "sending global lock startup message");
	bdr_send_message(&s, false);

	/*
	 * reacquire all old ddl locks (held by other nodes) in
	 * bdr.bdr_global_locks table.
	 */
	StartTransactionCommand();
	snap = RegisterSnapshot(GetLatestSnapshot());
	rel = table_open(BdrLocksRelid, RowExclusiveLock);

	key = (ScanKey) palloc(sizeof(ScanKeyData) * 1);

	ScanKeyInit(&key[0],
				8,
				BTEqualStrategyNumber, F_OIDEQ,
				bdr_my_locks_database->dboid);

	scan = systable_beginscan(rel, 0, true, snap, 1, key);

	/* TODO: support multiple locks */
	while ((tuple = systable_getnext(scan)) != NULL)
	{
		Datum		values[10];
		bool		isnull[10];
		const char *state;
		RepOriginId node_id;
		BDRLockType lock_type;
		BDRNodeId	locker_id;

		heap_deform_tuple(tuple, RelationGetDescr(rel),
						  values, isnull);

		/* lookup the lock owner's node id */
		if (isnull[9])

			/*
			 * A bug in BDR prior to 2.0.4 could leave this null when it
			 * should really be in catchup mode.
			 */
		{
			elog(WARNING, "fixing up bad DDL lock state, should be 'catchup' not NULL");
			state = "catchup";
		}
		else
			state = TextDatumGetCString(values[9]);

		if (sscanf(TextDatumGetCString(values[1]), UINT64_FORMAT, &locker_id.sysid) != 1)
			elog(ERROR, "could not parse sysid %s",
				 TextDatumGetCString(values[1]));
		locker_id.timeline = DatumGetObjectId(values[2]);
		locker_id.dboid = DatumGetObjectId(values[3]);
		node_id = bdr_fetch_node_id_via_sysid(&locker_id);
		lock_type = bdr_lock_name_to_type(TextDatumGetCString(values[0]));

		if (strcmp(state, "acquired") == 0)
		{
			bdr_my_locks_database->lock_holder = node_id;
			bdr_my_locks_database->lockcount++;
			bdr_my_locks_database->lock_type = lock_type;
			bdr_my_locks_database->lock_state = BDR_LOCKSTATE_PEER_CONFIRMED;
			/* A remote node might have held the local lock before restart */
			elog(DEBUG1, "reacquiring local lock held before shutdown");
		}
		else if (strcmp(state, "catchup") == 0)
		{
			XLogRecPtr	wait_for_lsn;

			/*
			 * Restart the catchup period. There shouldn't be any need to
			 * kickof sessions here, because we're starting early.
			 */
			wait_for_lsn = GetXLogInsertRecPtr();
			bdr_prepare_message(&s, BDR_MESSAGE_REQUEST_REPLAY_CONFIRM);
			pq_sendint64(&s, wait_for_lsn);
			bdr_send_message(&s, false);

			bdr_my_locks_database->lock_holder = node_id;
			bdr_my_locks_database->lockcount++;
			bdr_my_locks_database->lock_type = lock_type;
			bdr_my_locks_database->lock_state = BDR_LOCKSTATE_PEER_CATCHUP;
			bdr_my_locks_database->replay_confirmed = 0;
			bdr_my_locks_database->replay_confirmed_lsn = wait_for_lsn;

			elog(DEBUG1, "restarting global lock replay catchup phase");
		}
		else
			elog(PANIC, "unknown lockstate '%s'", state);
	}

	systable_endscan(scan);
	UnregisterSnapshot(snap);
	table_close(rel, NoLock);

	CommitTransactionCommand();

	elog(DEBUG2, "global locking startup completed, local DML enabled");

	/* allow local DML */
	bdr_my_locks_database->locked_and_loaded = true;
}

/*
 * Called from the perdb worker to update our idea of the number of nodes
 * in the group, when we process an update from shmem.
 */
void
bdr_locks_set_nnodes(int nnodes)
{
	Assert(IsBackgroundWorker);
	Assert(bdr_my_locks_database != NULL);
	Assert(nnodes >= 0);

	LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);
	if (bdr_my_locks_database->nnodes < nnodes && bdr_my_locks_database->nnodes > 0 && !bdr_my_locks_database->lockcount)
	{
		/*
		 * Because we take the ddl lock before setting node_status = r now,
		 * and we only count ready nodes in the node count, it should only be
		 * possible for the node count to increase when the DDL lock is held.
		 *
		 * If there are older BDR nodes that don't take the DDL lock before
		 * joining this protection doesn't apply, so we can only warn about
		 * it. Unless there's a lock acquisition in progress (which we don't
		 * actually know from here) it's harmless anyway.
		 *
		 * A corresponding nodecount decrease without the DDL lock held is
		 * normal. Node detach doesn't take the DDL lock, but it's careful to
		 * reject any in-progress DDL lock attempt or release any held lock.
		 *
		 * FIXME: there's a race here where we could release the lock before
		 * applying the final changes for the node in the perdb worker. We
		 * should really perform this test and update when we see the new
		 * bdr.bdr_nodes row arrive instead. See 2ndQuadrant/bdr-private#97.
		 */
		ereport(WARNING,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("number of nodes increased %d => %d while local DDL lock not held",
						bdr_my_locks_database->nnodes, nnodes),
				 errhint("This should only happen during an upgrade from an older BDR version.")));
	}
	bdr_my_locks_database->nnodes = nnodes;
	LWLockRelease(bdr_locks_ctl->lock);
}

/*
 * Handle a WAL message destined for bdr_locks.
 *
 * Note that we don't usually pq_getmsgend(), instead ignoring any trailing
 * data. Future versions may add extra fields.
 */
bool
bdr_locks_process_message(int msg_type, bool transactional, XLogRecPtr lsn,
						  const BDRNodeId * const origin, StringInfo message)
{
	bool		handled = true;

	Assert(CurrentMemoryContext == MessageContext);

	if (msg_type == BDR_MESSAGE_START)
	{
		bdr_locks_process_remote_startup(origin);
	}
	else if (msg_type == BDR_MESSAGE_ACQUIRE_LOCK)
	{
		int			lock_type;

		if (message->cursor == message->len)	/* Old proto */
			lock_type = BDR_LOCK_WRITE;
		else
			lock_type = pq_getmsgint(message, 4);
		bdr_process_acquire_ddl_lock(origin, lock_type);
	}
	else if (msg_type == BDR_MESSAGE_RELEASE_LOCK)
	{
		BDRNodeId	peer;

		/* locks are node-wide, so no node name */
		bdr_getmsg_nodeid(message, &peer, false);
		bdr_process_release_ddl_lock(origin, &peer);
	}
	else if (msg_type == BDR_MESSAGE_CONFIRM_LOCK)
	{
		BDRNodeId	peer;
		int			lock_type;

		/* locks are node-wide, so no node name */
		bdr_getmsg_nodeid(message, &peer, false);

		if (message->cursor == message->len)	/* Old proto */
			lock_type = BDR_LOCK_WRITE;
		else
			lock_type = pq_getmsgint(message, 4);

		bdr_process_confirm_ddl_lock(origin, &peer, lock_type);
	}
	else if (msg_type == BDR_MESSAGE_DECLINE_LOCK)
	{
		BDRNodeId	peer;
		int			lock_type;

		/* locks are node-wide, so no node name */
		bdr_getmsg_nodeid(message, &peer, false);

		if (message->cursor == message->len)	/* Old proto */
			lock_type = BDR_LOCK_WRITE;
		else
			lock_type = pq_getmsgint(message, 4);

		bdr_process_decline_ddl_lock(origin, &peer, lock_type);
	}
	else if (msg_type == BDR_MESSAGE_REQUEST_REPLAY_CONFIRM)
	{
		XLogRecPtr	confirm_lsn;

		confirm_lsn = pq_getmsgint64(message);

		bdr_process_request_replay_confirm(origin, confirm_lsn);
	}
	else if (msg_type == BDR_MESSAGE_REPLAY_CONFIRM)
	{
		XLogRecPtr	confirm_lsn;

		confirm_lsn = pq_getmsgint64(message);

		bdr_process_replay_confirm(origin, confirm_lsn);
	}
	else
	{
		elog(LOG, "unknown message type %d", msg_type);
		handled = false;
	}

	Assert(CurrentMemoryContext == MessageContext);

	return handled;
}

/*
 * Callback to release the global lock on commit/abort of the holding xact.
 * Only called from a user backend - or a bgworker from some unrelated tool.
 */
static void
bdr_lock_holder_xact_callback(XactEvent event, void *arg)
{
	BDRNodeId	myid;

	Assert(arg == NULL);
	Assert(!IsBdrApplyWorker());

	bdr_make_my_nodeid(&myid);

	if (!this_xact_acquired_lock)
		return;

	if (event == XACT_EVENT_ABORT || event == XACT_EVENT_COMMIT)
	{
		StringInfoData s;

		elog(ddl_lock_log_level(DDL_LOCK_TRACE_ACQUIRE_RELEASE), LOCKTRACE "releasing owned ddl lock on xact %s",
			 event == XACT_EVENT_ABORT ? "abort" : "commit");

		initStringInfo(&s);
		bdr_prepare_message(&s, BDR_MESSAGE_RELEASE_LOCK);

		/* no lock_type, finished transaction releases all locks it held */
		bdr_send_nodeid(&s, &myid, false);
		bdr_send_message(&s, false);

		LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);
		if (bdr_my_locks_database->lockcount > 0)
		{
			Assert(bdr_my_locks_database->lock_state > BDR_LOCKSTATE_NOLOCK);
			bdr_my_locks_database->lockcount--;
		}
		else
			elog(WARNING, "releasing unacquired global lock");

		this_xact_acquired_lock = false;
		Assert(bdr_my_locks_database->lock_holder_local_pid == MyProcPid);
		bdr_my_locks_database->lock_holder_local_pid = 0;
		bdr_my_locks_database->lock_type = BDR_LOCK_NOLOCK;
		bdr_my_locks_database->lock_state = BDR_LOCKSTATE_NOLOCK;
		bdr_my_locks_database->replay_confirmed = 0;
		bdr_my_locks_database->replay_confirmed_lsn = InvalidXLogRecPtr;
		bdr_my_locks_database->requestor = NULL;

		/* We requested the lock we're releasing */

		if (bdr_my_locks_database->lockcount == 0)
			bdr_locks_on_unlock();

		LWLockRelease(bdr_locks_ctl->lock);
	}
}

static void
register_holder_xact_callback(void)
{
	static bool registered;

	if (!registered)
	{
		RegisterXactCallback(bdr_lock_holder_xact_callback, NULL);
		registered = true;
	}
}

/*
 * Callback to update shmem state after we change global ddl lock state in
 * bdr_global_locks. Only called from apply worker and perdb worker.
 */
static void
bdr_lock_state_xact_callback(XactEvent event, void *arg)
{
	Assert(arg == NULL);
	Assert(IsBackgroundWorker);
	Assert(IsBdrApplyWorker() || IsBdrPerdbWorker());

	if (event == XACT_EVENT_COMMIT && bdr_lock_state_xact_callback_info.pending)
	{
		Assert(LWLockHeldByMe((bdr_locks_ctl->lock)));
		bdr_my_locks_database->lock_state
			= bdr_lock_state_xact_callback_info.commit_pending_lock_state;
		bdr_lock_state_xact_callback_info.pending = false;
	}
}

static void
register_state_xact_callback(void)
{
	static bool registered;

	if (!registered)
	{
		RegisterXactCallback(bdr_lock_state_xact_callback, NULL);
		registered = true;
	}
}

static SysScanDesc
locks_begin_scan(Relation rel, Snapshot snap, const BDRNodeId * const node)
{
	ScanKey		key;
	char		buf[33];

	key = (ScanKey) palloc(sizeof(ScanKeyData) * 4);

	snprintf(buf, sizeof(buf), UINT64_FORMAT, node->sysid);

	ScanKeyInit(&key[0],
				2,
				BTEqualStrategyNumber, F_TEXTEQ,
				CStringGetTextDatum(buf));
	ScanKeyInit(&key[1],
				3,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(node->timeline));
	ScanKeyInit(&key[2],
				4,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(node->dboid));

	return systable_beginscan(rel, 0, true, snap, 3, key);
}

/*
 * Acquire DDL lock on the side that wants to perform DDL.
 *
 * Called from a user backend when the command filter spots a DDL attempt; runs
 * in the user backend.
 */
void
bdr_acquire_ddl_lock(BDRLockType lock_type)
{
	StringInfoData s;

	Assert(IsTransactionState());
	/* Not called from within a BDR worker */
	Assert(bdr_worker_type == BDR_WORKER_EMPTY_SLOT);

	/* We don't support other types of the lock yet. */
	Assert(lock_type == BDR_LOCK_DDL || lock_type == BDR_LOCK_WRITE);

	/* shouldn't be called with ddl locking disabled */
	/* replace bdr_skip_ddl_locking by bdr_skip_ddl_replication for now */
	Assert(!bdr_skip_ddl_replication);

	bdr_locks_find_my_database(false);

	/*
	 * Currently we only support one lock. We might be called with it already
	 * held or to upgrade it.
	 */
	Assert((bdr_my_locks_database->lock_type == BDR_LOCK_NOLOCK && bdr_my_locks_database->lockcount == 0 && !this_xact_acquired_lock)
		   || (bdr_my_locks_database->lock_type > BDR_LOCK_NOLOCK && bdr_my_locks_database->lockcount == 1));

	/* No need to do anything if already holding requested lock. */
	if (this_xact_acquired_lock &&
		bdr_my_locks_database->lock_type >= lock_type)
	{
		Assert(bdr_my_locks_database->lock_holder_local_pid == MyProcPid);
		return;
	}

	/*
	 * If this is the first time in current transaction that we are trying to
	 * acquire DDL lock, do the sanity checking first.
	 */
	if (!this_xact_acquired_lock)
	{
		/* replace bdr_permit_ddl_locking by !bdr_skip_ddl_replication for now */
		if (bdr_skip_ddl_replication)
		{
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("global DDL locking attempt rejected by configuration"),
					 errdetail("bdr.skip_ddl_replication is true and the attempted command "
							   "would require the global lock to be acquired. "
							   "Command rejected."),
					 errhint("See the 'DDL replication' chapter of the documentation.")));
		}

		if (bdr_my_locks_database->nnodes < 0)
		{
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("no peer nodes or peer node count unknown, cannot acquire global lock"),
					 errhint("BDR is probably still starting up, wait a while.")));
		}
	}

	if (this_xact_acquired_lock)
	{
		elog(ddl_lock_log_level(DDL_LOCK_TRACE_STATEMENT),
			 LOCKTRACE "acquiring in mode <%s> (upgrading from <%s>) from <%d> peer nodes for " BDR_NODEID_FORMAT_WITHNAME " [tracelevel=%s]",
			 bdr_lock_type_to_name(lock_type),
			 bdr_lock_type_to_name(bdr_my_locks_database->lock_type),
			 bdr_my_locks_database->nnodes,
			 BDR_LOCALID_FORMAT_WITHNAME_ARGS,
			 GetConfigOption("bdr.trace_ddl_locks_level", false, false));
	}
	else
	{
		elog(ddl_lock_log_level(DDL_LOCK_TRACE_STATEMENT),
			 LOCKTRACE "acquiring in mode <%s> from <%d> nodes for " BDR_NODEID_FORMAT_WITHNAME " [tracelevel=%s]",
			 bdr_lock_type_to_name(lock_type),
			 bdr_my_locks_database->nnodes,
			 BDR_LOCALID_FORMAT_WITHNAME_ARGS,
			 GetConfigOption("bdr.trace_ddl_locks_level", false, false));
	}

	/* register an XactCallback to release the lock */
	register_holder_xact_callback();

	LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);

	/* check whether the lock can actually be acquired */
	if (!this_xact_acquired_lock && bdr_my_locks_database->lockcount > 0)
	{
		BDRNodeId	holder,
					myid;

		bdr_make_my_nodeid(&myid);

		bdr_fetch_sysid_via_node_id(bdr_my_locks_database->lock_holder, &holder);

		elog(ddl_lock_log_level(DDL_LOCK_TRACE_ACQUIRE_RELEASE),
			 LOCKTRACE "lock already held by " BDR_NODEID_FORMAT_WITHNAME " (is_local %d, pid %d)",
			 BDR_NODEID_FORMAT_WITHNAME_ARGS(holder),
			 bdr_nodeid_eq(&myid, &holder),
			 bdr_my_locks_database->lock_holder_local_pid);

		Assert(bdr_my_locks_database->lock_state > BDR_LOCKSTATE_NOLOCK);

		LWLockRelease(bdr_locks_ctl->lock);
		ereport(ERROR,
				(errcode(ERRCODE_LOCK_NOT_AVAILABLE),
				 errmsg("database is locked against ddl by another node"),
				 errhint("Node " BDR_NODEID_FORMAT_WITHNAME " in the cluster is already performing DDL.",
						 BDR_NODEID_FORMAT_WITHNAME_ARGS(holder))));
	}

	/*
	 * There should be nobody waiting to be notified if the DDL lock isn't
	 * held, and now we hold bdr_locks_ctl->lock and know the lock is free.
	 */
	Assert(slist_is_empty(&bdr_my_locks_database->waiters));

	/* send message about ddl lock */
	initStringInfo(&s);
	bdr_prepare_message(&s, BDR_MESSAGE_ACQUIRE_LOCK);
	/* Add lock type */
	pq_sendint(&s, lock_type, 4);

	START_CRIT_SECTION();

	/*
	 * NB: We need to setup the shmem state as if we'd have already acquired
	 * the lock before we release the LWLock on bdr_locks_ctl->lock. Otherwise
	 * concurrent transactions could acquire the lock, and we wouldn't send a
	 * release message when we fail to fully acquire the lock.
	 *
	 * This means that if we're called in a subtransaction that aborts the
	 * outer transaction will still hold the stronger lock.
	 *
	 * BUG: Per 2ndQuadrant/bdr-private#77 we may not properly check the
	 * acquisition of the stronger lock after a subxact abort.
	 */
	if (!this_xact_acquired_lock)
	{
		/*
		 * Can't be upgrading an existing lock; either we'd already have
		 * this_xact_acquired_lock or we'd have bailed out above
		 */
		Assert(bdr_my_locks_database->lockcount == 0);
		Assert(bdr_my_locks_database->lock_state == BDR_LOCKSTATE_NOLOCK);

		bdr_my_locks_database->lockcount++;
		this_xact_acquired_lock = true;
		Assert(bdr_my_locks_database->lock_holder_local_pid == 0);
		bdr_my_locks_database->lock_holder_local_pid = MyProcPid;
	}

	Assert(bdr_my_locks_database->lock_holder_local_pid == MyProcPid);

	/* Need to clear since we're possibly upgrading an already-held lock */
	bdr_my_locks_database->lock_holder = InvalidRepOriginId;
	bdr_my_locks_database->acquire_confirmed = 0;
	bdr_my_locks_database->acquire_declined = 0;

	/* Register as acquiring lock */
	Assert(bdr_my_locks_database->lock_holder_local_pid == MyProcPid);
	bdr_my_locks_database->requestor = &MyProc->procLatch;
	bdr_my_locks_database->lock_type = lock_type;
	bdr_my_locks_database->lock_state = BDR_LOCKSTATE_ACQUIRE_TALLY_CONFIRMATIONS;

	/* lock looks to be free, try to acquire it */
	bdr_send_message(&s, false);

	END_CRIT_SECTION();

	LWLockRelease(bdr_locks_ctl->lock);

	/* ---
	 * Now wait for standbys to ack ddl lock
	 * ---
	 */
	elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
		 LOCKTRACE "sent DDL lock mode %s request for " BDR_NODEID_FORMAT_WITHNAME "), waiting for confirmation",
		 bdr_lock_type_to_name(lock_type), BDR_LOCALID_FORMAT_WITHNAME_ARGS);

	while (true)
	{
		int			rc;

		ResetLatch(&MyProc->procLatch);

		LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);

		/*
		 * check for confirmations in shared memory.
		 *
		 * Even one decline is enough to prevent lock acquisition so bail
		 * immediately if we see one.
		 */
		if (bdr_my_locks_database->acquire_declined > 0)
		{
			elog(ddl_lock_log_level(DDL_LOCK_TRACE_ACQUIRE_RELEASE), LOCKTRACE "acquire declined by another node");
			LWLockRelease(bdr_locks_ctl->lock);
			ereport(ERROR,
					(errcode(ERRCODE_LOCK_NOT_AVAILABLE),
					 errmsg("could not acquire global lock - another node has declined our lock request"),
					 errhint("Likely the other node is acquiring the global lock itself.")));
		}

		/* wait till all have given their consent */
		if (bdr_my_locks_database->acquire_confirmed >= bdr_my_locks_database->nnodes)
		{
			LWLockRelease(bdr_locks_ctl->lock);
			break;
		}
		LWLockRelease(bdr_locks_ctl->lock);

		rc = WaitLatch(&MyProc->procLatch,
					   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
					   10000L, PG_WAIT_EXTENSION);

		/* emergency bailout if postmaster has died */
		if (rc & WL_POSTMASTER_DEATH)
			proc_exit(1);

		CHECK_FOR_INTERRUPTS();
	}

	LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);

	/* TODO: recheck it's ours */
	bdr_my_locks_database->acquire_confirmed = 0;
	bdr_my_locks_database->acquire_declined = 0;
	bdr_my_locks_database->requestor = NULL;
	Assert(bdr_my_locks_database->lock_state == BDR_LOCKSTATE_ACQUIRE_TALLY_CONFIRMATIONS);
	bdr_my_locks_database->lock_state = BDR_LOCKSTATE_ACQUIRE_ACQUIRED;

	elog(ddl_lock_log_level(DDL_LOCK_TRACE_ACQUIRE_RELEASE),
		 LOCKTRACE "DDL lock acquired in mode mode %s for " BDR_NODEID_FORMAT_WITHNAME,
		 bdr_lock_type_to_name(lock_type), BDR_LOCALID_FORMAT_WITHNAME_ARGS);

	LWLockRelease(bdr_locks_ctl->lock);
}

Datum
bdr_acquire_global_lock(PG_FUNCTION_ARGS)
{
	char	   *mode = text_to_cstring(PG_GETARG_TEXT_P(0));

	/* replace bdr_skip_ddl_locking by bdr_skip_ddl_replication for now */
	if (bdr_skip_ddl_replication)
		ereport(WARNING,
				(errmsg("bdr.skip_ddl_replication is set, ignoring explicit bdr.bdr_acquire_global_lock(...) call")));
	else
		bdr_acquire_ddl_lock(bdr_lock_name_to_type(mode));

	PG_RETURN_VOID();
}

/*
 * True if the passed nodeid is the node this apply worker replays
 * changes from.
 */
static bool
check_is_my_origin_node(const BDRNodeId * const peer)
{
	BDRNodeId	session_origin_node;
	MemoryContext old_ctx;

	Assert(!IsTransactionState());
	Assert(bdr_worker_type == BDR_WORKER_APPLY);

	old_ctx = CurrentMemoryContext;
	StartTransactionCommand();
	bdr_fetch_sysid_via_node_id(replorigin_session_origin, &session_origin_node);
	CommitTransactionCommand();
	MemoryContextSwitchTo(old_ctx);

	return bdr_nodeid_eq(peer, &session_origin_node);
}

/*
 * True if the passed nodeid is the local node.
 */
static bool
check_is_my_node(const BDRNodeId * const node)
{
	BDRNodeId	myid;

	bdr_make_my_nodeid(&myid);
	return bdr_nodeid_eq(node, &myid);
}

/*
 * Kill any writing transactions while giving them some grace period for
 * finishing.
 *
 * Caller is responsible for ensuring that no new writes can be started during
 * the execution of this function.
 */
static bool
cancel_conflicting_transactions(void)
{
	VirtualTransactionId *conflict;
	TimestampTz killtime,
				canceltime;
	int			waittime = 1000;


	LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);
	bdr_my_locks_database->lock_state = BDR_LOCKSTATE_PEER_CANCEL_XACTS;
	LWLockRelease(bdr_locks_ctl->lock);

	killtime = TimestampTzPlusMilliseconds(GetCurrentTimestamp(),
										   bdr_max_ddl_lock_delay > 0 ?
										   bdr_max_ddl_lock_delay : max_standby_streaming_delay);

	if (bdr_ddl_lock_timeout > 0 || LockTimeout > 0)
		canceltime = TimestampTzPlusMilliseconds(GetCurrentTimestamp(),
												 bdr_ddl_lock_timeout > 0 ? bdr_ddl_lock_timeout : LockTimeout);
	else
		TIMESTAMP_NOEND(canceltime);

	conflict = GetConflictingVirtualXIDs(InvalidTransactionId, MyDatabaseId);

	while (conflict->backendId != InvalidBackendId)
	{
		PGPROC	   *pgproc = BackendIdGetProc(conflict->backendId);
#if PG_VERSION_NUM < 140000
		PGXACT	   *pgxact;
#endif

		if (pgproc == NULL)
		{
			/* backend went away concurrently */
			conflict++;
			continue;
		}

#if PG_VERSION_NUM < 140000
		pgxact = &ProcGlobal->allPgXact[pgproc->pgprocno];

		/* Skip the transactions that didn't do any writes. */
		if (!TransactionIdIsValid(pgxact->xid))
#else
		if (!TransactionIdIsValid(pgproc->xid))
#endif
		{
			conflict++;
			continue;
		}

		/* If here is writing transaction give it time to finish */
		if (!TIMESTAMP_IS_NOEND(canceltime) &&
			GetCurrentTimestamp() < canceltime)
		{
			return false;
		}
		else if (GetCurrentTimestamp() < killtime)
		{
			int			rc;

			/* Increasing backoff interval for wait time with limit of 1s */
			waittime *= 2;
			if (waittime > 1000000)
				waittime = 1000000;

			rc = WaitLatch(&MyProc->procLatch,
						   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
						   waittime, PG_WAIT_EXTENSION);

			ResetLatch(&MyProc->procLatch);

			/* emergency bailout if postmaster has died */
			if (rc & WL_POSTMASTER_DEATH)
				proc_exit(1);
		}
		else
		{
			/* We reached timeout so lets kill the writing transaction */
			pid_t		p = CancelVirtualTransaction(*conflict, PROCSIG_RECOVERY_CONFLICT_LOCK);

			/*
			 * Either confirm kill or sleep a bit to prevent the other node
			 * being busy with signal processing.
			 */
			if (p == 0)
				conflict++;
			else
				pg_usleep(1000);

			elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
				 LOCKTRACE "signalling pid %d to terminate because of global DDL lock acquisition", p);
		}

		CHECK_FOR_INTERRUPTS();
	}

	return true;
}

static void
bdr_request_replay_confirmation(void)
{
	StringInfoData s;
	XLogRecPtr	wait_for_lsn;

	initStringInfo(&s);

	wait_for_lsn = GetXLogInsertRecPtr();
	bdr_prepare_message(&s, BDR_MESSAGE_REQUEST_REPLAY_CONFIRM);
	pq_sendint64(&s, wait_for_lsn);

	LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);

	/*
	 * We only do catchup in write-mode locking after cancelling conflicting
	 * xacts. Or after startup in catchup mode, but that's entered directly
	 * from startup, not here.
	 */
	Assert(
		   bdr_my_locks_database->lock_type == BDR_LOCK_WRITE
		   && (bdr_my_locks_database->lock_state == BDR_LOCKSTATE_PEER_CANCEL_XACTS));

	bdr_send_message(&s, false);

	bdr_my_locks_database->replay_confirmed = 0;
	bdr_my_locks_database->replay_confirmed_lsn = wait_for_lsn;
	bdr_my_locks_database->lock_state = BDR_LOCKSTATE_PEER_CATCHUP;
	LWLockRelease(bdr_locks_ctl->lock);
}

/*
 * Another node has asked for a DDL lock. Try to acquire the local ddl lock.
 *
 * Runs in the apply worker.
 */
void
bdr_process_acquire_ddl_lock(const BDRNodeId * const node, BDRLockType lock_type)
{
	StringInfoData s;
	const char *lock_name = bdr_lock_type_to_name(lock_type);
	BDRNodeId	myid;
	MemoryContext old_ctx = CurrentMemoryContext;

	bdr_make_my_nodeid(&myid);

	if (!check_is_my_origin_node(node))
		return;

	Assert(lock_type > BDR_LOCK_NOLOCK);

	bdr_locks_find_my_database(false);

	elog(ddl_lock_log_level(DDL_LOCK_TRACE_PEERS),
		 LOCKTRACE "%s lock requested by node " BDR_NODEID_FORMAT_WITHNAME,
		 lock_name, BDR_NODEID_FORMAT_WITHNAME_ARGS(*node));

	initStringInfo(&s);

	/*
	 * To prevent two concurrent apply workers from granting the DDL lock at
	 * the same time, lock out the control segment.
	 */
	LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);

	if (bdr_my_locks_database->lockcount == 0)
	{
		Relation	rel;
		Datum		values[10];
		bool		nulls[10];
		HeapTuple	tup;

		/*
		 * No previous DDL lock found. Start acquiring it.
		 */
		elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
			 LOCKTRACE "no prior global lock found, acquiring global lock locally");

		Assert(bdr_my_locks_database->lock_state == BDR_LOCKSTATE_NOLOCK);

		/* Add a row to bdr_locks */
		old_ctx = CurrentMemoryContext;
		StartTransactionCommand();

		memset(nulls, 0, sizeof(nulls));

		rel = table_open(BdrLocksRelid, RowExclusiveLock);

		values[0] = CStringGetTextDatum(lock_name);

		appendStringInfo(&s, UINT64_FORMAT, node->sysid);
		values[1] = CStringGetTextDatum(s.data);
		resetStringInfo(&s);
		values[2] = ObjectIdGetDatum(node->timeline);
		values[3] = ObjectIdGetDatum(node->dboid);

		values[4] = TimestampTzGetDatum(GetCurrentTimestamp());

		appendStringInfo(&s, UINT64_FORMAT, myid.sysid);
		values[5] = CStringGetTextDatum(s.data);
		resetStringInfo(&s);
		values[6] = ObjectIdGetDatum(myid.timeline);
		values[7] = ObjectIdGetDatum(myid.dboid);

		nulls[8] = true;

		values[9] = PointerGetDatum(cstring_to_text("catchup"));

		PG_TRY();
		{
			tup = heap_form_tuple(RelationGetDescr(rel), values, nulls);
			/* simple_heap_insert(rel, tup); */
			bdr_locks_set_commit_pending_state(BDR_LOCKSTATE_PEER_BEGIN_CATCHUP);
			/* CatalogTupleUpdate(rel, &tup->t_self, tup); */
			CatalogTupleInsert(rel, tup);
			ForceSyncCommit();	/* async commit would be too complicated */
			table_close(rel, NoLock);
			CommitTransactionCommand();
			MemoryContextSwitchTo(old_ctx);
		}
		PG_CATCH();
		{
			if (geterrcode() == ERRCODE_UNIQUE_VIOLATION)
			{
				/*
				 * Shouldn't happen since we take the control segment lock
				 * before checking lockcount, and increment lockcount before
				 * releasing it.
				 */
				elog(WARNING,
					 "declining global lock because a conflicting global lock exists in bdr_global_locks");
				AbortOutOfAnyTransaction();
				/* We only set BEGIN_CATCHUP mode on commit */
				Assert(bdr_my_locks_database->lock_state == BDR_LOCKSTATE_NOLOCK);
				goto decline;
			}
			else
				PG_RE_THROW();
		}
		PG_END_TRY();

		/* setup ddl lock */
		bdr_my_locks_database->lockcount++;
		bdr_my_locks_database->lock_type = lock_type;
		bdr_my_locks_database->lock_holder = replorigin_session_origin;
		LWLockRelease(bdr_locks_ctl->lock);

		if (lock_type >= BDR_LOCK_WRITE)
		{
			/*
			 * Now kill all local processes that are still writing. We can't
			 * just prevent them from writing via the acquired lock as they
			 * are still running.
			 */
			elog(ddl_lock_log_level(DDL_LOCK_TRACE_PEERS),
				 LOCKTRACE "terminating any local processes that conflict with the global lock");
			if (!cancel_conflicting_transactions())
			{
				elog(ddl_lock_log_level(DDL_LOCK_TRACE_PEERS),
					 LOCKTRACE "failed to terminate, declining the lock");
				goto decline;
			}

			/*
			 * We now have to wait till all our local pending changes have
			 * been streamed out. We do this by sending a message which is
			 * then acked by all other nodes. When the required number of
			 * messages is back we can confirm the lock to the original
			 * requestor (c.f. bdr_process_replay_confirm()).
			 *
			 * If we didn't wait for everyone to replay local changes then a
			 * DDL change that caused those local changes not to apply on
			 * remote nodes might occur, causing a divergent conflict.
			 */
			elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
				 LOCKTRACE "requesting replay confirmation from all other nodes before confirming global lock granted");
			bdr_request_replay_confirmation();
		}
		else
		{
			/*
			 * Simple DDL locks that are not conflicting with existing
			 * transactions can be just confirmed immediatelly.
			 */

			elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
				 LOCKTRACE "non-conflicting lock requested, logging confirmation of this node's acquisition of global lock");
			LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);
			bdr_send_confirm_lock();
			LWLockRelease(bdr_locks_ctl->lock);
		}
		elog(ddl_lock_log_level(DDL_LOCK_TRACE_ACQUIRE_RELEASE),
			 LOCKTRACE "global lock granted to remote node " BDR_NODEID_FORMAT_WITHNAME,
			 BDR_NODEID_FORMAT_WITHNAME_ARGS(*node));
	}
	else if (bdr_my_locks_database->lock_holder == replorigin_session_origin &&
			 lock_type > bdr_my_locks_database->lock_type)
	{
		Relation	rel;
		SysScanDesc scan;
		Snapshot	snap;
		HeapTuple	tuple;
		bool		found = false;
		BDRNodeId	replay_node;

		elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
			 LOCKTRACE "prior lesser lock from same lock holder, upgrading the global lock locally");

		Assert(!IsTransactionState());
		old_ctx = CurrentMemoryContext;
		StartTransactionCommand();
		bdr_fetch_sysid_via_node_id(bdr_my_locks_database->lock_holder, &replay_node);

		/*
		 * Update state of lock.
		 */
		/* Scan for a matching lock whose state needs to be updated */
		snap = RegisterSnapshot(GetLatestSnapshot());
		rel = table_open(BdrLocksRelid, RowExclusiveLock);

		scan = locks_begin_scan(rel, snap, &replay_node);

		while ((tuple = systable_getnext(scan)) != NULL)
		{
			HeapTuple	newtuple;
			Datum		values[10];
			bool		isnull[10];

			if (found)
				elog(PANIC, "duplicate lock?");

			heap_deform_tuple(tuple, RelationGetDescr(rel),
							  values, isnull);
			/* lock_type column */
			values[0] = CStringGetTextDatum(lock_name);
			/* lock state column */
			isnull[9] = false;
			values[9] = PointerGetDatum(cstring_to_text("catchup"));

			newtuple = heap_form_tuple(RelationGetDescr(rel),
									   values, isnull);
			/* simple_heap_update(rel, &tuple->t_self, newtuple); */
			bdr_locks_set_commit_pending_state(BDR_LOCKSTATE_PEER_BEGIN_CATCHUP);
			CatalogTupleUpdate(rel, &tuple->t_self, newtuple);
			found = true;
		}

		if (!found)
			elog(PANIC, "got lock in memory without corresponding lock table entry");

		systable_endscan(scan);
		UnregisterSnapshot(snap);
		table_close(rel, NoLock);

		CommitTransactionCommand();
		MemoryContextSwitchTo(old_ctx);

		LWLockRelease(bdr_locks_ctl->lock);

		if (lock_type >= BDR_LOCK_WRITE)
		{
			/*
			 * Now kill all local processes that are still writing. We can't
			 * just prevent them from writing via the acquired lock as they
			 * are still running.
			 */
			elog(ddl_lock_log_level(DDL_LOCK_TRACE_PEERS),
				 LOCKTRACE "terminating any local processes that conflict with the global lock");
			if (!cancel_conflicting_transactions())
			{
				elog(ddl_lock_log_level(DDL_LOCK_TRACE_PEERS),
					 LOCKTRACE "failed to terminate, declining the lock");
				goto decline;
			}

			/* update inmemory lock state */
			LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);
			bdr_my_locks_database->lock_type = lock_type;
			LWLockRelease(bdr_locks_ctl->lock);

			/*
			 * We now have to wait till all our local pending changes have
			 * been streamed out. We do this by sending a message which is
			 * then acked by all other nodes. When the required number of
			 * messages is back we can confirm the lock to the original
			 * requestor (c.f. bdr_process_replay_confirm()).
			 *
			 * If we didn't wait for everyone to replay local changes then a
			 * DDL change that caused those local changes not to apply on
			 * remote nodes might occur, causing a divergent conflict.
			 */
			elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
				 LOCKTRACE "requesting replay confirmation from all other nodes before confirming global lock granted");
			bdr_request_replay_confirmation();
		}
		else
		{
			/*
			 * Simple DDL locks that are not conflicting with existing
			 * transactions can be just confirmed immediatelly.
			 */

			/* update inmemory lock state */
			LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);
			bdr_my_locks_database->lock_type = lock_type;

			elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
				 LOCKTRACE "non-conflicting lock requested, logging confirmation of this node's acquisition of global lock");
			bdr_send_confirm_lock();
			LWLockRelease(bdr_locks_ctl->lock);
		}

		elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
			 LOCKTRACE "global lock granted to remote node " BDR_NODEID_FORMAT_WITHNAME,
			 BDR_NODEID_FORMAT_WITHNAME_ARGS(*node));
	}
	else
	{
		BDRNodeId	replay_node;

		LWLockRelease(bdr_locks_ctl->lock);
decline:
		ereport(ddl_lock_log_level(DDL_LOCK_TRACE_ACQUIRE_RELEASE),
				(errmsg(LOCKTRACE "declining remote global lock request, this node is already locked by origin=%u at level %s",
						bdr_my_locks_database->lock_holder,
						bdr_lock_type_to_name(bdr_my_locks_database->lock_type))));

		bdr_prepare_message(&s, BDR_MESSAGE_DECLINE_LOCK);

		Assert(!IsTransactionState());
		StartTransactionCommand();
		bdr_fetch_sysid_via_node_id(bdr_my_locks_database->lock_holder, &replay_node);
		CommitTransactionCommand();
		MemoryContextSwitchTo(old_ctx);

		bdr_send_nodeid(&s, node, false);
		pq_sendint(&s, lock_type, 4);

		bdr_send_message(&s, false);
		pfree(s.data);
	}
}

/*
 * Another node has released the global DDL lock, update our local state.
 *
 * Runs in the apply worker.
 *
 * The only time that !bdr_nodeid_eq(origin,lock) is if we're in
 * catchup mode and relaying locking messages from peers.
 */
void
bdr_process_release_ddl_lock(const BDRNodeId * const origin, const BDRNodeId * const lock)
{

	if (!check_is_my_origin_node(origin))
		return;

	elog(ddl_lock_log_level(DDL_LOCK_TRACE_PEERS),
		 LOCKTRACE "global lock released by " BDR_NODEID_FORMAT_WITHNAME,
		 BDR_NODEID_FORMAT_WITHNAME_ARGS(*lock));

	bdr_locks_release_local_ddl_lock(lock);
}

/*
 * Peer node has been detached from the system. We need to clear up any
 * local DDL lock it may hold so that we can continue to process
 * writes.
 *
 * This must ONLY be called after the apply worker for the peer
 * successfully is terminated.
 */
void
bdr_locks_node_detached(BDRNodeId * node)
{
	bool		peer_holds_lock = false;
	BDRNodeId	owner;

	bdr_locks_find_my_database(false);

	elog(INFO, "XXX testing if node holds ddl lock");

	/*
	 * Rather than looking up the replication origin of the node being detached,
	 * which might no longer exist, check if the lock is held and if so, if
	 * the node id matches.
	 *
	 * We could just call bdr_locks_release_local_ddl_lock but that'll do
	 * table scans etc we can avoid by taking a quick look at shmem first.
	 */
	LWLockAcquire(bdr_locks_ctl->lock, LW_SHARED);
	if (bdr_my_locks_database->lock_type > BDR_LOCK_NOLOCK)
	{
		StartTransactionCommand();
		bdr_fetch_sysid_via_node_id(bdr_my_locks_database->lock_holder, &owner);

		elog(ddl_lock_log_level(DDL_LOCK_TRACE_PEERS),
			 LOCKTRACE "global lock held by " BDR_NODEID_FORMAT_WITHNAME " released after node detach",
			 BDR_NODEID_FORMAT_WITHNAME_ARGS(*node));

		peer_holds_lock = bdr_nodeid_eq(node, &owner);
		CommitTransactionCommand();

		elog(INFO, "XXX target peer holds lock: %d", peer_holds_lock);
	}
	LWLockRelease(bdr_locks_ctl->lock);

	if (peer_holds_lock)
	{
		elog(INFO, "XXX attempting to release lock");
		bdr_locks_release_local_ddl_lock(node);
		elog(INFO, "XXX attempted to release lock");
	}
}

/*
 * Release any global DDL lock we may hold for node 'lock'.
 *
 * This is invoked from the apply worker when we get release messages,
 * and by node detach handling when detaching a node that may still hold
 * the DDL lock.
 */
static void
bdr_locks_release_local_ddl_lock(const BDRNodeId * const lock)
{
	Relation	rel;
	Snapshot	snap;
	SysScanDesc scan;
	HeapTuple	tuple;
	bool		found = false;
	Latch	   *latch;
	MemoryContext old_ctx = CurrentMemoryContext;
	MemoryContext cs_mem_ctx;

	/* FIXME: check db */

	bdr_locks_find_my_database(false);

	/*
	 * Remove row from bdr_locks *before* releasing the in memory lock. If we
	 * crash we'll replay the event again.
	 */
	StartTransactionCommand();
	snap = RegisterSnapshot(GetLatestSnapshot());
	rel = table_open(BdrLocksRelid, RowExclusiveLock);

	/* Find any bdr_locks entry for the releasing peer */
	scan = locks_begin_scan(rel, snap, lock);

	while ((tuple = systable_getnext(scan)) != NULL)
	{
		elog(DEBUG2, "found global lock entry to delete in response to global lock release message");
		simple_heap_delete(rel, &tuple->t_self);
		bdr_locks_set_commit_pending_state(BDR_LOCKSTATE_NOLOCK);
		ForceSyncCommit();		/* async commit would be too complicated */
		found = true;

		/*
		 * if we found a local lock tuple, there must be shmem state for it
		 * (and we recover it after crash, too).
		 *
		 * It can't be a state that exists only on the acquiring node because
		 * that never produces tuples on disk.
		 */
		Assert(bdr_my_locks_database->lock_type > BDR_LOCK_NOLOCK);
		Assert(bdr_my_locks_database->lock_state > BDR_LOCKSTATE_NOLOCK
			   && bdr_my_locks_database->lock_state != BDR_LOCKSTATE_ACQUIRE_TALLY_CONFIRMATIONS
			   && bdr_my_locks_database->lock_state != BDR_LOCKSTATE_ACQUIRE_ACQUIRED);
		Assert(bdr_my_locks_database->lockcount > 0);
	}

	systable_endscan(scan);
	UnregisterSnapshot(snap);
	table_close(rel, NoLock);

	/*
	 * Note that it's not unexpected to receive release requests for locks
	 * this node hasn't acquired. We'll only get a release from a node that
	 * previously sent an acquire message, but if we rejected the acquire from
	 * that node we don't keep any record of the rejection.
	 *
	 * We might've rejected a lock because we hold a lock for another node
	 * already, in which case we'll still hold a lock throughout this call.
	 *
	 * Another cause is if we already committed removal of this lock locally
	 * but crashed before advancing the replication origin, so we replay it
	 * again on recovery.
	 */
	if (!found)
	{
		ereport(DEBUG1,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("did not find global lock entry locally for a remotely released global lock"),
				 errdetail("node " BDR_NODEID_FORMAT_WITHNAME " sent a release message but the lock isn't held locally.",
						   BDR_NODEID_FORMAT_WITHNAME_ARGS(*lock))));

		elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
			 LOCKTRACE "missing local lock entry for remotely released global lock from " BDR_NODEID_FORMAT_WITHNAME,
			 BDR_NODEID_FORMAT_WITHNAME_ARGS(*lock));

		/* nothing to unlock, if there's a lock it's owned by someone else */
		CommitTransactionCommand();
		MemoryContextSwitchTo(old_ctx);
		return;
	}

	LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);

	Assert(found);
	Assert(bdr_my_locks_database->lockcount > 0);

	latch = bdr_my_locks_database->requestor;

	/*
	 * We allow memory allocations in the following critical section for
	 * the sake of CommitTransactionCommand(), so, keep track of the memory
	 * context in which we did so. This is needed to correctly reset
	 * allowInCritSection flag as CommitTransactionCommand() changes the
	 * memory context.
	 */
	cs_mem_ctx = CurrentMemoryContext;
	MemoryContextAllowInCriticalSection(CurrentMemoryContext, true);

	/* Ensure that if on disk and shmem state diverge we crash and recover */
	START_CRIT_SECTION();

	CommitTransactionCommand();

	/* Let's reset allowInCritSection correctly */
	MemoryContextAllowInCriticalSection(cs_mem_ctx, false);
	MemoryContextSwitchTo(old_ctx);

	Assert(bdr_my_locks_database->lock_state == BDR_LOCKSTATE_NOLOCK);
	bdr_my_locks_database->lockcount--;
	bdr_my_locks_database->lock_holder = InvalidRepOriginId;
	bdr_my_locks_database->lock_type = BDR_LOCK_NOLOCK;
	bdr_my_locks_database->replay_confirmed = 0;
	bdr_my_locks_database->replay_confirmed_lsn = InvalidXLogRecPtr;
	bdr_my_locks_database->requestor = NULL;
	/* XXX: recheck owner of lock */

	END_CRIT_SECTION();

	Assert(bdr_my_locks_database->lockcount == 0);
	bdr_locks_on_unlock();

	LWLockRelease(bdr_locks_ctl->lock);

	elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
		 LOCKTRACE "global lock released locally");

	/* notify an eventual waiter */
	if (latch)
		SetLatch(latch);
}

/*
 * Another node has confirmed that a node has acquired the DDL lock
 * successfully. If the acquiring node was us, change shared memory state and
 * wake up the user backend that was trying to acquire the lock.
 *
 * Runs in the apply worker.
 */
void
bdr_process_confirm_ddl_lock(const BDRNodeId * const origin, const BDRNodeId * const lock,
							 BDRLockType lock_type)
{
	Latch	   *latch;

	if (!check_is_my_origin_node(origin))
		return;

	/* don't care if another database has gotten the lock */
	if (!check_is_my_node(lock))
		return;

	bdr_locks_find_my_database(false);

	if (bdr_my_locks_database->lock_type != lock_type)
	{
		elog(WARNING,
			 LOCKTRACE "received global lock confirmation with unexpected lock type (%d), waiting for (%d)",
			 lock_type, bdr_my_locks_database->lock_type);
		return;
	}

	LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);
	bdr_my_locks_database->acquire_confirmed++;
	latch = bdr_my_locks_database->requestor;

	elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
		 LOCKTRACE "received global lock confirmation number %d/%d from " BDR_NODEID_FORMAT_WITHNAME,
		 bdr_my_locks_database->acquire_confirmed, bdr_my_locks_database->nnodes,
		 BDR_NODEID_FORMAT_WITHNAME_ARGS(*origin));

	LWLockRelease(bdr_locks_ctl->lock);

	if (latch)
		SetLatch(latch);
}

/*
 * Another node has declined a lock. If it was a lock requested by us, change
 * shared memory state and wakeup the user backend that tried to acquire the
 * lock.
 *
 * Runs in the apply worker.
 */
void
bdr_process_decline_ddl_lock(const BDRNodeId * const origin, const BDRNodeId * const lock,
							 BDRLockType lock_type)
{
	Latch	   *latch;

	/* don't care if another database has been declined a lock */
	if (!check_is_my_origin_node(origin))
		return;

	bdr_locks_find_my_database(false);

	if (bdr_my_locks_database->lock_type != lock_type)
	{
		elog(WARNING,
			 LOCKTRACE "received global lock confirmation with unexpected lock type (%d) from " BDR_NODEID_FORMAT_WITHNAME ", waiting for (%d)",
			 lock_type, BDR_NODEID_FORMAT_WITHNAME_ARGS(*origin), bdr_my_locks_database->lock_type);
		return;
	}

	LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);
	bdr_my_locks_database->acquire_declined++;
	latch = bdr_my_locks_database->requestor;
	LWLockRelease(bdr_locks_ctl->lock);
	if (latch)
		SetLatch(latch);

	elog(ddl_lock_log_level(DDL_LOCK_TRACE_ACQUIRE_RELEASE),
		 LOCKTRACE "global lock request declined by node " BDR_NODEID_FORMAT_WITHNAME,
		 BDR_NODEID_FORMAT_WITHNAME_ARGS(*origin));
}

/*
 * Another node has asked us to confirm that we've replayed up to a given LSN.
 * We've seen the request message, so send the requested confirmation.
 *
 * Runs in the apply worker.
 */
void
bdr_process_request_replay_confirm(const BDRNodeId * const node, XLogRecPtr request_lsn)
{
	StringInfoData s;

	if (!check_is_my_origin_node(node))
		return;

	bdr_locks_find_my_database(false);

	elog(ddl_lock_log_level(DDL_LOCK_TRACE_PEERS),
		 LOCKTRACE "replay confirmation requested by node " BDR_NODEID_FORMAT_WITHNAME "; sending",
		 BDR_NODEID_FORMAT_WITHNAME_ARGS(*node));

	initStringInfo(&s);
	bdr_prepare_message(&s, BDR_MESSAGE_REPLAY_CONFIRM);
	pq_sendint64(&s, request_lsn);

	/*
	 * This is crash safe even though we don't update the replication origin
	 * and FlushDatabaseBuffers() before replying. The message written to WAL
	 * by bdr_send_message will not get decoded and sent by walsenders until
	 * it is flushed to disk.
	 */
	bdr_send_message(&s, false);
}


static void
bdr_send_confirm_lock(void)
{
	Relation	rel;
	SysScanDesc scan;
	Snapshot	snap;
	HeapTuple	tuple;

	BDRNodeId	replay;
	StringInfoData s;
	bool		found = false;
	MemoryContext old_ctx;

	initStringInfo(&s);

	Assert(LWLockHeldByMe(bdr_locks_ctl->lock));

	bdr_my_locks_database->replay_confirmed = 0;
	bdr_my_locks_database->replay_confirmed_lsn = InvalidXLogRecPtr;
	bdr_my_locks_database->requestor = NULL;

	bdr_prepare_message(&s, BDR_MESSAGE_CONFIRM_LOCK);

	/* ddl lock jumps straight past catchup, write lock must have done catchup */
	Assert(
		   (bdr_my_locks_database->lock_state == BDR_LOCKSTATE_PEER_BEGIN_CATCHUP && bdr_my_locks_database->lock_type == BDR_LOCK_DDL)
		   || (bdr_my_locks_database->lock_state == BDR_LOCKSTATE_PEER_CATCHUP && bdr_my_locks_database->lock_type == BDR_LOCK_WRITE));

	Assert(!IsTransactionState());
	old_ctx = CurrentMemoryContext;
	StartTransactionCommand();
	bdr_fetch_sysid_via_node_id(bdr_my_locks_database->lock_holder, &replay);

	bdr_send_nodeid(&s, &replay, false);
	pq_sendint(&s, bdr_my_locks_database->lock_type, 4);
	bdr_send_message(&s, true); /* transactional */

	/*
	 * Update state of lock. Do so in the same xact that confirms the lock.
	 * That way we're safe against crashes.
	 *
	 * This is safe even though we don't force a synchronous commit, because
	 * the message written to WAL by bdr_send_message will not get decoded and
	 * sent by walsenders until it is flushed.
	 */
	/* Scan for a matching lock whose state needs to be updated */
	snap = RegisterSnapshot(GetLatestSnapshot());
	rel = table_open(BdrLocksRelid, RowExclusiveLock);

	scan = locks_begin_scan(rel, snap, &replay);

	while ((tuple = systable_getnext(scan)) != NULL)
	{
		HeapTuple	newtuple;
		Datum		values[10];
		bool		isnull[10];

		if (found)
			elog(PANIC, "duplicate lock?");

		elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
			 LOCKTRACE "updating global lock state from 'catchup' to 'acquired'");

		heap_deform_tuple(tuple, RelationGetDescr(rel),
						  values, isnull);
		/* status column */
		isnull[9] = false;
		values[9] = CStringGetTextDatum("acquired");

		newtuple = heap_form_tuple(RelationGetDescr(rel),
								   values, isnull);
		/* simple_heap_update(rel, &tuple->t_self, newtuple); */
		bdr_locks_set_commit_pending_state(BDR_LOCKSTATE_PEER_CONFIRMED);
		CatalogTupleUpdate(rel, &tuple->t_self, newtuple);
		found = true;
	}

	if (!found)
		elog(PANIC, "got confirmation for unknown lock");

	systable_endscan(scan);
	UnregisterSnapshot(snap);
	table_close(rel, NoLock);

	CommitTransactionCommand();
	MemoryContextSwitchTo(old_ctx);
}

/*
 * A remote node has seen a replay confirmation request and replied to it.
 *
 * If we sent the original request, update local state appropriately.
 *
 * If a DDL lock request has reached quorum as a result of this confirmation,
 * write a log acquisition confirmation and bdr_global_locks update to xlog.
 *
 * Runs in the apply worker.
 */
void
bdr_process_replay_confirm(const BDRNodeId * const node, XLogRecPtr request_lsn)
{
	bool		quorum_reached = false;

	if (!check_is_my_origin_node(node))
		return;

	bdr_locks_find_my_database(false);

	LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);
	elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
		 LOCKTRACE "processing replay confirmation from node " BDR_NODEID_FORMAT_WITHNAME " for request %X/%X at %X/%X",
		 BDR_NODEID_FORMAT_WITHNAME_ARGS(*node),
		 LSN_FORMAT_ARGS(bdr_my_locks_database->replay_confirmed_lsn),
		 LSN_FORMAT_ARGS(request_lsn));

	/* request matches the one we're interested in */
	if (bdr_my_locks_database->replay_confirmed_lsn == request_lsn)
	{
		bdr_my_locks_database->replay_confirmed++;

		elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
			 LOCKTRACE "confirming replay %d/%d",
			 bdr_my_locks_database->replay_confirmed,
			 bdr_my_locks_database->nnodes);

		quorum_reached =
			bdr_my_locks_database->replay_confirmed >= bdr_my_locks_database->nnodes;
	}

	if (quorum_reached)
	{
		elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
			 LOCKTRACE "global lock quorum reached, logging confirmation of this node's acquisition of global lock");

		bdr_send_confirm_lock();

		elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
			 LOCKTRACE "sent confirmation of successful global lock acquisition");
	}

	LWLockRelease(bdr_locks_ctl->lock);
}

/*
 * A remote node has sent a startup message. Update any appropriate local state
 * like any locally held DDL locks for it.
 *
 * Runs in the apply worker.
 */
void
bdr_locks_process_remote_startup(const BDRNodeId * const node)
{
	Relation	rel;
	Snapshot	snap;
	SysScanDesc scan;
	HeapTuple	tuple;
	StringInfoData s;
	MemoryContext old_ctx;

	Assert(bdr_worker_type == BDR_WORKER_APPLY);

	bdr_locks_find_my_database(false);

	initStringInfo(&s);

	elog(ddl_lock_log_level(DDL_LOCK_TRACE_PEERS),
		 LOCKTRACE "got startup message from node " BDR_NODEID_FORMAT_WITHNAME ", clearing any locks it held",
		 BDR_NODEID_FORMAT_WITHNAME_ARGS(*node));

	old_ctx = CurrentMemoryContext;
	StartTransactionCommand();
	snap = RegisterSnapshot(GetLatestSnapshot());
	rel = table_open(BdrLocksRelid, RowExclusiveLock);

	scan = locks_begin_scan(rel, snap, node);

	while ((tuple = systable_getnext(scan)) != NULL)
	{
		elog(ddl_lock_log_level(DDL_LOCK_TRACE_DEBUG),
			 LOCKTRACE "found remote lock to delete (after remote restart)");

		simple_heap_delete(rel, &tuple->t_self);
		bdr_locks_set_commit_pending_state(BDR_LOCKSTATE_NOLOCK);

		LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);
		if (bdr_my_locks_database->lockcount == 0)
			elog(WARNING, "bdr_global_locks row exists without corresponding in memory state");
		else
		{
			Assert(bdr_my_locks_database->lock_state > BDR_LOCKSTATE_NOLOCK);
			bdr_my_locks_database->lockcount--;
			bdr_my_locks_database->lock_holder = InvalidRepOriginId;
			bdr_my_locks_database->lock_type = BDR_LOCK_NOLOCK;
			bdr_my_locks_database->replay_confirmed = 0;
			bdr_my_locks_database->replay_confirmed_lsn = InvalidXLogRecPtr;
		}

		if (bdr_my_locks_database->lockcount == 0)
			bdr_locks_on_unlock();

		LWLockRelease(bdr_locks_ctl->lock);
	}

	systable_endscan(scan);
	UnregisterSnapshot(snap);
	table_close(rel, NoLock);
	/* Lock the shmem control segment for the state change */
	LWLockAcquire(bdr_locks_ctl->lock, LW_EXCLUSIVE);
	CommitTransactionCommand();
	MemoryContextSwitchTo(old_ctx);
	LWLockRelease(bdr_locks_ctl->lock);
}

/*
 * Return true if a peer node holds or is acquiring the global DDL lock
 * according to our local state. Ignores locks of strength less than min_mode.
 * In other words, does any peer own our local ddl lock in any state,
 * in at least the specified mode?
 */
static bool
bdr_locks_peer_has_lock(BDRLockType min_mode)
{
	bool		lock_held_by_peer;

	Assert(LWLockHeldByMe(bdr_locks_ctl->lock));

	lock_held_by_peer = !this_xact_acquired_lock &&
		bdr_my_locks_database->lockcount > 0 &&
		bdr_my_locks_database->lock_type >= min_mode &&
		bdr_my_locks_database->lock_holder != InvalidRepOriginId;

	if (lock_held_by_peer)
	{
		Assert(bdr_my_locks_database->lock_state == BDR_LOCKSTATE_PEER_BEGIN_CATCHUP ||
			   bdr_my_locks_database->lock_state == BDR_LOCKSTATE_PEER_CANCEL_XACTS ||
			   bdr_my_locks_database->lock_state == BDR_LOCKSTATE_PEER_CATCHUP ||
			   bdr_my_locks_database->lock_state == BDR_LOCKSTATE_PEER_CONFIRMED);
	}
	else
	{
		/*
		 * If no peer holds the lock, it must be us, or unlocked, or the
		 * strength must be lower than requested.
		 */
		Assert(bdr_my_locks_database->lock_state == BDR_LOCKSTATE_NOLOCK ||
			   bdr_my_locks_database->lock_state == BDR_LOCKSTATE_ACQUIRE_TALLY_CONFIRMATIONS ||
			   bdr_my_locks_database->lock_state == BDR_LOCKSTATE_ACQUIRE_ACQUIRED ||
			   bdr_my_locks_database->lock_type < min_mode);
	}

	return lock_held_by_peer;
}

/*
 * Function for checking if there is no conflicting BDR lock.
 *
 * Should be caled from ExecutorStart_hook.
 */
void
bdr_locks_check_dml(void)
{
	bool		lock_held_by_peer;

	/* replace bdr_skip_ddl_locking by bdr_skip_ddl_replication for now */
	if (bdr_skip_ddl_replication)
		return;

	bdr_locks_find_my_database(false);

	/*
	 * The bdr is still starting up and hasn't loaded locks, wait for it. The
	 * statement_timeout will kill us if necessary.
	 */
	while (!bdr_my_locks_database->locked_and_loaded)
	{
		CHECK_FOR_INTERRUPTS();

		/* Probably can't use latch here easily, since init didn't happen yet. */
		pg_usleep(10000L);
	}

	/*
	 * Is this database locked against user initiated dml by another node?
	 *
	 * If the locker is our own node we can safely continue. Postgres's normal
	 * heavyweight locks will ensure consistency, and we'll replay changes in
	 * commit-order to peers so there's no ordering problem. It doesn't matter
	 * if we hold the lock or are still acquiring it; if we're acquiring and
	 * we fail to get the lock, another node that acquires our local lock will
	 * deal with any running xacts then.
	 */
	LWLockAcquire(bdr_locks_ctl->lock, LW_SHARED);
	lock_held_by_peer = bdr_locks_peer_has_lock(BDR_LOCK_WRITE);
	LWLockRelease(bdr_locks_ctl->lock);

	/*
	 * We can race against concurrent lock release here, but at worst we'll
	 * just wait a bit longer than needed.
	 */
	if (lock_held_by_peer)
	{
		TimestampTz canceltime;

		/*
		 * If we add a waiter after the lock is released we may get woken
		 * unnecessarily, but it won't do any harm.
		 */
		bdr_locks_addwaiter(MyProc);

		if (bdr_ddl_lock_timeout > 0 || LockTimeout > 0)
			canceltime = TimestampTzPlusMilliseconds(GetCurrentTimestamp(),
													 bdr_ddl_lock_timeout > 0 ? bdr_ddl_lock_timeout : LockTimeout);
		else
			TIMESTAMP_NOEND(canceltime);

		/* Wait for lock to be released. */
		for (;;)
		{
			int			rc;

			if (!TIMESTAMP_IS_NOEND(canceltime) &&
				GetCurrentTimestamp() < canceltime)
			{
				ereport(ERROR,
						(errcode(ERRCODE_LOCK_NOT_AVAILABLE),
						 errmsg("canceling statement due to global lock timeout")));
			}

			CHECK_FOR_INTERRUPTS();

			LWLockAcquire(bdr_locks_ctl->lock, LW_SHARED);
			lock_held_by_peer = bdr_locks_peer_has_lock(BDR_LOCK_WRITE);
			LWLockRelease(bdr_locks_ctl->lock);

			if (!lock_held_by_peer)
				break;

			rc = WaitLatch(&MyProc->procLatch,
						   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
						   10000L, PG_WAIT_EXTENSION);

			ResetLatch(&MyProc->procLatch);

			/* emergency bailout if postmaster has died */
			if (rc & WL_POSTMASTER_DEATH)
				proc_exit(1);
		}
	}
}

/* Lock type conversion functions */
char *
bdr_lock_type_to_name(BDRLockType lock_type)
{
	switch (lock_type)
	{
		case BDR_LOCK_NOLOCK:
			return "nolock";
		case BDR_LOCK_DDL:
			return "ddl_lock";
		case BDR_LOCK_WRITE:
			return "write_lock";
		default:
			elog(ERROR, "unknown lock type %d", lock_type);
	}
}

BDRLockType
bdr_lock_name_to_type(const char *lock_type)
{
	if (strcasecmp(lock_type, "nolock") == 0)
		return BDR_LOCK_NOLOCK;
	else if (strcasecmp(lock_type, "ddl_lock") == 0)
		return BDR_LOCK_DDL;
	else if (strcasecmp(lock_type, "write_lock") == 0)
		return BDR_LOCK_WRITE;
	else
		elog(ERROR, "unknown lock type %s", lock_type);
}

/* Lock type conversion functions */
static char *
bdr_lock_state_to_name(BDRLockState lock_state)
{
	switch (lock_state)
	{
		case BDR_LOCKSTATE_NOLOCK:
			return "nolock";
		case BDR_LOCKSTATE_ACQUIRE_TALLY_CONFIRMATIONS:
			return "acquire_tally_confirmations";
		case BDR_LOCKSTATE_ACQUIRE_ACQUIRED:
			return "acquire_acquired";
		case BDR_LOCKSTATE_PEER_BEGIN_CATCHUP:
			/* should be so short lived nobody sees it, but eh */
			return "peer_begin_catchup";
		case BDR_LOCKSTATE_PEER_CANCEL_XACTS:
			return "peer_cancel_xacts";
		case BDR_LOCKSTATE_PEER_CATCHUP:
			return "peer_catchup";
		case BDR_LOCKSTATE_PEER_CONFIRMED:
			return "peer_confirmed";

		default:
			elog(ERROR, "unknown lock state %d", lock_state);
	}
}

Datum
bdr_get_global_locks_info(PG_FUNCTION_ARGS)
{
#define BDR_DDL_LOCK_INFO_NFIELDS 13
	BdrLocksDBState state;
	BDRNodeId	locknodeid,
				myid;
	char		sysid_str[33];
	Datum		values[BDR_DDL_LOCK_INFO_NFIELDS];
	bool		isnull[BDR_DDL_LOCK_INFO_NFIELDS];
	TupleDesc	tupleDesc;
	HeapTuple	returnTuple;
	int			field;

	if (get_call_result_type(fcinfo, NULL, &tupleDesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	if (!bdr_is_bdr_activated_db(MyDatabaseId))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("bdr is not active in this database")));

	bdr_locks_find_my_database(false);

	LWLockAcquire(bdr_locks_ctl->lock, LW_SHARED);
	memcpy(&state, bdr_my_locks_database, sizeof(BdrLocksDBState));
	LWLockRelease(bdr_locks_ctl->lock);

	if (!state.in_use)
		/* shouldn't happen */
		elog(ERROR, "bdr active but lockstate not configured");

	bdr_make_my_nodeid(&myid);

	/* fields: */
	memset(&values, 0, sizeof(values));
	memset(&isnull, 0, sizeof(isnull));
	field = 0;

	/* owner_replorigin, owner_sysid, owner_timeline and dboid, lock_type */
	if (state.lockcount > 0)
	{
		/*
		 * While we don't strictly need to map the reporigin to node identity,
		 * doing so here saves the user from having to parse the reporigin
		 * name and map it to bdr.bdr_nodes to get the node name.
		 */
		values[field++] = ObjectIdGetDatum(state.lock_holder);
		if (bdr_fetch_sysid_via_node_id_ifexists(state.lock_holder, &locknodeid, true))
		{
			snprintf(sysid_str, sizeof(sysid_str), UINT64_FORMAT, locknodeid.sysid);
			values[field++] = CStringGetTextDatum(sysid_str);
			values[field++] = ObjectIdGetDatum(locknodeid.timeline);
			values[field++] = ObjectIdGetDatum(locknodeid.dboid);
		}
		else
		{
			elog(WARNING, "lookup of replication origin %d failed",
				 state.lock_holder);
			isnull[field++] = true;
			isnull[field++] = true;
			isnull[field++] = true;
		}
		values[field++] = CStringGetTextDatum(bdr_lock_type_to_name(state.lock_type));
	}
	else
	{
		int			end;

		for (end = field + 5; field < end; field++)
			isnull[field] = true;
	}

	/* lock_state */
	values[field++] = CStringGetTextDatum(bdr_lock_state_to_name(state.lock_state));

	/* record locking backend pid if we're the locking node */
	values[field] = Int32GetDatum(state.lock_holder_local_pid);
	isnull[field++] = bdr_nodeid_eq(&myid, &locknodeid);

	/*
	 * Finer grained info, may be subject to change:
	 *
	 * npeers, npeers_confirmed, npeers_declined, npeers_replayed, replay_upto
	 *
	 * These reflect shmem state directly; no checking for whether we're
	 * locker etc.
	 *
	 * Note that the counters get cleared once the current operation is
	 * finished, so you'll rarely if ever see nnodes = acquire_confirmed for
	 * example.
	 */
	values[field++] = Int32GetDatum(state.lockcount);
	values[field++] = Int32GetDatum(state.nnodes);
	values[field++] = Int32GetDatum(state.acquire_confirmed);
	values[field++] = Int32GetDatum(state.acquire_declined);
	values[field++] = Int32GetDatum(state.replay_confirmed);
	if (state.replay_confirmed_lsn != InvalidXLogRecPtr)
		values[field++] = LSNGetDatum(state.replay_confirmed_lsn);
	else
		isnull[field++] = true;

	Assert(field == BDR_DDL_LOCK_INFO_NFIELDS);

	returnTuple = heap_form_tuple(tupleDesc, values, isnull);
	PG_RETURN_DATUM(HeapTupleGetDatum(returnTuple));
}
