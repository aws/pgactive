/* -------------------------------------------------------------------------
 *
 * pgactive_count.c
 *		Replication replication stats
 *
 * Copyright (C) 2013-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_count.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include <unistd.h>
#include <sys/stat.h>

#include "pgactive.h"

#include "fmgr.h"
#include "funcapi.h"

#include "nodes/execnodes.h"

#include "replication/origin.h"

#include "storage/fd.h"
#include "storage/lwlock.h"
#include "storage/spin.h"

#include "utils/builtins.h"

/*
 * Statistics about logical replication
 *
 * whenever this struct is changed, pgactive_count_version needs to be increased so
 * on-disk values aren't reused
 */
typedef struct pgactiveCountSlot
{
	RepOriginId node_id;

	/* we use int64 to make sure we can export to sql, there is uint64 there */
	int64		nr_commit;
	int64		nr_rollback;

	int64		nr_insert;
	int64		nr_insert_conflict;
	int64		nr_update;
	int64		nr_update_conflict;
	int64		nr_delete;
	int64		nr_delete_conflict;

	int64		nr_disconnect;
}			pgactiveCountSlot;

/*
 * Shared memory header for the stats module.
 */
typedef struct pgactiveCountControl
{
	LWLockId	lock;
	pgactiveCountSlot slots[FLEXIBLE_ARRAY_MEMBER];
}			pgactiveCountControl;

/*
 * Header of a stats disk serialization, used to detect old files, changed
 * parameters and such.
 */
typedef struct pgactiveCountSerialize
{
	uint32		magic;
	uint32		version;
	uint32		nr_slots;
}			pgactiveCountSerialize;

/* magic number of the stats file, don't change */
static const uint32 pgactive_count_magic = 0x5e51A7;

/* everytime the stored data format changes, increase */
static const uint32 pgactive_count_version = 2;

/* shortcut for the finding pgactiveCountControl in memory */
static pgactiveCountControl * pgactiveCountCtl = NULL;

/* how many nodes have we built shmem for */
static Size pgactive_count_nnodes = 0;

/* offset in the pgactiveCountControl->slots "our" backend is in */
static int	MyCountOffsetIdx = -1;

static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

static void pgactive_count_shmem_startup(void);
static void pgactive_count_shmem_shutdown(int code, Datum arg);
static Size pgactive_count_shmem_size(void);

static void pgactive_count_serialize(void);
static void pgactive_count_unserialize(void);

#define pgactive_COUNT_STAT_COLS 12

PGDLLEXPORT Datum pgactive_get_stats(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pgactive_get_stats);

static Size
pgactive_count_shmem_size(void)
{
	Size		size = 0;

	size = add_size(size, sizeof(pgactiveCountControl));
	size = add_size(size, mul_size(pgactive_count_nnodes, sizeof(pgactiveCountSlot)));

	return size;
}

void
pgactive_count_shmem_init(int nnodes)
{
#if PG_VERSION_NUM >= 150000
	Assert(process_shmem_requests_in_progress);
#else
	Assert(process_shared_preload_libraries_in_progress);
#endif

	Assert(nnodes >= 0);
	pgactive_count_nnodes = (Size) nnodes;

	RequestAddinShmemSpace(pgactive_count_shmem_size());
	/* lock for slot acquiration */
	RequestNamedLWLockTranche("pgactive_count", 1);

	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = pgactive_count_shmem_startup;
}

static void
pgactive_count_shmem_startup(void)
{
	bool		found;

	if (prev_shmem_startup_hook != NULL)
		prev_shmem_startup_hook();

	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
	pgactiveCountCtl = ShmemInitStruct("pgactive_count",
									   pgactive_count_shmem_size(),
									   &found);
	if (!found)
	{
		/* initialize */
		memset(pgactiveCountCtl, 0, pgactive_count_shmem_size());
		pgactiveCountCtl->lock = &(GetNamedLWLockTranche("pgactive_count"))->lock;
		pgactive_count_unserialize();
	}
	LWLockRelease(AddinShmemInitLock);

	/*
	 * If we're in the postmaster (or a standalone backend...), set up a shmem
	 * exit hook to dump the statistics to disk.
	 */
	if (!IsUnderPostmaster)
		on_shmem_exit(pgactive_count_shmem_shutdown, (Datum) 0);
}

static void
pgactive_count_shmem_shutdown(int code, Datum arg)
{
	/*
	 * To avoid doing the same everywhere, we only write in postmaster itself
	 * (or in a single node postgres)
	 */
	if (IsUnderPostmaster)
		return;

	/* persist the file */
	pgactive_count_serialize();
}

/*
 * Find a statistics slot for a given RepOriginId and setup a local variable
 * pointing to it so we can quickly find it for the actual statistics
 * manipulation.
 */
void
pgactive_count_set_current_node(RepOriginId node_id)
{
	size_t		i;

	MyCountOffsetIdx = -1;

	LWLockAcquire(pgactiveCountCtl->lock, LW_EXCLUSIVE);

	/* check whether stats already are counted for this node */
	for (i = 0; i < pgactive_count_nnodes; i++)
	{
		if (pgactiveCountCtl->slots[i].node_id == node_id)
		{
			MyCountOffsetIdx = i;
			break;
		}
	}

	if (MyCountOffsetIdx != -1)
		goto out;

	/* ok, get a new slot */
	for (i = 0; i < pgactive_count_nnodes; i++)
	{
		if (pgactiveCountCtl->slots[i].node_id == InvalidRepOriginId)
		{
			MyCountOffsetIdx = i;
			pgactiveCountCtl->slots[i].node_id = node_id;
			break;
		}
	}

	if (MyCountOffsetIdx == -1)
		elog(PANIC, "could not find a pgactive count slot for %u", node_id);
out:
	LWLockRelease(pgactiveCountCtl->lock);
}

/*
 * Statistic manipulation functions.
 *
 * We assume we don't have to do any locking for *our* slot since only one
 * backend will do writing there.
 */
void
pgactive_count_commit(void)
{
	Assert(MyCountOffsetIdx != -1);
	pgactiveCountCtl->slots[MyCountOffsetIdx].nr_commit++;
}

void
pgactive_count_rollback(void)
{
	Assert(MyCountOffsetIdx != -1);
	pgactiveCountCtl->slots[MyCountOffsetIdx].nr_rollback++;
}

void
pgactive_count_insert(void)
{
	Assert(MyCountOffsetIdx != -1);
	pgactiveCountCtl->slots[MyCountOffsetIdx].nr_insert++;
}

void
pgactive_count_insert_conflict(void)
{
	Assert(MyCountOffsetIdx != -1);
	pgactiveCountCtl->slots[MyCountOffsetIdx].nr_insert_conflict++;
}

void
pgactive_count_update(void)
{
	Assert(MyCountOffsetIdx != -1);
	pgactiveCountCtl->slots[MyCountOffsetIdx].nr_update++;
}

void
pgactive_count_update_conflict(void)
{
	Assert(MyCountOffsetIdx != -1);
	pgactiveCountCtl->slots[MyCountOffsetIdx].nr_update_conflict++;
}

void
pgactive_count_delete(void)
{
	Assert(MyCountOffsetIdx != -1);
	pgactiveCountCtl->slots[MyCountOffsetIdx].nr_delete++;
}

void
pgactive_count_delete_conflict(void)
{
	Assert(MyCountOffsetIdx != -1);
	pgactiveCountCtl->slots[MyCountOffsetIdx].nr_delete_conflict++;
}

void
pgactive_count_disconnect(void)
{
	Assert(MyCountOffsetIdx != -1);
	pgactiveCountCtl->slots[MyCountOffsetIdx].nr_disconnect++;
}

Datum
pgactive_get_stats(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	size_t		current_offset;

	/* Construct the tuplestore and tuple descriptor */
	InitMaterializedSRF(fcinfo, 0);

	/* don't let a node get created/vanish below us */
	LWLockAcquire(pgactiveCountCtl->lock, LW_SHARED);

	for (current_offset = 0; current_offset < pgactive_count_nnodes;
		 current_offset++)
	{
		pgactiveCountSlot *slot;
		char	   *riname;
		Datum		values[pgactive_COUNT_STAT_COLS];
		bool		nulls[pgactive_COUNT_STAT_COLS];

		slot = &pgactiveCountCtl->slots[current_offset];

		/* no stats here */
		if (slot->node_id == InvalidRepOriginId)
			continue;

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		replorigin_by_oid(slot->node_id, false, &riname);

		values[0] = ObjectIdGetDatum(slot->node_id);
		values[1] = ObjectIdGetDatum(slot->node_id);
		values[2] = CStringGetTextDatum(riname);
		values[3] = Int64GetDatumFast(slot->nr_commit);
		values[4] = Int64GetDatumFast(slot->nr_rollback);
		values[5] = Int64GetDatumFast(slot->nr_insert);
		values[6] = Int64GetDatumFast(slot->nr_insert_conflict);
		values[7] = Int64GetDatumFast(slot->nr_update);
		values[8] = Int64GetDatumFast(slot->nr_update_conflict);
		values[9] = Int64GetDatumFast(slot->nr_delete);
		values[10] = Int64GetDatumFast(slot->nr_delete_conflict);
		values[11] = Int64GetDatumFast(slot->nr_disconnect);

		tuplestore_putvalues(rsinfo->setResult, rsinfo->setDesc,
							 values, nulls);
	}
	LWLockRelease(pgactiveCountCtl->lock);

	PG_RETURN_VOID();
}

/*
 * Write the pgactive stats from shared memory to a file
 */
static void
pgactive_count_serialize(void)
{
	int			fd;
	const char *tpath = "global/pgactive.stat.tmp";
	const char *path = "global/pgactive.stat";
	pgactiveCountSerialize serial;
	Size		write_size;

	LWLockAcquire(pgactiveCountCtl->lock, LW_EXCLUSIVE);

	if (unlink(tpath) < 0 && errno != ENOENT)
	{
		LWLockRelease(pgactiveWorkerCtl->lock);
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not unlink \"%s\": %m", tpath)));
	}

	fd = OpenTransientFilePerm((char *) tpath,
							   O_WRONLY | O_CREAT | O_EXCL | PG_BINARY,
							   S_IRUSR | S_IWUSR);
	if (fd < 0)
	{
		LWLockRelease(pgactiveWorkerCtl->lock);
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open \"%s\": %m", tpath)));
	}

	serial.magic = pgactive_count_magic;
	serial.version = pgactive_count_version;
	serial.nr_slots = pgactive_count_nnodes;

	/* write header */
	write_size = sizeof(serial);
	if ((write(fd, &serial, write_size)) != write_size)
	{
		LWLockRelease(pgactiveWorkerCtl->lock);
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not write pgactive stat file data \"%s\": %m",
						tpath)));
	}

	/* write data */
	write_size = sizeof(pgactiveCountSlot) * pgactive_count_nnodes;
	if ((write(fd, &pgactiveCountCtl->slots, write_size)) != write_size)
	{
		LWLockRelease(pgactiveWorkerCtl->lock);
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not write pgactive stat file data \"%s\": %m",
						tpath)));
	}

	CloseTransientFile(fd);

	/* rename into place */
	if (rename(tpath, path) != 0)
	{
		LWLockRelease(pgactiveWorkerCtl->lock);
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not rename pgactive stat file \"%s\" to \"%s\": %m",
						tpath, path)));
	}
	LWLockRelease(pgactiveCountCtl->lock);
}

/*
 * Load pgactive stats from file into shared memory
 */
static void
pgactive_count_unserialize(void)
{
	int			fd;
	const char *path = "global/pgactive.stat";
	pgactiveCountSerialize serial;
	ssize_t		read_size;

	if (pgactiveCountCtl == NULL)
		elog(ERROR, "cannot use pgactive statistics function without loading pgactive");

	LWLockAcquire(pgactiveCountCtl->lock, LW_EXCLUSIVE);

	fd = OpenTransientFilePerm((char *) path,
							   O_RDONLY | PG_BINARY, 0);
	if (fd < 0 && errno == ENOENT)
		goto out;

	if (fd < 0)
	{
		LWLockRelease(pgactiveWorkerCtl->lock);
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open pgactive stat file \"%s\": %m", path)));
	}

	read_size = sizeof(serial);
	if (read(fd, &serial, read_size) != read_size)
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not read pgactive stat file data \"%s\": %m",
						path)));

	if (serial.magic != pgactive_count_magic)
	{
		LWLockRelease(pgactiveWorkerCtl->lock);
		elog(ERROR, "expected magic %u doesn't match read magic %u",
			 pgactive_count_magic, serial.magic);
	}

	if (serial.version != pgactive_count_version)
	{
		elog(WARNING, "version of stat file changed (file %u, current %u), zeroing",
			 serial.version, pgactive_count_version);
		goto zero_file;
	}

	if (serial.nr_slots > pgactive_count_nnodes)
	{
		elog(WARNING, "stat file has more stats than we need, zeroing");
		goto zero_file;
	}

	/* read actual data, directly into shmem */
	read_size = sizeof(pgactiveCountSlot) * serial.nr_slots;
	if (read(fd, &pgactiveCountCtl->slots, read_size) != read_size)
	{
		LWLockRelease(pgactiveWorkerCtl->lock);
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not read pgactive stat file data \"%s\": %m",
						path)));
	}

out:
	if (fd >= 0)
		CloseTransientFile(fd);
	LWLockRelease(pgactiveCountCtl->lock);
	return;

zero_file:
	CloseTransientFile(fd);
	LWLockRelease(pgactiveCountCtl->lock);

	/*
	 * Overwrite the existing file.  Note our struct was zeroed in
	 * pgactive_count_shmem_startup, so we're writing empty data.
	 */
	pgactive_count_serialize();
}
