/* -------------------------------------------------------------------------
 *
 * bdr_shmem.c
 *		BDR shared memory management
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		bdr_shmem.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "bdr.h"

#include "miscadmin.h"

#include "replication/walsender.h"

#include "postmaster/bgworker.h"

#include "storage/ipc.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

/* shortcut for finding the the worker shmem block */
BdrWorkerControl *BdrWorkerCtl = NULL;

/* Store kind of BDR worker slot acquired for the current proc */
BdrWorkerType bdr_worker_type = BDR_WORKER_EMPTY_SLOT;

/* This worker's block within BdrWorkerCtl - only valid in bdr workers */
BdrWorker  *bdr_worker_slot = NULL;

static bool worker_slot_free_at_rel;

/* Worker generation number; see bdr_worker_shmem_startup comments */
static uint16 bdr_worker_generation;

/* shmem init hook to chain to on startup, if any */
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

static void bdr_worker_shmem_init(void);
static void bdr_worker_shmem_startup(void);

void
bdr_shmem_init(void)
{
#if PG_VERSION_NUM >= 150000
	if (prev_shmem_request_hook)
		prev_shmem_request_hook();
#endif
	/* can never have more worker slots than processes to register them */
	bdr_max_workers = max_worker_processes + max_wal_senders;

	/*
	 * For BDR there can be at most ceil(max_worker_processes / 2) databases,
	 * because for every connection we need a perdb worker and a apply
	 * process.
	 */
	bdr_max_databases = (max_worker_processes / 2) + 1;

	/* Initialize segment to keep track of processes involved in bdr. */
	bdr_worker_shmem_init();

	/* initialize other modules that need shared memory. */
	bdr_count_shmem_init(bdr_max_workers);

	bdr_locks_shmem_init();
}

/*
 * Release resources upon exit of a process that has been involved in BDR
 * work.
 *
 * NB: Has to be safe to execute even if no resources have been acquired - we
 * don't unregister the before_shmem_exit handler.
 */
static void
bdr_worker_exit(int code, Datum arg)
{
	if (bdr_worker_slot == NULL)
		return;

	bdr_worker_shmem_release();
}

static size_t
bdr_worker_shmem_size()
{
	Size		size = 0;

	size = add_size(size, sizeof(BdrWorkerControl));
	size = add_size(size, mul_size(bdr_max_workers, sizeof(BdrWorker)));

	return size;
}

/*
 * Allocate a shared memory segment big enough to hold bdr_max_workers entries
 * in the array of BDR worker info structs (BdrApplyWorker).
 *
 * Called during _PG_init, but not during postmaster restart.
 */
static void
bdr_worker_shmem_init(void)
{
#if PG_VERSION_NUM >= 150000
	Assert(process_shmem_requests_in_progress);
#else
	Assert(process_shared_preload_libraries_in_progress);
#endif

	/*
	 * bdr_worker_shmem_init() only runs on first load, not on postmaster
	 * restart, so set the worker generation here. See
	 * bdr_worker_shmem_startup.
	 *
	 * It starts at 1 because the postmaster zeroes shmem on restart, so 0 can
	 * mean "just restarted, hasn't run shmem setup callback yet".
	 */
	bdr_worker_generation = 1;

	/* Allocate enough shmem for the worker limit ... */
	RequestAddinShmemSpace(bdr_worker_shmem_size());

	/*
	 * We'll need to be able to take exclusive locks so only one per-db
	 * backend tries to allocate or free blocks from this array at once. There
	 * won't be enough contention to make anything fancier worth doing.
	 */
	RequestNamedLWLockTranche("bdr_shmem", 1);

	/*
	 * Whether this is a first startup or crash recovery, we'll be re-initing
	 * the bgworkers.
	 */
	BdrWorkerCtl = NULL;

	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = bdr_worker_shmem_startup;
}

/*
 * Init the header for our shm segment, if not already done.
 *
 * Called during postmaster start or restart, in the context of the postmaster.
 */
static void
bdr_worker_shmem_startup(void)
{
	bool		found;

	if (prev_shmem_startup_hook != NULL)
		prev_shmem_startup_hook();

	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
	BdrWorkerCtl = ShmemInitStruct("bdr_worker",
								   bdr_worker_shmem_size(),
								   &found);
	if (!found)
	{
		/* Must be in postmaster its self */
		Assert(IsPostmasterEnvironment && !IsUnderPostmaster);

		/* Init shm segment header after postmaster start or restart */
		memset(BdrWorkerCtl, 0, bdr_worker_shmem_size());
		BdrWorkerCtl->lock = &(GetNamedLWLockTranche("bdr_shmem")->lock);
		/* Assigned on supervisor launch */
		BdrWorkerCtl->supervisor_latch = NULL;
		/* Worker management starts unpaused */
		BdrWorkerCtl->worker_management_paused = false;

		BdrWorkerCtl->in_init_exec_dump_restore = false;

		/*
		 * The postmaster keeps track of a generation number for BDR workers
		 * and increments it at each restart.
		 *
		 * Background workers aren't unregistered when the postmaster restarts
		 * and clears shared memory, so after a restart the supervisor and
		 * per-db workers have no idea what workers are/aren't running, nor
		 * any way to control them. To make a clean BDR restart possible the
		 * workers registered before the restart need to find out about the
		 * restart and terminate.
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
		if (bdr_worker_generation == UINT16_MAX)
			/* We could handle wrap-around, but really ... */
			elog(FATAL, "too many postmaster crash/restart cycles, restart the PostgreSQL server");

		BdrWorkerCtl->worker_generation = ++bdr_worker_generation;
	}
	LWLockRelease(AddinShmemInitLock);

	/*
	 * We don't have anything to preserve on shutdown and don't support being
	 * unloaded from a running Pg, so don't register any shutdown hook.
	 */
}


/*
 * Allocate a block from the bdr_worker shm segment in BdrWorkerCtl, or ERROR
 * if there are no free slots.
 *
 * The block is zeroed. The worker type is set in the header.
 *
 * ctl_idx, if passed, is set to the index of the worker within BdrWorkerCtl.
 *
 * To release a block, use bdr_worker_shmem_release(...)
 *
 * You must hold BdrWorkerCtl->lock in LW_EXCLUSIVE mode for
 * this call.
 */
BdrWorker *
bdr_worker_shmem_alloc(BdrWorkerType worker_type, uint32 *ctl_idx)
{
	int			i;

	Assert(LWLockHeldByMe(BdrWorkerCtl->lock));
	for (i = 0; i < bdr_max_workers; i++)
	{
		BdrWorker  *new_entry = &BdrWorkerCtl->slots[i];

		if (new_entry->worker_type == BDR_WORKER_EMPTY_SLOT)
		{
			memset(new_entry, 0, sizeof(BdrWorker));
			new_entry->worker_type = worker_type;
			if (ctl_idx)
				*ctl_idx = i;
			return new_entry;
		}
	}
	ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
			 errmsg("no free BDR worker slots - bdr.max_workers is too low")));
	/* unreachable */
}

/*
 * Release a block allocated by bdr_worker_shmem_alloc so it can be
 * re-used.
 *
 * The bgworker *must* no longer be running.
 *
 * If passed, the bgworker handle is checked to ensure the worker
 * is not still running before the slot is released.
 */
void
bdr_worker_shmem_free(BdrWorker * worker, BackgroundWorkerHandle *handle)
{
	Assert(!LWLockHeldByMe(BdrWorkerCtl->lock));

	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);

	/* Already free? Do nothing */
	if (worker->worker_type != BDR_WORKER_EMPTY_SLOT)
	{
		/* Sanity check - ensure any associated dynamic bgworker is stopped */
		if (handle)
		{
			pid_t		pid;
			BgwHandleStatus status;

			status = GetBackgroundWorkerPid(handle, &pid);
			if (status == BGWH_STARTED)
			{
				LWLockRelease(BdrWorkerCtl->lock);
				elog(ERROR, "BUG: Attempt to release shm segment for BDR worker type=%d pid=%d that's still alive",
					 worker->worker_type, pid);
			}
		}

		/* Mark it as free */
		worker->worker_type = BDR_WORKER_EMPTY_SLOT;
		/* and for good measure, zero it so problems are seen immediately */
		memset(worker, 0, sizeof(BdrWorker));
	}
	LWLockRelease(BdrWorkerCtl->lock);
}

/*
 * Mark this process as using one of the slots created by
 * bdr_worker_shmem_alloc().
 */
void
bdr_worker_shmem_acquire(BdrWorkerType worker_type, uint32 worker_idx, bool free_at_rel)
{
	BdrWorker  *worker;

	/* can't acquire if we already have one */
	Assert(bdr_worker_type == BDR_WORKER_EMPTY_SLOT);
	Assert(bdr_worker_slot == NULL);

	worker = &BdrWorkerCtl->slots[worker_idx];

	/* ensure type is correct, before acquiring the slot */
	if (worker->worker_type != worker_type)
		elog(FATAL, "mismatch in worker state, got %u, expected %u",
			 worker->worker_type, worker_type);

	/* then acquire worker slot */
	bdr_worker_slot = worker;
	bdr_worker_type = worker->worker_type;

	worker_slot_free_at_rel = free_at_rel;

	/* register release function */
	before_shmem_exit(bdr_worker_exit, 0);
}

/*
 * Relase shmem slot acquired by bdr_worker_shmem_acquire().
 */
void
bdr_worker_shmem_release(void)
{
	Assert(bdr_worker_type != BDR_WORKER_EMPTY_SLOT);
	Assert(!LWLockHeldByMe(BdrWorkerCtl->lock));

	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);
	bdr_worker_slot->worker_pid = 0;
	bdr_worker_slot->worker_proc = NULL;
	BdrWorkerCtl->in_init_exec_dump_restore = false;
	LWLockRelease(BdrWorkerCtl->lock);

	bdr_worker_type = BDR_WORKER_EMPTY_SLOT;

	if (worker_slot_free_at_rel)
		bdr_worker_shmem_free(bdr_worker_slot, NULL);

	bdr_worker_slot = NULL;
}

/*
 * Look up a walsender or apply worker in the current database by its peer
 * sysid/timeline/dboid tuple and return a pointer to its BdrWorker struct,
 * or NULL if not found.
 *
 * The caller must hold the BdrWorkerCtl lock in at least share mode.
 */
BdrWorker *
bdr_worker_get_entry(const BDRNodeId * const nodeid, BdrWorkerType worker_type)
{
	BdrWorker  *worker = NULL;
	int			i;

	Assert(LWLockHeldByMe(BdrWorkerCtl->lock));

	if (!(worker_type == BDR_WORKER_APPLY || worker_type == BDR_WORKER_WALSENDER))
		ereport(ERROR,
				(errmsg_internal("attempt to get non-peer-specific worker of type %u by peer identity",
								 worker_type)));

	for (i = 0; i < bdr_max_workers; i++)
	{
		worker = &BdrWorkerCtl->slots[i];

		if (worker->worker_type != worker_type
			|| worker->worker_proc == NULL
			|| worker->worker_proc->databaseId != MyDatabaseId)
		{
			continue;
		}

		if (worker->worker_type == BDR_WORKER_APPLY)
		{
			const		BdrApplyWorker *const w = &worker->data.apply;

			if (bdr_nodeid_eq(&w->remote_node, nodeid))
				break;
		}
		else if (worker->worker_type == BDR_WORKER_WALSENDER)
		{
			const		BdrWalsenderWorker *const w = &worker->data.walsnd;

			if (bdr_nodeid_eq(&w->remote_node, nodeid))
				break;
		}
		else
		{
			Assert(false);		/* unreachable */
		}
	}

	return worker;
}
