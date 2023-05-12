/* -------------------------------------------------------------------------
 *
 * bdr_count.c
 *		Replication replication stats
 *
 * Copyright (C) 2013-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		bdr_count.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include <unistd.h>
#include <sys/stat.h>

#include "bdr.h"

#include "fmgr.h"
#include "funcapi.h"
#include "miscadmin.h"

#include "nodes/execnodes.h"

#include "replication/origin.h"

#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/lwlock.h"
#include "storage/spin.h"

#include "utils/builtins.h"
#include "utils/syscache.h"

/*
 * Statistics about logical replication
 *
 * whenever this struct is changed, bdr_count_version needs to be increased so
 * on-disk values aren't reused
 */
typedef struct BdrCountSlot
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
}			BdrCountSlot;

/*
 * Shared memory header for the stats module.
 */
typedef struct BdrCountControl
{
	LWLockId	lock;
	BdrCountSlot slots[FLEXIBLE_ARRAY_MEMBER];
}			BdrCountControl;

/*
 * Header of a stats disk serialization, used to detect old files, changed
 * parameters and such.
 */
typedef struct BdrCountSerialize
{
	uint32		magic;
	uint32		version;
	uint32		nr_slots;
}			BdrCountSerialize;

/* magic number of the stats file, don't change */
static const uint32 bdr_count_magic = 0x5e51A7;

/* everytime the stored data format changes, increase */
static const uint32 bdr_count_version = 2;

/* shortcut for the finding BdrCountControl in memory */
static BdrCountControl * BdrCountCtl = NULL;

/* how many nodes have we built shmem for */
static Size bdr_count_nnodes = 0;

/* offset in the BdrCountControl->slots "our" backend is in */
static int	MyCountOffsetIdx = -1;

static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

static void bdr_count_shmem_startup(void);
static void bdr_count_shmem_shutdown(int code, Datum arg);
static Size bdr_count_shmem_size(void);

static void bdr_count_serialize(void);
static void bdr_count_unserialize(void);

#define BDR_COUNT_STAT_COLS 12

PGDLLEXPORT Datum pg_stat_get_bdr(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pg_stat_get_bdr);

static Size
bdr_count_shmem_size(void)
{
	Size		size = 0;

	size = add_size(size, sizeof(BdrCountControl));
	size = add_size(size, mul_size(bdr_count_nnodes, sizeof(BdrCountSlot)));

	return size;
}

void
bdr_count_shmem_init(int nnodes)
{
#if PG_VERSION_NUM < 150000
	Assert(process_shared_preload_libraries_in_progress);
#endif

	Assert(nnodes >= 0);
	bdr_count_nnodes = (Size) nnodes;

	RequestAddinShmemSpace(bdr_count_shmem_size());
	/* lock for slot acquiration */
	RequestNamedLWLockTranche("bdr_count", 1);

	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = bdr_count_shmem_startup;
}

static void
bdr_count_shmem_startup(void)
{
	bool		found;

	if (prev_shmem_startup_hook != NULL)
		prev_shmem_startup_hook();

	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
	BdrCountCtl = ShmemInitStruct("bdr_count",
								  bdr_count_shmem_size(),
								  &found);
	if (!found)
	{
		/* initialize */
		memset(BdrCountCtl, 0, bdr_count_shmem_size());
		BdrCountCtl->lock = &(GetNamedLWLockTranche("bdr_count"))->lock;
		bdr_count_unserialize();
	}
	LWLockRelease(AddinShmemInitLock);

	/*
	 * If we're in the postmaster (or a standalone backend...), set up a shmem
	 * exit hook to dump the statistics to disk.
	 */
	if (!IsUnderPostmaster)
		on_shmem_exit(bdr_count_shmem_shutdown, (Datum) 0);
}

static void
bdr_count_shmem_shutdown(int code, Datum arg)
{
	/*
	 * To avoid doing the same everywhere, we only write in postmaster itself
	 * (or in a single node postgres)
	 */
	if (IsUnderPostmaster)
		return;

	/* persist the file */
	bdr_count_serialize();
}

/*
 * Find a statistics slot for a given RepOriginId and setup a local variable
 * pointing to it so we can quickly find it for the actual statistics
 * manipulation.
 */
void
bdr_count_set_current_node(RepOriginId node_id)
{
	size_t		i;

	MyCountOffsetIdx = -1;

	LWLockAcquire(BdrCountCtl->lock, LW_EXCLUSIVE);

	/* check whether stats already are counted for this node */
	for (i = 0; i < bdr_count_nnodes; i++)
	{
		if (BdrCountCtl->slots[i].node_id == node_id)
		{
			MyCountOffsetIdx = i;
			break;
		}
	}

	if (MyCountOffsetIdx != -1)
		goto out;

	/* ok, get a new slot */
	for (i = 0; i < bdr_count_nnodes; i++)
	{
		if (BdrCountCtl->slots[i].node_id == InvalidRepOriginId)
		{
			MyCountOffsetIdx = i;
			BdrCountCtl->slots[i].node_id = node_id;
			break;
		}
	}

	if (MyCountOffsetIdx == -1)
		elog(PANIC, "could not find a bdr count slot for %u", node_id);
out:
	LWLockRelease(BdrCountCtl->lock);
}

/*
 * Statistic manipulation functions.
 *
 * We assume we don't have to do any locking for *our* slot since only one
 * backend will do writing there.
 */
void
bdr_count_commit(void)
{
	Assert(MyCountOffsetIdx != -1);
	BdrCountCtl->slots[MyCountOffsetIdx].nr_commit++;
}

void
bdr_count_rollback(void)
{
	Assert(MyCountOffsetIdx != -1);
	BdrCountCtl->slots[MyCountOffsetIdx].nr_rollback++;
}

void
bdr_count_insert(void)
{
	Assert(MyCountOffsetIdx != -1);
	BdrCountCtl->slots[MyCountOffsetIdx].nr_insert++;
}

void
bdr_count_insert_conflict(void)
{
	Assert(MyCountOffsetIdx != -1);
	BdrCountCtl->slots[MyCountOffsetIdx].nr_insert_conflict++;
}

void
bdr_count_update(void)
{
	Assert(MyCountOffsetIdx != -1);
	BdrCountCtl->slots[MyCountOffsetIdx].nr_update++;
}

void
bdr_count_update_conflict(void)
{
	Assert(MyCountOffsetIdx != -1);
	BdrCountCtl->slots[MyCountOffsetIdx].nr_update_conflict++;
}

void
bdr_count_delete(void)
{
	Assert(MyCountOffsetIdx != -1);
	BdrCountCtl->slots[MyCountOffsetIdx].nr_delete++;
}

void
bdr_count_delete_conflict(void)
{
	Assert(MyCountOffsetIdx != -1);
	BdrCountCtl->slots[MyCountOffsetIdx].nr_delete_conflict++;
}

void
bdr_count_disconnect(void)
{
	Assert(MyCountOffsetIdx != -1);
	BdrCountCtl->slots[MyCountOffsetIdx].nr_disconnect++;
}

Datum
pg_stat_get_bdr(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	size_t		current_offset;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("Access to pg_stat_get_bdr() denied as non-superuser")));

	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not allowed in this context")));
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	if (tupdesc->natts != BDR_COUNT_STAT_COLS)
		elog(ERROR, "wrong function definition");

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	/* don't let a node get created/vanish below us */
	LWLockAcquire(BdrCountCtl->lock, LW_SHARED);

	for (current_offset = 0; current_offset < bdr_count_nnodes;
		 current_offset++)
	{
		BdrCountSlot *slot;
		char	   *riname;
		Datum		values[BDR_COUNT_STAT_COLS];
		bool		nulls[BDR_COUNT_STAT_COLS];

		slot = &BdrCountCtl->slots[current_offset];

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

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}
	LWLockRelease(BdrCountCtl->lock);

	tuplestore_donestoring(tupstore);

	return (Datum) 0;
}

/*
 * Write the BDR stats from shared memory to a file
 */
static void
bdr_count_serialize(void)
{
	int			fd;
	const char *tpath = "global/bdr.stat.tmp";
	const char *path = "global/bdr.stat";
	BdrCountSerialize serial;
	Size		write_size;

	LWLockAcquire(BdrCountCtl->lock, LW_EXCLUSIVE);

	if (unlink(tpath) < 0 && errno != ENOENT)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not unlink \"%s\": %m", tpath)));

	fd = OpenTransientFilePerm((char *) tpath,
							   O_WRONLY | O_CREAT | O_EXCL | PG_BINARY,
							   S_IRUSR | S_IWUSR);
	if (fd < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open \"%s\": %m", tpath)));

	serial.magic = bdr_count_magic;
	serial.version = bdr_count_version;
	serial.nr_slots = bdr_count_nnodes;

	/* write header */
	write_size = sizeof(serial);
	if ((write(fd, &serial, write_size)) != write_size)
	{
		int			save_errno = errno;

		CloseTransientFile(fd);
		errno = save_errno;
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not write bdr stat file data \"%s\": %m",
						tpath)));
	}

	/* write data */
	write_size = sizeof(BdrCountSlot) * bdr_count_nnodes;
	if ((write(fd, &BdrCountCtl->slots, write_size)) != write_size)
	{
		int			save_errno = errno;

		CloseTransientFile(fd);
		errno = save_errno;
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not write bdr stat file data \"%s\": %m",
						tpath)));
	}

	CloseTransientFile(fd);

	/* rename into place */
	if (rename(tpath, path) != 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not rename bdr stat file \"%s\" to \"%s\": %m",
						tpath, path)));
	LWLockRelease(BdrCountCtl->lock);
}

/*
 * Load BDR stats from file into shared memory
 */
static void
bdr_count_unserialize(void)
{
	int			fd;
	const char *path = "global/bdr.stat";
	BdrCountSerialize serial;
	ssize_t		read_size;

	if (BdrCountCtl == NULL)
		elog(ERROR, "cannot use bdr statistics function without loading bdr");

	LWLockAcquire(BdrCountCtl->lock, LW_EXCLUSIVE);

	fd = OpenTransientFilePerm((char *) path,
							   O_RDONLY | PG_BINARY, 0);
	if (fd < 0 && errno == ENOENT)
		goto out;

	if (fd < 0)
	{
		LWLockRelease(BdrCountCtl->lock);
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open bdr stat file \"%s\": %m", path)));
	}

	read_size = sizeof(serial);
	if (read(fd, &serial, read_size) != read_size)
	{
		int			saved_errno = errno;

		LWLockRelease(BdrCountCtl->lock);
		CloseTransientFile(fd);
		errno = saved_errno;
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not read bdr stat file data \"%s\": %m",
						path)));
	}

	if (serial.magic != bdr_count_magic)
	{
		LWLockRelease(BdrCountCtl->lock);
		CloseTransientFile(fd);
		elog(ERROR, "expected magic %u doesn't match read magic %u",
			 bdr_count_magic, serial.magic);
	}

	if (serial.version != bdr_count_version)
	{
		elog(WARNING, "version of stat file changed (file %u, current %u), zeroing",
			 serial.version, bdr_count_version);
		goto zero_file;
	}

	if (serial.nr_slots > bdr_count_nnodes)
	{
		elog(WARNING, "stat file has more stats than we need, zeroing");
		goto zero_file;
	}

	/* read actual data, directly into shmem */
	read_size = sizeof(BdrCountSlot) * serial.nr_slots;
	if (read(fd, &BdrCountCtl->slots, read_size) != read_size)
	{
		int			saved_errno = errno;

		CloseTransientFile(fd);
		errno = saved_errno;
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not read bdr stat file data \"%s\": %m",
						path)));
	}

out:
	if (fd >= 0)
		CloseTransientFile(fd);
	LWLockRelease(BdrCountCtl->lock);
	return;

zero_file:
	CloseTransientFile(fd);
	LWLockRelease(BdrCountCtl->lock);

	/*
	 * Overwrite the existing file.  Note our struct was zeroed in
	 * bdr_count_shmem_startup, so we're writing empty data.
	 */
	bdr_count_serialize();
}
