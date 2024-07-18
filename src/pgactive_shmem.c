/* -------------------------------------------------------------------------
 *
 * pgactive_shmem.c
 *		pgactive shared memory management
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_shmem.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pgactive.h"

#include "miscadmin.h"

#include "replication/walsender.h"

#include "postmaster/bgworker.h"

#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

/* shortcut for finding the the worker shmem block */
pgactiveWorkerControl *pgactiveWorkerCtl = NULL;

/* Store kind of pgactive worker slot acquired for the current proc */
pgactiveWorkerType pgactive_worker_type = pgactive_WORKER_EMPTY_SLOT;

/* This worker's block within pgactiveWorkerCtl - only valid in pgactive workers */
pgactiveWorker *pgactive_worker_slot = NULL;

static bool worker_slot_free_at_rel;

/* Worker generation number; see pgactive_worker_shmem_startup comments */
static uint16 pgactive_worker_generation;

/* shmem init hook to chain to on startup, if any */
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

static void pgactive_worker_shmem_init(void);
static void pgactive_worker_shmem_startup(void);

void
pgactive_shmem_init(void)
{
#if PG_VERSION_NUM >= 150000
	if (pgactive_prev_shmem_request_hook)
		pgactive_prev_shmem_request_hook();
#endif
	/* can never have more worker slots than processes to register them */
	pgactive_max_workers = max_worker_processes + max_wal_senders;

	/*
	 * For pgactive there can be at most ceil(max_worker_processes / 2)
	 * databases, because for every connection we need a perdb worker and a
	 * apply process.
	 */
	pgactive_max_databases = (max_worker_processes / 2) + 1;

	/* Initialize segment to keep track of processes involved in pgactive. */
	pgactive_worker_shmem_init();

	/* initialize other modules that need shared memory. */
	pgactive_count_shmem_init(pgactive_max_workers);

	pgactive_locks_shmem_init();

	pgactive_nid_shmem_init();
}

/*
 * Release resources upon exit of a process that has been involved in pgactive
 * work.
 *
 * NB: Has to be safe to execute even if no resources have been acquired - we
 * don't unregister the before_shmem_exit handler.
 */
static void
pgactive_worker_exit(int code, Datum arg)
{
	if (pgactive_worker_slot == NULL)
		return;

	pgactive_worker_shmem_release();
}

static size_t
pgactive_worker_shmem_size()
{
	Size		size = 0;

	size = add_size(size, sizeof(pgactiveWorkerControl));
	size = add_size(size, mul_size(pgactive_max_workers, sizeof(pgactiveWorker)));

	return size;
}

/*
 * Allocate a shared memory segment big enough to hold pgactive_max_workers entries
 * in the array of pgactive worker info structs (pgactiveApplyWorker).
 *
 * Called during _PG_init, but not during postmaster restart.
 */
static void
pgactive_worker_shmem_init(void)
{
#if PG_VERSION_NUM >= 150000
	Assert(process_shmem_requests_in_progress);
#else
	Assert(process_shared_preload_libraries_in_progress);
#endif

	/*
	 * pgactive_worker_shmem_init() only runs on first load, not on postmaster
	 * restart, so set the worker generation here. See
	 * pgactive_worker_shmem_startup.
	 *
	 * It starts at 1 because the postmaster zeroes shmem on restart, so 0 can
	 * mean "just restarted, hasn't run shmem setup callback yet".
	 */
	pgactive_worker_generation = 1;

	/* Allocate enough shmem for the worker limit ... */
	RequestAddinShmemSpace(pgactive_worker_shmem_size());

	/*
	 * We'll need to be able to take exclusive locks so only one per-db
	 * backend tries to allocate or free blocks from this array at once. There
	 * won't be enough contention to make anything fancier worth doing.
	 */
	RequestNamedLWLockTranche("pgactive_shmem", 1);

	/*
	 * Whether this is a first startup or crash recovery, we'll be re-initing
	 * the bgworkers.
	 */
	pgactiveWorkerCtl = NULL;

	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = pgactive_worker_shmem_startup;
}

/*
 * Init the header for our shm segment, if not already done.
 *
 * Called during postmaster start or restart, in the context of the postmaster.
 */
static void
pgactive_worker_shmem_startup(void)
{
	bool		found;

	if (prev_shmem_startup_hook != NULL)
		prev_shmem_startup_hook();

	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
	pgactiveWorkerCtl = ShmemInitStruct("pgactive_worker",
										pgactive_worker_shmem_size(),
										&found);
	if (!found)
	{
		/* Must be in postmaster its self */
		Assert(IsPostmasterEnvironment && !IsUnderPostmaster);

		/* Init shm segment header after postmaster start or restart */
		memset(pgactiveWorkerCtl, 0, pgactive_worker_shmem_size());
		pgactiveWorkerCtl->lock = &(GetNamedLWLockTranche("pgactive_shmem")->lock);
		/* Assigned on supervisor launch */
		pgactiveWorkerCtl->supervisor_latch = NULL;
		/* Worker management starts unpaused */
		pgactiveWorkerCtl->worker_management_paused = false;

		/*
		 * The postmaster keeps track of a generation number for pgactive
		 * workers and increments it at each restart.
		 *
		 * Background workers aren't unregistered when the postmaster restarts
		 * and clears shared memory, so after a restart the supervisor and
		 * per-db workers have no idea what workers are/aren't running, nor
		 * any way to control them. To make a clean pgactive restart possible
		 * the workers registered before the restart need to find out about
		 * the restart and terminate.
		 *
		 * To make that possible we pass the generation number to the worker
		 * in its main argument, and also set it in shared memory. The two
		 * must match. If they don't, the worker will proc_exit(0), causing
		 * its self to be unregistered.
		 *
		 * This should really be part of the bgworker API its self, handled
		 * via a BGW_NO_RESTART_ON_CRASH flag or by providing a generation
		 * number as a bgworker argument. However, for now we're stuck with
		 * this workaround.
		 */
		if (pgactive_worker_generation == UINT16_MAX)
			/* We could handle wrap-around, but really ... */
			elog(FATAL, "too many postmaster crash/restart cycles, restart the PostgreSQL server");

		pgactiveWorkerCtl->worker_generation = ++pgactive_worker_generation;
	}
	LWLockRelease(AddinShmemInitLock);

	/*
	 * We don't have anything to preserve on shutdown and don't support being
	 * unloaded from a running Pg, so don't register any shutdown hook.
	 */
}


/*
 * Allocate a block from the pgactive_worker shm segment in pgactiveWorkerCtl, or ERROR
 * if there are no free slots.
 *
 * The block is zeroed. The worker type is set in the header.
 *
 * ctl_idx, if passed, is set to the index of the worker within pgactiveWorkerCtl.
 *
 * To release a block, use pgactive_worker_shmem_release(...)
 *
 * You must hold pgactiveWorkerCtl->lock in LW_EXCLUSIVE mode for
 * this call.
 */
pgactiveWorker *
pgactive_worker_shmem_alloc(pgactiveWorkerType worker_type, uint32 *ctl_idx)
{
	int			i;

	Assert(LWLockHeldByMe(pgactiveWorkerCtl->lock));
	for (i = 0; i < pgactive_max_workers; i++)
	{
		pgactiveWorker *new_entry = &pgactiveWorkerCtl->slots[i];

		if (new_entry->worker_type == pgactive_WORKER_EMPTY_SLOT)
		{
			memset(new_entry, 0, sizeof(pgactiveWorker));
			new_entry->worker_type = worker_type;
			if (ctl_idx)
				*ctl_idx = i;
			return new_entry;
		}
	}

	ereport(ERROR,
			(errcode(ERRCODE_CONFIGURATION_LIMIT_EXCEEDED),
			 errmsg("could not find free slot for pgactve \"%s\"",
					pgactiveWorkerTypeNames[worker_type]),
			 errhint("Consider increasing configuration parameter \"%s\"",
					 worker_type == pgactive_WORKER_WALSENDER ? "max_wal_senders" : "max_worker_processes")));

	/* unreachable */
}

/*
 * Release a block allocated by pgactive_worker_shmem_alloc so it can be
 * re-used.
 *
 * The bgworker *must* no longer be running.
 *
 * If passed, the bgworker handle is checked to ensure the worker
 * is not still running before the slot is released.
 */
void
pgactive_worker_shmem_free(pgactiveWorker * worker,
						   BackgroundWorkerHandle *handle,
						   bool need_lock)
{
	if (need_lock)
		LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);

	/* Already free? Do nothing */
	if (worker->worker_type != pgactive_WORKER_EMPTY_SLOT)
	{
		/* Sanity check - ensure any associated dynamic bgworker is stopped */
		if (handle)
		{
			pid_t		pid;
			BgwHandleStatus status;

			status = GetBackgroundWorkerPid(handle, &pid);
			if (status == BGWH_STARTED)
			{
				LWLockRelease(pgactiveWorkerCtl->lock);
				elog(ERROR, "cannot release shared memory slot for a live pgactive worker type=%d pid=%d",
					 worker->worker_type, pid);
			}
		}

		/* Mark it as free */
		worker->worker_type = pgactive_WORKER_EMPTY_SLOT;
		/* and for good measure, zero it so problems are seen immediately */
		memset(worker, 0, sizeof(pgactiveWorker));
	}

	/*
	 * Reset pgactive worker-local variable to shmem block if we've freed it
	 * up above.
	 */
	if (pgactive_worker_slot == worker)
		pgactive_worker_slot = NULL;

	if (need_lock)
		LWLockRelease(pgactiveWorkerCtl->lock);
}

/*
 * Mark this process as using one of the slots created by
 * pgactive_worker_shmem_alloc().
 */
void
pgactive_worker_shmem_acquire(pgactiveWorkerType worker_type, uint32 worker_idx, bool free_at_rel)
{
	pgactiveWorker *worker;

	/* can't acquire if we already have one */
	Assert(pgactive_worker_type == pgactive_WORKER_EMPTY_SLOT);
	Assert(pgactive_worker_slot == NULL);

	worker = &pgactiveWorkerCtl->slots[worker_idx];

	/* ensure type is correct, before acquiring the slot */
	if (worker->worker_type != worker_type)
		elog(FATAL, "mismatch in worker state, got %u, expected %u",
			 worker->worker_type, worker_type);

	/* then acquire worker slot */
	pgactive_worker_slot = worker;
	pgactive_worker_type = worker->worker_type;

	worker_slot_free_at_rel = free_at_rel;

	/* register release function */
	before_shmem_exit(pgactive_worker_exit, 0);
}

/*
 * Release shmem slot acquired by pgactive_worker_shmem_acquire().
 *
 * NB: Has to be safe to execute even if no resources have been acquired. This
 * is needed to avoid crashes when the function is called from multiple sites.
 */
void
pgactive_worker_shmem_release(void)
{
	if (pgactive_worker_slot == NULL)
		return;

	Assert(pgactive_worker_type != pgactive_WORKER_EMPTY_SLOT);
	Assert(!LWLockHeldByMe(pgactiveWorkerCtl->lock));

	LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);
	pgactive_worker_slot->worker_pid = 0;
	pgactive_worker_slot->worker_proc = NULL;
	pgactive_worker_type = pgactive_WORKER_EMPTY_SLOT;

	if (worker_slot_free_at_rel)
		pgactive_worker_shmem_free(pgactive_worker_slot, NULL, false);

	pgactive_worker_slot = NULL;
	LWLockRelease(pgactiveWorkerCtl->lock);
}

/*
 * Look up a walsender or apply worker in the current database by its peer
 * sysid/timeline/dboid tuple and return a pointer to its pgactiveWorker struct,
 * or NULL if not found.
 *
 * The caller must hold the pgactiveWorkerCtl lock in at least share mode.
 */
pgactiveWorker *
pgactive_worker_get_entry(const pgactiveNodeId * const nodeid, pgactiveWorkerType worker_type)
{
	pgactiveWorker *worker = NULL;
	int			i;

	Assert(LWLockHeldByMe(pgactiveWorkerCtl->lock));

	if (!(worker_type == pgactive_WORKER_APPLY || worker_type == pgactive_WORKER_WALSENDER))
		ereport(ERROR,
				(errmsg_internal("attempt to get non-peer-specific worker of type %u by peer identity",
								 worker_type)));

	for (i = 0; i < pgactive_max_workers; i++)
	{
		worker = &pgactiveWorkerCtl->slots[i];

		if (worker->worker_type != worker_type
			|| worker->worker_proc == NULL
			|| worker->worker_proc->databaseId != MyDatabaseId)
		{
			continue;
		}

		if (worker->worker_type == pgactive_WORKER_APPLY)
		{
			const		pgactiveApplyWorker *const w = &worker->data.apply;

			if (pgactive_nodeid_eq(&w->remote_node, nodeid))
				break;
		}
		else if (worker->worker_type == pgactive_WORKER_WALSENDER)
		{
			const		pgactiveWalsenderWorker *const w = &worker->data.walsnd;

			if (pgactive_nodeid_eq(&w->remote_node, nodeid))
				break;
		}
		else
		{
			Assert(false);		/* unreachable */
		}
	}

	return worker;
}
