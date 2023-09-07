/* -------------------------------------------------------------------------
 *
 * pgactive_perdb.c
 *		Per database supervisor worker.
 *
 * Copyright (C) 2014-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_perdb.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pgactive.h"
#include "pgactive_locks.h"

#include "miscadmin.h"
#include "pgstat.h"

#include "access/xact.h"

#include "catalog/pg_type.h"

#include "commands/dbcommands.h"

#include "executor/spi.h"

#include "postmaster/bgworker.h"

#include "lib/stringinfo.h"

/* For struct Port only! */
#include "libpq/libpq-be.h"

#include "replication/origin.h"

#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/proc.h"

#include "utils/builtins.h"
#include "utils/elog.h"
#include "utils/guc.h"
#include "utils/memutils.h"
#include "utils/snapmgr.h"
#include "utils/regproc.h"

PG_FUNCTION_INFO_V1(pgactive_connections_changed);

/* In the commit hook, should we attempt to start a per-db worker? */
static bool xacthook_registered = false;
static bool xacthook_connections_changed = false;

static bool is_perdb_worker = true;

static void check_params_are_same(void);

bool
IspgactivePerdbWorker(void)
{
	return is_perdb_worker;
}

/*
 * Scan shmem looking for a perdb worker for the named DB and
 * return its offset. If not found, return -1.
 *
 * Must hold the LWLock on the worker control segment in at
 * least share mode.
 *
 * Note that there's no guarantee that the worker is actually
 * started up.
 */
int
find_perdb_worker_slot(Oid dboid, pgactiveWorker * *worker_found)
{
	int			i,
				found = -1;

	Assert(LWLockHeldByMe(pgactiveWorkerCtl->lock));

	for (i = 0; i < pgactive_max_workers; i++)
	{
		pgactiveWorker *w = &pgactiveWorkerCtl->slots[i];

		if (w->worker_type == pgactive_WORKER_PERDB)
		{
			pgactivePerdbWorker *pw = &w->data.perdb;

			if (pw->p_dboid == dboid)
			{
				found = i;
				if (worker_found != NULL)
					*worker_found = w;
				break;
			}
		}
	}

	return found;
}

/*
 * Scan shmem looking for an apply worker for the current perdb worker and
 * specified target node identifier and return its offset. If not found, return
 * -1.
 *
 * Must hold the LWLock on the worker control segment in at least share mode.
 *
 * Note that there's no guarantee that the worker is actually started up.
 */
int
find_apply_worker_slot(const pgactiveNodeId * const remote, pgactiveWorker * *worker_found)
{
	int			i,
				found = -1;

	Assert(LWLockHeldByMe(pgactiveWorkerCtl->lock));

	for (i = 0; i < pgactive_max_workers; i++)
	{
		pgactiveWorker *w = &pgactiveWorkerCtl->slots[i];

		if (w->worker_type == pgactive_WORKER_APPLY)
		{
			pgactiveApplyWorker *aw = &w->data.apply;

			if (aw->dboid == MyDatabaseId &&
				pgactive_nodeid_eq(&aw->remote_node, remote))
			{
				found = i;
				if (worker_found != NULL)
					*worker_found = w;
				break;
			}
		}
	}

	return found;
}

static void
pgactive_perdb_xact_callback(XactEvent event, void *arg)
{
	switch (event)
	{
		case XACT_EVENT_COMMIT:
			if (xacthook_connections_changed)
			{
				int			slotno;
				pgactiveWorker *w;

				xacthook_connections_changed = false;

				LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);

				/*
				 * If a perdb worker already exists, wake it and tell it to
				 * check for new connections.
				 */
				slotno = find_perdb_worker_slot(MyDatabaseId, &w);
				if (slotno >= 0)
				{
					/*
					 * The worker is registered, but might not be started yet
					 * (or could be crashing and restarting). If it's not
					 * started the latch will be zero. If it's started but
					 * dead, the latch will be bogus, but it's safe to set a
					 * proclatch to a dead process. At worst we'll set a latch
					 * for the wrong process, and that's fine. If it's zero
					 * then the worker is still starting and will see our new
					 * changes anyway.
					 */
					if (w->data.perdb.proclatch != NULL)
						SetLatch(w->data.perdb.proclatch);
				}
				else
				{
					/*
					 * Per-db worker doesn't exist, ask the supervisor to
					 * check for changes and register new per-db workers for
					 * labeled databases.
					 */
					if (pgactiveWorkerCtl->supervisor_latch)
						SetLatch(pgactiveWorkerCtl->supervisor_latch);
				}

				LWLockRelease(pgactiveWorkerCtl->lock);
			}
			break;
		default:
			/* We're not interested in other tx events */
			break;
	}
}

/*
 * Prepare to launch a perdb worker for the current DB if it's not already
 * running, and register a XACT_EVENT_COMMIT hook to perform the actual launch
 * when the addition of the worker commits.
 *
 * If a perdb worker is already running, notify it to check for new
 * connections.
 */
Datum
pgactive_connections_changed(PG_FUNCTION_ARGS)
{
	if (!xacthook_registered)
	{
		RegisterXactCallback(pgactive_perdb_xact_callback, NULL);
		xacthook_registered = true;
	}
	xacthook_connections_changed = true;
	PG_RETURN_VOID();
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
 * Launch a dynamic bgworker to run pgactive_apply_main for each pgactive connection on
 * the database identified by dbname.
 *
 * Scans the pgactive.pgactive_connections table for workers and launch a worker for any
 * connection that doesn't already have one.
 */
void
pgactive_maintain_db_workers(void)
{
	BackgroundWorker bgw = {0};
	int			i,
				ret;
	int			nnodes = 0;
#define pgactive_CON_Q_NARGS 3
	Oid			argtypes[pgactive_CON_Q_NARGS] = {TEXTOID, OIDOID, OIDOID};
	Datum		values[pgactive_CON_Q_NARGS];
	char		sysid_str[33];
	char		our_status;
	pgactiveNodeId myid;
	List	   *detached_nodes = NIL;
	List	   *nodes_to_forget = NIL;
	List	   *rep_origin_to_remove = NIL;
	ListCell   *lcdetached;
	ListCell   *lcforget;
	ListCell   *lcroname;
	bool		at_least_one_worker_terminated = false;

	pgactive_make_my_nodeid(&myid);

	/* Should be called from the perdb worker */
	Assert(IsBackgroundWorker);
	Assert(pgactive_worker_type == pgactive_WORKER_PERDB);

	Assert(!LWLockHeldByMe(pgactiveWorkerCtl->lock));

	if (pgactiveWorkerCtl->worker_management_paused)
	{
		/*
		 * We're going to ignore this worker update check by request (used
		 * mainly for testing). We'll notice changes when our latch is next
		 * set.
		 */
		return;
	}

	snprintf(sysid_str, sizeof(sysid_str), UINT64_FORMAT, myid.sysid);

	elog(DEBUG2, "launching apply workers");

	/*
	 * It's easy enough to make this tolerant of an open tx, but in general
	 * rollback doesn't make sense here.
	 */
	Assert(!IsTransactionState());

	/* Configure apply worker */
	bgw.bgw_flags = BGWORKER_SHMEM_ACCESS |
		BGWORKER_BACKEND_DATABASE_CONNECTION;
	bgw.bgw_start_time = BgWorkerStart_RecoveryFinished;
	snprintf(bgw.bgw_library_name, BGW_MAXLEN, pgactive_LIBRARY_NAME);
	snprintf(bgw.bgw_function_name, BGW_MAXLEN, "pgactive_apply_main");
	snprintf(bgw.bgw_type, BGW_MAXLEN, "pgactive apply worker");
	bgw.bgw_restart_time = 5;

	StartTransactionCommand();
	SPI_connect();
	PushActiveSnapshot(GetTransactionSnapshot());

	our_status = pgactive_nodes_get_local_status(&myid, false);

	/*
	 * First check whether any existing processes to/from this database need
	 * to be killed off because of the node status.
	 *
	 * We have three main states for nodes being removed: 'p'arting, 'P'arted,
	 * and 'k'illed.
	 */
	ret = SPI_execute(
					  "SELECT node_sysid, node_timeline, node_dboid\n"
					  "FROM pgactive.pgactive_nodes\n"
					  "WHERE pgactive_nodes.node_status = " pgactive_NODE_STATUS_KILLED_S,
					  false, 0);

	if (ret != SPI_OK_SELECT)
		elog(ERROR, "SPI error while querying pgactive.pgactive_nodes");

	/*
	 * We may want to use the SPI within the loop that processes detached
	 * nodes, so copy the matched list of node IDs.
	 */

	for (i = 0; i < SPI_processed; i++)
	{
		/*
		 * If the connection is dead, iterate over all shem slots and kill
		 * everything using that slot.
		 */
		HeapTuple	tuple;
		pgactiveNodeId *node;
		char	   *node_sysid_s;
		MemoryContext oldcontext;

		bool		isnull;

		tuple = SPI_tuptable->vals[i];

		oldcontext = MemoryContextSwitchTo(TopMemoryContext);
		node = palloc(sizeof(pgactiveNodeId));
		MemoryContextSwitchTo(oldcontext);

		node_sysid_s = SPI_getvalue(tuple, SPI_tuptable->tupdesc, pgactive_NODES_ATT_SYSID);

		if (sscanf(node_sysid_s, UINT64_FORMAT, &node->sysid) != 1)
			elog(ERROR, "parsing sysid uint64 from %s failed", node_sysid_s);

		node->timeline = DatumGetObjectId(
										  SPI_getbinval(tuple, SPI_tuptable->tupdesc, pgactive_NODES_ATT_TIMELINE,
														&isnull));
		Assert(!isnull);

		node->dboid = DatumGetObjectId(
									   SPI_getbinval(tuple, SPI_tuptable->tupdesc, pgactive_NODES_ATT_DBOID,
													 &isnull));
		Assert(!isnull);

		detached_nodes = lappend(detached_nodes, (void *) node);
	}

	/*
	 * Terminate worker processes and, where possible, drop slots for detached
	 * peers.
	 */
	foreach(lcdetached, detached_nodes)
	{
		pgactiveNodeId *node = lfirst(lcdetached);
		bool		found_alive = false;
		int			slotoff;

		LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);
		for (slotoff = 0; slotoff < pgactive_max_workers; slotoff++)
		{
			pgactiveWorker *w = &pgactiveWorkerCtl->slots[slotoff];
			bool		kill_proc = false;

			/* unused slot */
			if (w->worker_type == pgactive_WORKER_EMPTY_SLOT)
				continue;

			/* not directly linked to a peer */
			if (w->worker_type == pgactive_WORKER_PERDB)
				continue;

			/* unconnected slot */
			if (w->worker_proc == NULL)
				continue;

			if (w->worker_type == pgactive_WORKER_APPLY)
			{
				pgactiveApplyWorker *apply = &w->data.apply;

				/*
				 * Kill apply workers either if they're running on the
				 * to-be-killed node or connecting to it.
				 */
				if (our_status == pgactive_NODE_STATUS_KILLED && w->worker_proc->databaseId == node->dboid)
				{
					/*
					 * NB: It's sufficient to check the database oid, the
					 * others have to be the same
					 */
					kill_proc = true;
				}
				else if (pgactive_nodeid_eq(&apply->remote_node, node))
					kill_proc = true;
			}
			else if (w->worker_type == pgactive_WORKER_WALSENDER)
			{
				pgactiveWalsenderWorker *walsnd = &w->data.walsnd;

				if (our_status == pgactive_NODE_STATUS_KILLED && w->worker_proc->databaseId == node->dboid)
					kill_proc = true;
				else if (pgactive_nodeid_eq(&walsnd->remote_node, node))
					kill_proc = true;
			}
			else
			{
				/* unreachable */
				elog(WARNING, "unrecognised worker type %u", w->worker_type);
			}

			if (kill_proc)
			{
				found_alive = true;

				elog(DEBUG1, "need to terminate process for detached node: pid %u type: %u",
					 w->worker_pid, w->worker_type);
				kill(w->worker_pid, SIGTERM);
			}
		}
		LWLockRelease(pgactiveWorkerCtl->lock);

		if (found_alive)
		{
			at_least_one_worker_terminated = true;

			/*
			 * and treat as still alive for DDL locking purposes, since if it
			 * holds the ddl lock we might still have pending xacts from it
			 */
			nnodes++;
		}
		else
		{
			List	   *drop = NIL;
			ListCell   *dc;
			bool		we_were_dropped;
			NameData	slot_name_dropped;	/* slot of the dropped node */
			MemoryContext oldcontext;

			/*
			 * If a remote node (got) detached, we can easily drop their slot.
			 * If the local node was dropped, we instead drop all slots for
			 * peer nodes.
			 */
			pgactive_slot_name(&slot_name_dropped, node, myid.dboid);

			we_were_dropped = pgactive_nodeid_eq(node, &myid);

			LWLockAcquire(ReplicationSlotControlLock, LW_SHARED);
			for (i = 0; i < max_replication_slots; i++)
			{
				ReplicationSlot *s = &ReplicationSlotCtl->replication_slots[i];

				if (!s->in_use)
					continue;

				if (strcmp("pgactive", NameStr(s->data.plugin)) != 0)
					continue;

				if (we_were_dropped &&
					s->data.database == myid.dboid)
				{
					elog(DEBUG1, "need to drop slot %s as we got detached",
						 NameStr(s->data.name));
					drop = lappend(drop, pstrdup(NameStr(s->data.name)));
				}

				else if (strcmp(NameStr(s->data.name),
								NameStr(slot_name_dropped)) == 0)
				{
					elog(DEBUG1, "need to drop slot %s of detached node %s",
						 NameStr(s->data.name),
						 pgactive_nodeid_name(node, true, false));
					drop = lappend(drop, pstrdup(NameStr(s->data.name)));
				}
			}
			LWLockRelease(ReplicationSlotControlLock);

			foreach(dc, drop)
			{
				char	   *slot_name = (char *) lfirst(dc);

				elog(DEBUG1, "dropping slot %s due to node detach", slot_name);
				ReplicationSlotDrop(slot_name, true);
				elog(LOG, "dropped slot %s due to node detach", slot_name);

				if (!we_were_dropped)
				{
					char		roname[256];

					snprintf(roname, sizeof(roname), pgactive_REPORIGIN_ID_FORMAT,
							 node->sysid, node->timeline, node->dboid, myid.dboid,
							 EMPTY_REPLICATION_NAME);

					oldcontext = MemoryContextSwitchTo(TopMemoryContext);
					rep_origin_to_remove = lappend(rep_origin_to_remove, roname);
					MemoryContextSwitchTo(oldcontext);
				}
			}

			oldcontext = MemoryContextSwitchTo(TopMemoryContext);
			nodes_to_forget = lappend(nodes_to_forget, (void *) node);
			MemoryContextSwitchTo(oldcontext);
		}
	}

	/*
	 * If at least one worker was found alive and killed in the above for
	 * loop, we check again next time for dropping replication slots of the
	 * detached peers. However, we want to do this soon, so setting the latch
	 * ensures the per-db worker doesn't go into long wait in its main loop.
	 * And, we set the latch specifically after ReplicationSlotDrop() call in
	 * the above for loop, because it can get reset by
	 * ConditionVariablePrepareToSleep() or ConditionVariableSleep() (called
	 * via ReplicationSlotDrop() -> ReplicationSlotAcquire()) making per-db
	 * worker go into long wait.
	 */
	if (at_least_one_worker_terminated)
		SetLatch(&MyProc->procLatch);

	PopActiveSnapshot();
	SPI_finish();

	/*
	 * The node cache needs to be invalidated as pgactive_nodes may have
	 * changed
	 */
	pgactive_nodecache_invalidate();
	CommitTransactionCommand();

	foreach(lcforget, nodes_to_forget)
	{
		pgactiveNodeId *node = lfirst(lcforget);

		/*
		 * If this node held the global DDL lock, purge it. We can no longer
		 * replicate changes from it so doing so is safe, it can never release
		 * the lock, and we'll otherwise be unable to recover.
		 */
		pgactive_locks_node_detached(node);

		/*
		 * TODO: if we leave it at 'k' we'll keep on re-checking it over and
		 * over. But for now that's what we do.
		 *
		 * We could set the node as 'dead'. This is a local state, since it
		 * could still be detaching on other nodes. So we shouldn't just
		 * update pgactive_nodes, we'd have to do a non-replicated update in a
		 * replicated table and it'd be ugly. We'll need a side-table for
		 * local node state.
		 *
		 * Or we could delete the row locally. We're eventually consistent
		 * anyway, right? We'd have to do that with do_not_replicate set.
		 */
	}

	/*
	 * Now we can remove replication origins linked to detached node(s) (if
	 * any).
	 */
	StartTransactionCommand();
	PushActiveSnapshot(GetTransactionSnapshot());
	foreach(lcroname, rep_origin_to_remove)
	{
#if PG_VERSION_NUM < 140000
		RepOriginId roident;
#endif
		char	   *roname = (char *) lfirst(lcroname);

		/*
		 * Replication origins removal should not be allowed if
		 * RecoveryInProgress() but we don't do this extra check as
		 * RecoveryInProgress() is not possible here. Indeed, see the
		 * RecoveryInProgress() test in pgactive_supervisor_worker_main().
		 */
		elog(DEBUG1, "dropping replication origin %s due to node detach", roname);
#if PG_VERSION_NUM < 140000
		roident = replorigin_by_name(roname, true);
		if (roident != InvalidRepOriginId)
		{
			replorigin_drop(roident, true);
			elog(LOG, "dropped replication origin %s due to node detach", roname);
		}
#else
		replorigin_drop_by_name(roname, true, true);
		elog(LOG, "dropped replication origin %s due to node detach", roname);
#endif

	}
	PopActiveSnapshot();
	CommitTransactionCommand();

	list_free_deep(nodes_to_forget);

	/* If our own node is dead, don't start new connections to other nodes */
	if (our_status == pgactive_NODE_STATUS_KILLED)
	{
		elog(LOG, "this node has been detached, not starting connections");
		goto out;
	}

	StartTransactionCommand();
	SPI_connect();
	PushActiveSnapshot(GetTransactionSnapshot());

	/*
	 * Look up connection entries for all nodes other than our own.
	 *
	 * If an entry with our origin (sysid,tlid,dboid) exists, treat that as
	 * overriding the generic one.
	 *
	 * Connections with no corresponding nodes entry will be ignored (excluded
	 * by the join).
	 */
	values[0] = CStringGetTextDatum(sysid_str);
	values[1] = ObjectIdGetDatum(myid.timeline);
	values[2] = ObjectIdGetDatum(myid.dboid);

	ret = SPI_execute_with_args(
								"SELECT DISTINCT ON (conn_sysid, conn_timeline, conn_dboid) "
								"  conn_sysid, conn_timeline, conn_dboid, node_status "
								"FROM pgactive.pgactive_connections "
								"    JOIN pgactive.pgactive_nodes ON ("
								"          conn_sysid = node_sysid AND "
								"          conn_timeline = node_timeline AND "
								"          conn_dboid = node_dboid "
								"    )"
								"WHERE NOT ( "
								"          conn_sysid = $1 AND "
								"          conn_timeline = $2 AND "
								"          conn_dboid = $3) "
								"ORDER BY conn_sysid, conn_timeline, conn_dboid, "
								"         conn_timeline ASC NULLS LAST, "
								"         conn_dboid ASC NULLS LAST ",
								pgactive_CON_Q_NARGS, argtypes, values, NULL,
								false, 0);

	if (ret != SPI_OK_SELECT)
		elog(ERROR, "SPI error while querying pgactive.pgactive_connections");

	for (i = 0; i < SPI_processed; i++)
	{
		BackgroundWorkerHandle *bgw_handle;
		HeapTuple	tuple;
		uint32		slot;
		uint32		worker_arg;
		pgactiveWorker *worker;
		pgactiveApplyWorker *apply;
		Datum		temp_datum;
		bool		isnull;
		pgactiveNodeId target;
		char	   *tmp_sysid;
		bool		origin_is_my_id;
		pgactiveNodeStatus node_status;

		tuple = SPI_tuptable->vals[i];

		tmp_sysid = SPI_getvalue(tuple, SPI_tuptable->tupdesc,
								 getattno("conn_sysid"));

		if (sscanf(tmp_sysid, UINT64_FORMAT, &target.sysid) != 1)
			elog(ERROR, "parsing sysid uint64 from %s failed", tmp_sysid);

		temp_datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc,
								   getattno("conn_timeline"),
								   &isnull);
		Assert(!isnull);
		target.timeline = DatumGetObjectId(temp_datum);

		temp_datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc,
								   getattno("conn_dboid"),
								   &isnull);
		Assert(!isnull);
		target.dboid = DatumGetObjectId(temp_datum);

		origin_is_my_id = false;

		temp_datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc,
								   getattno("node_status"),
								   &isnull);
		Assert(!isnull);
		node_status = DatumGetChar(temp_datum);

		elog(DEBUG1, "found pgactive_connections entry for " pgactive_NODEID_FORMAT " (origin specific: %d, status: %c)",
			 pgactive_NODEID_FORMAT_ARGS(target),
			 (int) origin_is_my_id, node_status);

		if (node_status == pgactive_NODE_STATUS_KILLED)
		{
			elog(DEBUG2, "skipping registration of conn as killed");
			continue;
		}

		/*
		 * We're only interested in counting 'r'eady nodes since nodes that're
		 * still coming up don't participate in DDL locking etc.
		 *
		 * It's OK to count it even if the apply worker doesn't exist right
		 * now or there's no incoming walsender yet.  For the node to have
		 * entered 'r'eady state we must've already successfully created slots
		 * on it, and it on us, so we're going to successfully exchange DDL
		 * lock messages etc when we get our workers sorted out.
		 */
		if (node_status == pgactive_NODE_STATUS_READY)
			nnodes++;

		LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);

		/*
		 * Is there already a worker registered for this connection?
		 */
		if (find_apply_worker_slot(&target, &worker) != -1)
		{
			elog(DEBUG2, "skipping registration of worker for node " pgactive_NODEID_FORMAT " on db oid=%u: already registered",
				 pgactive_NODEID_FORMAT_ARGS(target), myid.dboid);

			/*
			 * Notify the worker that its config could have changed.
			 *
			 * The latch is assigned after the worker starts, so it might be
			 * unset if the worker slot was created but it's still in early
			 * startup. If that's the case it hasn't read its config yet
			 * anyway, so we don't have to set the latch.
			 */
			if (worker->data.apply.proclatch != NULL)
				SetLatch(worker->data.apply.proclatch);

			LWLockRelease(pgactiveWorkerCtl->lock);
			continue;
		}

		/* We're going to register a new worker for this connection */

		/* Set the display name in 'ps' etc */
		snprintf(bgw.bgw_name, BGW_MAXLEN,
				 "pgactive apply worker for %s to %s",
				 pgactive_nodeid_name(&target, true, false),
				 pgactive_nodeid_name(&myid, true, false));

		/* Allocate a new shmem slot for this apply worker */
		worker = pgactive_worker_shmem_alloc(pgactive_WORKER_APPLY, &slot);

		/* Tell the apply worker what its shmem slot is */
		Assert(slot <= UINT16_MAX);
		worker_arg = (((uint32) pgactiveWorkerCtl->worker_generation) << 16) | (uint32) slot;
		bgw.bgw_main_arg = Int32GetDatum(worker_arg);

		/*
		 * Apply workers (other than in catchup mode, which are registered
		 * elsewhere) should not be using the local node's connection entry.
		 */
		Assert(!pgactive_nodeid_eq(&target, &myid));

		/* Now populate the apply worker state */
		apply = &worker->data.apply;
		apply->dboid = MyDatabaseId;
		pgactive_nodeid_cpy(&apply->remote_node, &target);
		apply->replay_stop_lsn = InvalidXLogRecPtr;
		apply->forward_changesets = false;
		apply->perdb = pgactive_worker_slot;
		LWLockRelease(pgactiveWorkerCtl->lock);

		/*
		 * Finally, register the worker for launch.
		 */
		if (!RegisterDynamicBackgroundWorker(&bgw,
											 &bgw_handle))
		{
			/*
			 * Already-registered workers will keep on running.  We need to
			 * make sure the slot we just acquired but failed to launch a
			 * worker for gets released again though.
			 */
			LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);
			apply->dboid = InvalidOid;
			apply->remote_node.sysid = 0;
			apply->remote_node.timeline = 0;
			apply->remote_node.dboid = InvalidOid;
			worker->worker_type = pgactive_WORKER_EMPTY_SLOT;
			LWLockRelease(pgactiveWorkerCtl->lock);

			ereport(ERROR,
					(errmsg("failed to register apply worker for " pgactive_NODEID_FORMAT,
							pgactive_NODEID_FORMAT_ARGS(target))));
		}
		else
		{
			elog(DEBUG2, "registered apply worker for " pgactive_NODEID_FORMAT,
				 pgactive_NODEID_FORMAT_ARGS(target));
		}
	}

	PopActiveSnapshot();
	SPI_finish();

	/*
	 * The node cache needs to be invalidated as pgactive_nodes may have
	 * changed
	 */
	pgactive_nodecache_invalidate();
	CommitTransactionCommand();

out:

	elog(DEBUG2, "done registering apply workers");

	/*
	 * Now we need to tell the lock manager about the changed node count.
	 *
	 * Now that node join takes the DDL lock and detach is careful to wait
	 * until it completes, the node count should only change when it's safe.
	 * In particular it should only go up when the DDL lock is held.
	 */
	pgactive_worker_slot->data.perdb.nnodes = nnodes;
	pgactive_locks_set_nnodes(nnodes);

	elog(DEBUG2, "updated worker counts");
}

/*
 * Check whether the local node and one remote node have same
 * pgactive.max_nodes and pgactive.skip_ddl_replication GUC values while ensuring
 * error cleanup.
 */
static void
check_params_ensure_error_cleanup(PGconn *conn, bool *check_done)
{
	PG_ENSURE_ERROR_CLEANUP(pgactive_cleanup_conn_close,
							PointerGetDatum(&conn));
	{
		struct remote_node_info ri;

		pgactive_get_remote_nodeinfo_internal(conn, &ri);

		if (pgactive_max_nodes != ri.max_nodes)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("pgactive.max_nodes parameter value (%d) on local node " pgactive_NODEID_FORMAT_WITHNAME " doesn't match with remote node (%d)",
							pgactive_max_nodes,
							pgactive_LOCALID_FORMAT_WITHNAME_ARGS,
							ri.max_nodes),
					 errhint("The parameter must be set to the same value on all pgactive members.")));

		if (prev_pgactive_skip_ddl_replication != ri.skip_ddl_replication)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("pgactive.skip_ddl_replication parameter value (%s) on local node " pgactive_NODEID_FORMAT_WITHNAME " doesn't match with remote node (%s)",
							prev_pgactive_skip_ddl_replication ? "true" : "false",
							pgactive_LOCALID_FORMAT_WITHNAME_ARGS,
							ri.skip_ddl_replication ? "true" : "false"),
					 errhint("The parameter must be set to the same value on all pgactive members.")));

		free_remote_node_info(&ri);
		*check_done = true;
	}
	PG_END_ENSURE_ERROR_CLEANUP(pgactive_cleanup_conn_close,
								PointerGetDatum(&conn));
}

/*
 * Check whether the local node and one remote node have same
 * pgactive.max_nodes and pgactive.skip_ddl_replication GUC values.
 *
 * If remote nodes exist and none is available to check the values
 * against then error out with FATAL (per-db worker will keep re-trying).
 *
 * Once a remote node is available, if their values differ then let's
 * not proceed further.
 */
static void
check_params_are_same(void)
{
	MemoryContext saved_ctx;
	List	   *all_local_dsn;
	ListCell   *lc;
	bool		check_done = false;
	bool		empty_list = false;

	while (!check_done && !empty_list)
	{
		StartTransactionCommand();
		saved_ctx = MemoryContextSwitchTo(TopMemoryContext);
		all_local_dsn = pgactive_get_all_local_dsn();
		MemoryContextSwitchTo(saved_ctx);
		empty_list = true;

		foreach(lc, all_local_dsn)
		{
			char	   *dsn = (char *) lfirst(lc);
			PGconn	   *conn;

			empty_list = false;

			conn = pgactive_connect_nonrepl(dsn,
											"pgactivenodeinfo", false);

			if (PQstatus(conn) != CONNECTION_OK)
				continue;

			check_params_ensure_error_cleanup(conn, &check_done);

			PQfinish(conn);
			/* no need to check against other remote nodes */
			if (check_done)
				break;
		}

		CommitTransactionCommand();
		list_free(all_local_dsn);

		if (!check_done && !empty_list)
		{
			ereport(FATAL,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("local node " pgactive_NODEID_FORMAT_WITHNAME " is not able to connect to any remote node to compare its parameters with",
							pgactive_LOCALID_FORMAT_WITHNAME_ARGS),
					 errhint("Ensure one remote node is connectable from the local node.")));
		}
	}
}

/*
 * Each database with pgactive enabled on it has a static background worker,
 * registered at shared_preload_libraries time during postmaster start. This is
 * the entry point for these bgworkers.
 *
 * This worker handles pgactive startup on the database and launches apply workers
 * for each pgactive connection.
 *
 * Since the worker is fork()ed from the postmaster, all globals initialised in
 * _PG_init remain valid.
 *
 * This worker can use the SPI and shared memory.
 */
void
pgactive_perdb_worker_main(Datum main_arg)
{
	int			rc = 0;
	pgactivePerdbWorker *perdb;
	StringInfoData si;
	pgactiveNodeId myid;

	is_perdb_worker = true;

	initStringInfo(&si);

	pgactive_bgworker_init(DatumGetInt32(main_arg), pgactive_WORKER_PERDB);

	perdb = &pgactive_worker_slot->data.perdb;

	perdb->nnodes = -1;

	pgactive_make_my_nodeid(&myid);
	elog(DEBUG1, "per-db worker for node " pgactive_NODEID_FORMAT " starting", pgactive_LOCALID_FORMAT_ARGS);

	appendStringInfo(&si, "%s:perdb", pgactive_get_my_cached_node_name());
	SetConfigOption("application_name", si.data, PGC_USERSET, PGC_S_SESSION);
	SetConfigOption("lock_timeout", "10000", PGC_USERSET, PGC_S_SESSION);

	CurrentResourceOwner = ResourceOwnerCreate(NULL, "pgactive seq top-level resource owner");
	pgactive_saved_resowner = CurrentResourceOwner;

	/*
	 * It's necessary to acquire a lock here so that a concurrent
	 * pgactive_perdb_xact_callback can't try to set our latch at the same
	 * time as we write to it.
	 *
	 * There's no per-worker lock, so we just take the lock on the whole
	 * segment.
	 */
	LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);
	perdb->proclatch = &MyProc->procLatch;
	perdb->p_dboid = MyDatabaseId;
	LWLockRelease(pgactiveWorkerCtl->lock);

	Assert(perdb->c_dboid == perdb->p_dboid);

	/* need to be able to perform writes ourselves */
	pgactive_executor_always_allow_writes(true);
	pgactive_locks_startup();

	{
		int			spi_ret;
		MemoryContext saved_ctx;
		pgactiveNodeInfo *local_node;

		/*
		 * Check the local pgactive.pgactive_nodes table to see if there's an
		 * entry for our node.
		 *
		 * Note that we don't have to explicitly SPI_finish(...) on error
		 * paths; that's taken care of for us.
		 */
		StartTransactionCommand();
		spi_ret = SPI_connect();
		if (spi_ret != SPI_OK_CONNECT)
			elog(ERROR, "SPI already connected; this shouldn't be possible");
		PushActiveSnapshot(GetTransactionSnapshot());

		saved_ctx = MemoryContextSwitchTo(TopMemoryContext);
		local_node = pgactive_nodes_get_local_info(&myid);
		MemoryContextSwitchTo(saved_ctx);

		if (local_node == NULL)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("local node record for " pgactive_NODEID_FORMAT " not found",
							pgactive_NODEID_FORMAT_ARGS(myid))));

		SPI_finish();
		PopActiveSnapshot();
		CommitTransactionCommand();

		/*
		 * Check whether the local node and one remote node have same
		 * pgactive.max_nodes and pgactive.skip_ddl_replication GUC values.
		 */
		check_params_are_same();

		/*
		 * Do we need to init the local DB from a remote node?
		 */
		if (local_node->status != pgactive_NODE_STATUS_READY
			&& local_node->status != pgactive_NODE_STATUS_KILLED)
			pgactive_init_replica(local_node);

		pgactive_pgactive_node_free(local_node);
	}

	elog(DEBUG1, "starting pgactive apply workers on " pgactive_NODEID_FORMAT,
		 pgactive_LOCALID_FORMAT_ARGS);

	/* Launch the apply workers */
	pgactive_maintain_db_workers();

	while (!ProcDiePending)
	{
		if (ConfigReloadPending)
		{
			ConfigReloadPending = false;
			ProcessConfigFile(PGC_SIGHUP);
			/* set log_min_messages */
			SetConfigOption("log_min_messages", pgactive_error_severity(pgactive_log_min_messages),
							PGC_POSTMASTER, PGC_S_OVERRIDE);
		}

		pgstat_report_activity(STATE_IDLE, NULL);

		/*
		 * Background workers mustn't call usleep() or any direct equivalent:
		 * instead, they may wait on their process latch, which sleeps as
		 * necessary, but is awakened if postmaster dies.  That way the
		 * background process goes away immediately in an emergency.
		 *
		 * We wake up everytime our latch gets set or if 180 seconds have
		 * passed without events. That's a stopgap for the case a backend
		 * committed txn changes but died before setting the latch.
		 */
		rc = pgactiveWaitLatch(&MyProc->procLatch,
							   WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
							   180000L, PG_WAIT_EXTENSION);
		ResetLatch(&MyProc->procLatch);
		CHECK_FOR_INTERRUPTS();

		if (rc & WL_LATCH_SET)
		{
			/*
			 * If the perdb worker's latch is set we're being asked to rescan
			 * and launch new apply workers.
			 */
			pgactive_maintain_db_workers();
		}
	}

	perdb->p_dboid = InvalidOid;
	proc_exit(0);
}
