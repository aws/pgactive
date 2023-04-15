/* -------------------------------------------------------------------------
 *
 * bdr_seq.c
 *		A distributed sequence implementation.
 *
 * Replication???
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		bdr_seq.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "bdr_config.h"
#ifndef HAVE_SEQAM
#error bdr_seq.h only compiles when sequence access methods are available in Pg
#endif

#include "bdr.h"

#include "miscadmin.h"
#include "pgstat.h"

#include "access/reloptions.h"
#include "access/seqam.h"
#include "access/transam.h"
#include "access/xact.h"

#include "catalog/namespace.h"
#include "catalog/pg_type.h"

#include "commands/sequence.h"

#include "executor/spi.h"

#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"

#include "storage/bufmgr.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lmgr.h"
#include "storage/proc.h"
#include "storage/sinvaladt.h"

typedef struct BdrSequencerSlot
{
	Oid			database_oid;
	int			nnodes;
	Latch	   *proclatch;
}			BdrSequencerSlot;

typedef struct BdrSequencerControl
{
	int			next_slot;
	BdrSequencerSlot slots[FLEXIBLE_ARRAY_MEMBER];
}			BdrSequencerControl;

typedef struct BdrSequenceValues
{
	int64		start_value;
	int64		next_value;
	int64		end_value;
}			BdrSequenceValues;

/* global sequence relids */
Oid			BdrSeqamOid = InvalidOid;
Oid			BdrSequenceValuesRelid; /* bdr_sequence_values */
Oid			BdrSequenceElectionsRelid;	/* bdr_sequence_elections */
Oid			BdrVotesRelid;		/* bdr_votes */

/* Our offset within the shared memory array of registered sequence managers */
static int	seq_slot = -1;

static BdrSequencerControl * BdrSequencerCtl = NULL;

/* how many nodes have we built shmem for */
static size_t bdr_seq_nsequencers = 0;

static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

static bool bdr_seq_pending_wakeup = false;

static relopt_kind bdr_seq_relopt_kind = RELOPT_KIND_SEQUENCE;

/* vote, the logic is in a function */
const char *vote_sql = ""
"SELECT bdr.bdr_sequencer_vote($1, $2, $3, $4);\n";

const char *start_elections_sql =
"WITH to_be_updated_sequences AS (\n"
"    SELECT\n"
"        pg_namespace.nspname AS seqschema,\n"
"        seq.relname AS seqname,\n"
"        COUNT(bdr_sequence_values) AS open_seq_chunks,\n"
"        GREATEST(COALESCE((\n"
"            SELECT max(upper(seqrange))\n"
"            FROM bdr_sequence_values max_val\n"
"            WHERE\n"
"                max_val.seqschema = pg_namespace.nspname\n"
"                AND max_val.seqname = seq.relname\n"
"        ), 0), (SELECT start_value FROM pg_sequence_parameters(seq.oid)))\n"
"        AS current_max,\n"
"        seq.cache_chunks\n"
"    FROM\n"
"        (SELECT\n"
"            pg_class.oid,\n"
"            pg_class.relnamespace,\n"
"            pg_class.relname,\n"
"            COALESCE((SELECT split_part(o, '=', 2)::int\n"
"                      FROM unnest(pg_class.reloptions) o\n"
"                      WHERE split_part(o, '=', 1) = 'cache_chunks'), 5)\n"
"            AS cache_chunks\n"
"        FROM\n"
"            pg_class\n"
"        WHERE\n"
"            pg_class.relkind = 'S' AND\n"
"            pg_class.relam = (SELECT oid FROM pg_seqam WHERE seqamname = 'bdr')\n"
"        ) seq\n"
"        JOIN pg_namespace ON (seq.relnamespace = pg_namespace.oid)\n"
"        LEFT JOIN bdr_sequence_values ON (\n"
"            bdr_sequence_values.seqschema = pg_namespace.nspname\n"
"            AND bdr_sequence_values.seqname = seq.relname\n"
"            AND bdr_sequence_values.emptied = false\n"
"            AND bdr_sequence_values.in_use = false\n"
"            AND bdr_sequence_values.failed = false\n"
"            AND bdr_sequence_values.owning_sysid = $1\n"
"            AND bdr_sequence_values.owning_tlid = $2\n"
"            AND bdr_sequence_values.owning_dboid = $3\n"
"            AND bdr_sequence_values.owning_riname = $4\n"
"        )\n"
"    GROUP BY\n"
"        seq.relname,\n"
"        pg_namespace.nspname,\n"
"        seq.oid,\n"
"        seq.cache_chunks\n"
"    HAVING\n"
"        count(bdr_sequence_values) <= cache_chunks\n"
"),\n"
"to_be_inserted_chunks AS (\n"
"    SELECT\n"
"        seqschema,\n"
"        seqname,\n"
"        current_max,\n"
"        generate_series(\n"
"            current_max,\n"
"            -- 10000 is the chunk size, -1 is to get < instead <= out of generate_series\n"
"            current_max + 10000 * (cache_chunks - open_seq_chunks) - 1,\n"
"            10000) chunk_start\n"
"    FROM to_be_updated_sequences\n"
"    LIMIT 500\n"
"),\n"
"inserted_chunks AS (\n"
"    INSERT INTO bdr_sequence_elections(\n"
"        owning_sysid,\n"
"        owning_tlid,\n"
"        owning_dboid,\n"
"        owning_riname,\n"
"        owning_election_id,\n"
"        vote_type,\n"
"        open,\n"
"        seqschema,\n"
"        seqname,\n"
"        seqrange\n"
"    )\n"
"    SELECT\n"
"        $1,\n"
"        $2,\n"
"        $3,\n"
"        $4,\n"
"        (\n"
"            SELECT COALESCE(max(owning_election_id), 0)\n"
"            FROM bdr_sequence_elections biggest\n"
"            WHERE\n"
"               biggest.owning_sysid = $1\n"
"               AND biggest.owning_tlid = $2\n"
"               AND biggest.owning_dboid = $3\n"
"               AND biggest.owning_riname = $4\n"
"         ) + row_number() OVER (),\n"
"        'sequence',\n"
"        true AS open,\n"
"        seqschema,\n"
"        seqname,\n"
"        int8range(chunk_start, chunk_start + 10000) AS seqrange\n"
"    FROM to_be_inserted_chunks\n"
"    RETURNING\n"
"        seqschema,\n"
"        seqname,\n"
"        seqrange\n"
")\n"
"\n"
"INSERT INTO bdr_sequence_values(\n"
"    owning_sysid,\n"
"    owning_tlid,\n"
"    owning_dboid,\n"
"    owning_riname,\n"
"    seqschema,\n"
"    seqname,\n"
"    confirmed,\n"
"    in_use,\n"
"    emptied,\n"
"    seqrange\n"
")\n"
"SELECT\n"
"    $1,\n"
"    $2,\n"
"    $3,\n"
"    $4,\n"
"    seqschema,\n"
"    seqname,\n"
"    false AS confirmed,\n"
"    false AS in_use,\n"
"    false AS emptied,\n"
"    int8range(chunk_start, chunk_start + 10000)\n"
"FROM to_be_inserted_chunks\n"
"-- force evaluation \n"
"WHERE (SELECT count(*) FROM inserted_chunks) >= 0\n"
"RETURNING\n"
"    owning_sysid,\n"
"    owning_tlid,\n"
"    owning_dboid,\n"
"    owning_riname,\n"
"    seqschema,\n"
"    seqname,\n"
"    confirmed,\n"
"    emptied,\n"
"    seqrange\n"
		   ;

const char *tally_elections_sql =
"WITH tallied_votes AS (\n"
"SELECT\n"
"    election.owning_sysid,\n"
"    election.owning_tlid,\n"
"    election.owning_dboid,\n"
"    election.owning_riname,\n"
"    election.owning_election_id,\n"
"    election.seqschema,\n"
"    election.seqname,\n"
"    election.seqrange,\n"
"    SUM(COALESCE((vote.vote = true)::int, 0)) AS yays,\n"
"    SUM(COALESCE((vote.vote = false)::int, 0)) AS nays,\n"
"    COUNT(vote.vote) AS nr_votes,\n"
"    /* majority of others */\n"
"    COUNT(vote.vote) >= ceil($5/ 2.0) AS sufficient\n"
"FROM\n"
"    bdr_sequence_elections election\n"
"    LEFT JOIN bdr_votes vote ON (\n"
"            election.owning_sysid = vote.vote_sysid\n"
"            AND election.owning_tlid = vote.vote_tlid\n"
"            AND election.owning_dboid = vote.vote_dboid\n"
"            AND election.owning_riname = vote.vote_riname\n"
"            AND election.owning_election_id = vote.vote_election_id\n"
"    )\n"
"WHERE\n"
"    election.open\n"
"    AND election.owning_sysid = $1\n"
"    AND election.owning_tlid = $2\n"
"    AND election.owning_dboid = $3\n"
"    AND election.owning_riname = $4\n"
"GROUP BY\n"
"    election.owning_sysid,\n"
"    election.owning_tlid,\n"
"    election.owning_dboid,\n"
"    election.owning_riname,\n"
"    election.owning_election_id,\n"
"    election.seqschema,\n"
"    election.seqname,\n"
"    election.seqrange\n"
"),\n"
"cast_votes AS (\n"
"    UPDATE bdr_sequence_elections\n"
"    SET\n"
"        open = false,\n"
"        success = (nays = 0 OR yays >= ceil($5/ 2.0))\n"
"    FROM tallied_votes\n"
"    WHERE\n"
"       bdr_sequence_elections.owning_sysid = tallied_votes.owning_sysid\n"
"       AND bdr_sequence_elections.owning_tlid = tallied_votes.owning_tlid\n"
"       AND bdr_sequence_elections.owning_dboid = tallied_votes.owning_dboid\n"
"       AND bdr_sequence_elections.owning_riname = tallied_votes.owning_riname\n"
"       AND bdr_sequence_elections.owning_election_id = tallied_votes.owning_election_id\n"
"       AND tallied_votes.sufficient\n"
"    RETURNING bdr_sequence_elections.*\n"
"),\n"
"successfull_sequence_values AS (\n"
"    UPDATE bdr_sequence_values\n"
"    SET\n"
"        confirmed = true\n"
"    FROM cast_votes\n"
"    WHERE\n"
"        cast_votes.success = true\n"
"        AND bdr_sequence_values.seqschema = cast_votes.seqschema\n"
"        AND bdr_sequence_values.seqname = cast_votes.seqname\n"
"        AND bdr_sequence_values.seqrange = cast_votes.seqrange\n"
"        AND bdr_sequence_values.owning_sysid = $1\n"
"        AND bdr_sequence_values.owning_tlid = $2\n"
"        AND bdr_sequence_values.owning_dboid = $3\n"
"        AND bdr_sequence_values.owning_riname = $4\n"
"    RETURNING bdr_sequence_values.*\n"
"),\n"
"failed_sequence_values AS (\n"
"    UPDATE bdr_sequence_values\n"
"    SET\n"
"        failed = true\n"
"    FROM cast_votes\n"
"    WHERE\n"
"        cast_votes.success = false\n"
"        AND bdr_sequence_values.seqschema = cast_votes.seqschema\n"
"        AND bdr_sequence_values.seqname = cast_votes.seqname\n"
"        AND bdr_sequence_values.seqrange = cast_votes.seqrange\n"
"        AND bdr_sequence_values.owning_sysid = $1\n"
"        AND bdr_sequence_values.owning_tlid = $2\n"
"        AND bdr_sequence_values.owning_dboid = $3\n"
"        AND bdr_sequence_values.owning_riname = $4\n"
"    RETURNING bdr_sequence_values.*\n"
")\n"
"\n"
"SELECT\n"
"    seqschema,\n"
"    seqname,\n"
"    seqrange,\n"
"    'success'::text\n"
"FROM successfull_sequence_values\n"
"\n"
"UNION ALL\n"
"\n"
"SELECT\n"
"    seqschema,\n"
"    seqname,\n"
"    seqrange,\n"
"    'failed'::text\n"
"FROM failed_sequence_values\n"
"\n"
"UNION ALL\n"
"\n"
"SELECT\n"
"    seqschema,\n"
"    seqname,\n"
"    seqrange,\n"
"    'pending'::text\n"
"FROM tallied_votes\n"
"WHERE NOT sufficient\n"
		   ;

const char *fill_sequences_sql =
"SELECT\n"
"    pg_class.oid seqoid,\n"
"    pg_namespace.nspname seqschema,\n"
"    pg_class.relname seqname\n"
"FROM pg_class\n"
"    JOIN pg_seqam ON (pg_seqam.oid = pg_class.relam)\n"
"    JOIN pg_namespace ON (pg_class.relnamespace = pg_namespace.oid)\n"
"WHERE\n"
"    relkind = 'S'\n"
"    AND seqamname = 'bdr'\n"
"ORDER BY pg_class.oid\n"
		   ;


const char *get_chunk_sql =
"UPDATE bdr_sequence_values\n"
"   SET in_use = true\n"
"WHERE\n"
"    (\n"
"        owning_sysid,\n"
"        owning_tlid,\n"
"        owning_dboid,\n"
"        owning_riname,\n"
"        seqname,\n"
"        seqschema,\n"
"        seqrange\n"
"    )\n"
"    =\n"
"    (\n"
"        SELECT\n"
"            newval.owning_sysid,\n"
"            newval.owning_tlid,\n"
"            newval.owning_dboid,\n"
"            newval.owning_riname,\n"
"            newval.seqname,\n"
"            newval.seqschema,\n"
"            newval.seqrange\n"
"        FROM bdr_sequence_values newval\n"
"        WHERE\n"
"           newval.confirmed\n"
"           AND NOT newval.emptied\n"
"           AND NOT newval.in_use\n"
"           AND newval.owning_sysid = $1\n"
"           AND newval.owning_tlid = $2\n"
"           AND newval.owning_dboid = $3\n"
"           AND newval.owning_riname = $4\n"
"           AND newval.seqschema = $5\n"
"           AND newval.seqname = $6\n"
"        ORDER BY newval.seqrange ASC\n"
"        LIMIT 1\n"
"        FOR UPDATE\n"
"    )\n"
"RETURNING\n"
"    lower(seqrange),\n"
"    upper(seqrange)\n"
		   ;

static Size
bdr_sequencer_shmem_size(void)
{
	Size		size = 0;

	size = add_size(size, sizeof(BdrSequencerControl));
	size = add_size(size, mul_size(bdr_seq_nsequencers, sizeof(BdrSequencerSlot)));

	return size;
}

static void
bdr_sequencer_shmem_shutdown(int code, Datum arg)
{
	BdrSequencerSlot *slot;

	if (seq_slot < 0)
		return;

	Assert(seq_slot < bdr_seq_nsequencers);

	slot = &BdrSequencerCtl->slots[seq_slot];

	slot->database_oid = InvalidOid;
	slot->proclatch = NULL;
	seq_slot = -1;
}

static void
bdr_sequencer_shmem_startup(void)
{
	bool		found;

	if (prev_shmem_startup_hook != NULL)
		prev_shmem_startup_hook();

	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
	BdrSequencerCtl = ShmemInitStruct("bdr_sequencer",
									  bdr_sequencer_shmem_size(),
									  &found);
	if (!found)
	{
		/* initialize */
		memset(BdrSequencerCtl, 0, bdr_sequencer_shmem_size());

		/*
		 * next_slot allows perdb workers to allocate seq slots. The sequencer
		 * will likely be separated into a different worker later.
		 */
		BdrSequencerCtl->next_slot = 0;
	}
	LWLockRelease(AddinShmemInitLock);

	on_shmem_exit(bdr_sequencer_shmem_shutdown, (Datum) 0);
}

void
bdr_sequencer_shmem_init(int sequencers)
{
	Assert(process_shared_preload_libraries_in_progress);

	bdr_seq_nsequencers = sequencers;

	RequestAddinShmemSpace(bdr_sequencer_shmem_size());

	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = bdr_sequencer_shmem_startup;

	/*
	 * We do the reloptions initialization here because this function is
	 * called at startup for every backend.
	 */
	bdr_seq_relopt_kind = add_reloption_kind();
	add_int_reloption(bdr_seq_relopt_kind, "cache_chunks",
					  "Sets how many chunks shoult be cached on each node.",
					  5, 5, 100);
}

/*
 * The perdb worker doing sequencer setup needs to know what slot to
 * allocate for the next sequencer.
 *
 * This should go away once the sequencer is separated into its own
 * worker.
 */
int
bdr_sequencer_get_next_free_slot(void)
{
	return BdrSequencerCtl->next_slot++;
}

void
bdr_sequencer_wakeup(void)
{
	size_t		off;
	BdrSequencerSlot *slot;


	for (off = 0; off < bdr_seq_nsequencers; off++)
	{
		slot = &BdrSequencerCtl->slots[off];

		/* FIXME: locking! */
		if (slot->database_oid == InvalidOid)
			continue;

		if (slot->database_oid != MyDatabaseId)
			continue;

		SetLatch(slot->proclatch);
	}
}

static void
bdr_sequence_xact_callback(XactEvent event, void *arg)
{
	if (event != XACT_EVENT_COMMIT)
		return;

	if (bdr_seq_pending_wakeup)
	{
		bdr_sequencer_wakeup();
		bdr_seq_pending_wakeup = false;
	}
}

/*
 * Schedule a wakeup of all sequencer workers, as soon as this transaction
 * commits.
 *
 * This is e.g. useful when a new sequence is created, and the voting process
 * should start immediately.
 *
 * NB: There's a window between the commit and this callback in which this
 * backend could die without causing a cluster wide restart. So we need to
 * periodically check whether we've missed wakeups.
 */
void
bdr_schedule_eoxact_sequencer_wakeup(void)
{
	static bool registered = false;

	if (!registered)
	{
		RegisterXactCallback(bdr_sequence_xact_callback, NULL);
		registered = true;
	}
	bdr_seq_pending_wakeup = true;
}

void
bdr_sequencer_set_nnodes(int nnodes)
{
	BdrSequencerSlot *slot = &BdrSequencerCtl->slots[seq_slot];

	Assert(nnodes >= 0);
	slot->nnodes = nnodes;
}

void
bdr_sequencer_init(int new_seq_slot, int nnodes)
{
	BdrSequencerSlot *slot;

	Assert(nnodes >= 0);

	Assert(seq_slot == -1);
	seq_slot = new_seq_slot;

	slot = &BdrSequencerCtl->slots[seq_slot];
	slot->database_oid = MyDatabaseId;
	slot->proclatch = &MyProc->procLatch;
	slot->nnodes = nnodes;
}

/*
 * Acquire sequencer lock.
 *
 * We lock on on our seqam instead of the underlying relations. That's
 * advantageous because we only want to prevent modifications by the sequencer
 * or apply processes, it's perfectly fine for auto-analyze/vacuum to process
 * the relation.
 */
void
bdr_sequencer_lock(void)
{
	SetCurrentStatementStartTimestamp();
	pgstat_report_activity(STATE_RUNNING, "acquiring sequencer lock");
	LockDatabaseObject(SeqAccessMethodRelationId, BdrSeqamOid, InvalidOid,
					   ExclusiveLock);
}

bool
bdr_sequencer_vote(void)
{
	static SPIPlanPtr plan;
	Oid			argtypes[4];
	Datum		values[4];
	char		nulls[4];
	char		local_sysid[32];
	int			ret;
	int			processed = 0;

	snprintf(local_sysid, sizeof(local_sysid), UINT64_FORMAT,
			 GetSystemIdentifier());

	argtypes[0] = TEXTOID;
	nulls[0] = false;
	values[0] = CStringGetTextDatum(local_sysid);

	argtypes[1] = OIDOID;
	nulls[1] = false;
	values[1] = ObjectIdGetDatum(ThisTimeLineID);

	argtypes[2] = OIDOID;
	values[2] = ObjectIdGetDatum(MyDatabaseId);
	nulls[2] = false;

	argtypes[3] = TEXTOID;
	values[3] = CStringGetTextDatum("");
	nulls[3] = false;

	StartTransactionCommand();
	SPI_connect();

	bdr_sequencer_lock();
	PushActiveSnapshot(GetTransactionSnapshot());

	if (plan == NULL)
	{
		plan = SPI_prepare(vote_sql, 4, argtypes);
		SPI_keepplan(plan);
	}

	SetCurrentStatementStartTimestamp();
	pgstat_report_activity(STATE_RUNNING, "sequence voting");
	ret = SPI_execute_plan(plan, values, nulls, false, 0);

	if (ret != SPI_OK_SELECT)
		elog(ERROR, "expected SPI state %u, got %u", SPI_OK_INSERT, ret);

	if (SPI_processed > 0)
	{
		HeapTuple	tuple = SPI_tuptable->vals[0];
		TupleDesc	tupdesc = SPI_tuptable->tupdesc;
		Datum		inserted;
		bool		isnull;

		inserted = SPI_getbinval(tuple, tupdesc, 1, &isnull);
		Assert(!isnull);

		processed = DatumGetInt32(inserted);
	}

	PopActiveSnapshot();
	SPI_finish();
	CommitTransactionCommand();
	pgstat_report_stat(false);

	elog(DEBUG1, "started %d votes", processed);

	return processed > 0;
}

/*
 * Check whether we need to initiate a voting procedure for getting new
 * sequence chunks.
 */
bool
bdr_sequencer_start_elections(void)
{
	static SPIPlanPtr plan;
	Oid			argtypes[4];
	Datum		values[4];
	char		nulls[4];
	char		local_sysid[32];
	int			ret;
	int			processed;

	snprintf(local_sysid, sizeof(local_sysid), UINT64_FORMAT,
			 GetSystemIdentifier());

	StartTransactionCommand();
	SPI_connect();

	argtypes[0] = TEXTOID;
	nulls[0] = false;
	values[0] = CStringGetTextDatum(local_sysid);

	argtypes[1] = OIDOID;
	nulls[1] = false;
	values[1] = ObjectIdGetDatum(ThisTimeLineID);

	argtypes[2] = OIDOID;
	values[2] = ObjectIdGetDatum(MyDatabaseId);
	nulls[2] = false;

	argtypes[3] = TEXTOID;
	values[3] = CStringGetTextDatum("");
	nulls[3] = false;

	bdr_sequencer_lock();
	PushActiveSnapshot(GetTransactionSnapshot());

	if (plan == NULL)
	{
		plan = SPI_prepare(start_elections_sql, 4, argtypes);
		SPI_keepplan(plan);
	}

	SetCurrentStatementStartTimestamp();
	pgstat_report_activity(STATE_RUNNING, "start_elections");

	ret = SPI_execute_plan(plan, values, nulls, false, 0);

	if (ret != SPI_OK_INSERT_RETURNING)
		elog(ERROR, "expected SPI state %u, got %u", SPI_OK_INSERT_RETURNING, ret);

	elog(DEBUG1, "started %d elections", SPI_processed);
	processed = SPI_processed;

	PopActiveSnapshot();
	SPI_finish();
	CommitTransactionCommand();
	pgstat_report_stat(false);

	return processed > 0;
}

/*
 * Check whether enough votes have come in for any of *our* in progress
 * elections.
 */
void
bdr_sequencer_tally(void)
{
	static SPIPlanPtr plan;
	Oid			argtypes[5];
	Datum		values[5];
	char		nulls[5];
	char		local_sysid[32];
	int			ret;

	snprintf(local_sysid, sizeof(local_sysid), UINT64_FORMAT,
			 GetSystemIdentifier());

	StartTransactionCommand();
	SPI_connect();

	argtypes[0] = TEXTOID;
	nulls[0] = false;
	values[0] = CStringGetTextDatum(local_sysid);

	argtypes[1] = OIDOID;
	nulls[1] = false;
	values[1] = ObjectIdGetDatum(ThisTimeLineID);

	argtypes[2] = OIDOID;
	values[2] = ObjectIdGetDatum(MyDatabaseId);
	nulls[2] = false;

	argtypes[3] = TEXTOID;
	values[3] = CStringGetTextDatum("");
	nulls[3] = false;

	argtypes[4] = INT4OID;
	values[4] = Int32GetDatum(BdrSequencerCtl->slots[seq_slot].nnodes);
	nulls[4] = false;

	bdr_sequencer_lock();
	PushActiveSnapshot(GetTransactionSnapshot());

	if (plan == NULL)
	{
		plan = SPI_prepare(tally_elections_sql, 5, argtypes);
		SPI_keepplan(plan);
	}

	SetCurrentStatementStartTimestamp();
	pgstat_report_activity(STATE_RUNNING, "tally_elections");

	ret = SPI_execute_plan(plan, values, nulls, false, 0);

	if (ret != SPI_OK_SELECT)
		elog(ERROR, "expected SPI state %u, got %u", SPI_OK_SELECT, ret);

	elog(DEBUG1, "tallied %d elections", SPI_processed);

	PopActiveSnapshot();
	SPI_finish();
	CommitTransactionCommand();
	pgstat_report_stat(false);
}


static int
bdr_sequence_value_cmp(const void *a, const void *b)
{
	const		BdrSequenceValues *left = a;
	const		BdrSequenceValues *right = b;

	if (left->start_value < right->start_value)
		return -1;
	if (left->start_value == right->start_value)
		return 0;
	return 1;
}

/*
 * Replace a single (uninitialized or used up) chunk by a free one. Mark the
 * new chunk from bdr_sequence_values as in_use.
 *
 * Returns whether we could find a chunk or not.
 */
static bool
bdr_sequencer_fill_chunk(Oid seqoid, char *seqschema, char *seqname,
						 BdrSequenceValues * curval)
{
	static SPIPlanPtr plan;
	Oid			argtypes[6];
	Datum		values[6];
	char		nulls[6];
	char		local_sysid[32];
	int			ret;
	int64		lower,
				upper;
	bool		success;

	SPI_push();
	SPI_connect();

	snprintf(local_sysid, sizeof(local_sysid), UINT64_FORMAT,
			 GetSystemIdentifier());

	argtypes[0] = TEXTOID;
	nulls[0] = false;
	values[0] = CStringGetTextDatum(local_sysid);

	argtypes[1] = OIDOID;
	nulls[1] = false;
	values[1] = ObjectIdGetDatum(ThisTimeLineID);

	argtypes[2] = OIDOID;
	values[2] = ObjectIdGetDatum(MyDatabaseId);
	nulls[2] = false;

	argtypes[3] = TEXTOID;
	values[3] = CStringGetTextDatum("");
	nulls[3] = false;

	argtypes[4] = TEXTOID;
	values[4] = CStringGetTextDatum(seqschema);
	nulls[4] = false;

	argtypes[5] = TEXTOID;
	values[5] = CStringGetTextDatum(seqname);
	nulls[5] = false;

	SetCurrentStatementStartTimestamp();
	pgstat_report_activity(STATE_RUNNING, "get_chunk");

	if (plan == NULL)
	{
		plan = SPI_prepare(get_chunk_sql, 6, argtypes);
		SPI_keepplan(plan);
	}

	ret = SPI_execute_plan(plan, values, nulls, false, 0);
	if (ret != SPI_OK_UPDATE_RETURNING)
		elog(ERROR, "expected SPI state %u, got %u", SPI_OK_UPDATE_RETURNING, ret);

	if (SPI_processed != 1)
	{
		elog(DEBUG2, "no free chunks for sequence %s.%s",
			 seqschema, seqname);
		success = false;
	}
	else
	{
		HeapTuple	tup = SPI_tuptable->vals[0];
		bool		isnull;

		lower = DatumGetInt64(SPI_getbinval(tup, SPI_tuptable->tupdesc, 1, &isnull));
		Assert(!isnull);
		upper = DatumGetInt64(SPI_getbinval(tup, SPI_tuptable->tupdesc, 2, &isnull));
		Assert(!isnull);

		elog(DEBUG2, "got chunk [%zu, %zu) for sequence %s.%s",
			 lower, upper, seqschema, seqname);
		curval->start_value = lower;
		curval->next_value = lower;
		curval->end_value = upper;

		success = true;
	}
	SPI_finish();
	SPI_pop();

	return success;
}

/*
 * Search for used up chunks in one bdr sequence.
 */
static void
bdr_sequencer_fill_sequence(Oid seqoid, char *seqschema, char *seqname)
{
	Buffer		buf;
	SeqTable	elm;
	Relation	rel;
	HeapTupleData seqtuple;
	Datum		values[SEQ_COL_LASTCOL];
	bool		nulls[SEQ_COL_LASTCOL];
	HeapTuple	newtup;
	Page		page,
				temppage;
	BdrSequenceValues *curval,
			   *firstval;
	int			i;
	bool		acquired_new = false;
	LockRelId	heaprelid;
	LOCKTAG		heaplocktag;
	VirtualTransactionId *lockholders;

	/* lock page, fill heaptup */
	init_sequence(seqoid, &elm, &rel);
	(void) read_seq_tuple(elm, rel, &buf, &seqtuple);

	/* get values */
	heap_deform_tuple(&seqtuple, RelationGetDescr(rel),
					  values, nulls);

	/* now make sure we have space for our own data */
	if (nulls[SEQ_COL_AMDATA - 1])
	{
		struct varlena *vl = palloc0(VARHDRSZ + sizeof(BdrSequenceValues) * 10);

		SET_VARSIZE(vl, VARHDRSZ + sizeof(BdrSequenceValues) * 10);
		nulls[SEQ_COL_AMDATA - 1] = false;
		values[SEQ_COL_AMDATA - 1] = PointerGetDatum(vl);
	}

	firstval = (BdrSequenceValues *)
		VARDATA_ANY(DatumGetByteaP(values[SEQ_COL_AMDATA - 1]));
	curval = firstval;

	for (i = 0; i < 10; i++)
	{
		if (curval->next_value == curval->end_value)
		{
			if (curval->end_value > 0)
				elog(DEBUG1, "sequence %s.%s: used up old chunk",
					 seqschema, seqname);

			elog(DEBUG2, "sequence %s.%s: needs new batch %i",
				 seqschema, seqname, i);
			if (bdr_sequencer_fill_chunk(seqoid, seqschema, seqname, curval))
				acquired_new = true;
			else
				break;
		}
		curval++;
	}

	if (!acquired_new)
		goto done_with_sequence;

	/* sort chunks, so we always use the smallest one first */
	qsort(firstval, 10, sizeof(BdrSequenceValues), bdr_sequence_value_cmp);

	newtup = heap_form_tuple(RelationGetDescr(rel), values, nulls);

	/* special requirements for sequence tuples */
	HeapTupleHeaderSetXmin(newtup->t_data, FrozenTransactionId);
	HeapTupleHeaderSetXminFrozen(newtup->t_data);
	HeapTupleHeaderSetCmin(newtup->t_data, FirstCommandId);
	HeapTupleHeaderSetXmax(newtup->t_data, InvalidTransactionId);
	newtup->t_data->t_infomask |= HEAP_XMAX_INVALID;
	ItemPointerSet(&newtup->t_data->t_ctid, 0, FirstOffsetNumber);

	page = BufferGetPage(buf);
	temppage = PageGetTempPage(page);

	/* replace page contents, the direct way */
	PageInit(temppage, BufferGetPageSize(buf), PageGetSpecialSize(page));
	memcpy(PageGetSpecialPointer(temppage),
		   PageGetSpecialPointer(page),
		   PageGetSpecialSize(page));

	if (PageAddItem(temppage, (Item) newtup->t_data, newtup->t_len,
					FirstOffsetNumber, false, false) == InvalidOffsetNumber)
		elog(PANIC, "fill_sequence: failed to add item to page");

	PageSetLSN(temppage, PageGetLSN(page));

	START_CRIT_SECTION();

	MarkBufferDirty(buf);

	memcpy(page, temppage, BufferGetPageSize(buf));

	seqtuple.t_len = newtup->t_len;

	log_sequence_tuple(rel, &seqtuple, page);

	END_CRIT_SECTION();

done_with_sequence:

	UnlockReleaseBuffer(buf);
	heap_close(rel, NoLock);

	/*
	 * Check for backends who have the sequence open, they might be waiting
	 * for the voting to finish so let's signal them.
	 */
	if (acquired_new)
	{
		heaprelid = rel->rd_lockInfo.lockRelId;
		SET_LOCKTAG_RELATION(heaplocktag, heaprelid.dbId, heaprelid.relId);

		lockholders = GetLockConflicts(&heaplocktag, AccessExclusiveLock);

		while (VirtualTransactionIdIsValid(*lockholders))
		{
			PGPROC	   *proc = BackendIdGetProc(lockholders->backendId);

			if (proc)
				SetLatch(&proc->procLatch);
			lockholders++;
		}
	}
}

/*
 * Check whether all BDR sequences have enough values inline. If not, add
 * some. This should be called after tallying (so we have a better chance to
 * have enough chunks) but before starting new elections since we might use up
 * existing chunks.
 */
void
bdr_sequencer_fill_sequences(void)
{
	static SPIPlanPtr plan;
	Portal		cursor;
	int			total = 0;

	StartTransactionCommand();
	SPI_connect();

	bdr_sequencer_lock();
	PushActiveSnapshot(GetTransactionSnapshot());

	if (plan == NULL)
	{
		plan = SPI_prepare(fill_sequences_sql, 0, NULL);
		SPI_keepplan(plan);
	}

	SetCurrentStatementStartTimestamp();
	pgstat_report_activity(STATE_RUNNING, "fill_sequences");

	cursor = SPI_cursor_open("seq", plan, NULL, NULL, 0);

	SPI_cursor_fetch(cursor, true, 1);

	while (SPI_processed > 0)
	{
		HeapTuple	tup = SPI_tuptable->vals[0];
		bool		isnull;
		Datum		seqoid;
		Datum		seqschema;
		Datum		seqname;

		seqoid = SPI_getbinval(tup, SPI_tuptable->tupdesc, 1, &isnull);
		Assert(!isnull);
		seqschema = SPI_getbinval(tup, SPI_tuptable->tupdesc, 2, &isnull);
		Assert(!isnull);
		seqname = SPI_getbinval(tup, SPI_tuptable->tupdesc, 3, &isnull);
		Assert(!isnull);

		bdr_sequencer_fill_sequence(DatumGetObjectId(seqoid),
									NameStr(*DatumGetName(seqschema)),
									NameStr(*DatumGetName(seqname)));

		SPI_cursor_fetch(cursor, true, 1);

		total += 1;
	}

	PopActiveSnapshot();
	SPI_finish();
	CommitTransactionCommand();
	pgstat_report_stat(false);

	elog(DEBUG1, "checked %d sequences for filling", total);
}


/* check sequence.c */
#define SEQ_LOG_VALS	32

PGDLLEXPORT Datum bdr_sequence_alloc(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(bdr_sequence_alloc);

Datum
bdr_sequence_alloc(PG_FUNCTION_ARGS)
{
	Relation	seqrel = (Relation) PG_GETARG_POINTER(0);
	SeqTable	elm = (SeqTable) PG_GETARG_POINTER(1);
	Buffer		buf = (Buffer) PG_GETARG_INT32(2);
	HeapTuple	seqtuple = (HeapTuple) PG_GETARG_POINTER(3);
	Page		page;
	Form_pg_sequence seq;
	bool		logit = false;
	int64		cache,
				log,
				fetch,
				last;
	int64		result = 0;
	int64		next;
	Datum		values;
	bool		isnull;
	BdrSequenceValues *curval;
	int			i;
	bool		wakeup = false;
	int			retries = 0;

	page = BufferGetPage(buf);

retry:

	seq = (Form_pg_sequence) GETSTRUCT(seqtuple);

	values = fastgetattr(seqtuple, 11, RelationGetDescr(seqrel), &isnull);
	if (isnull)
		ereport(ERROR,
				(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
				 errmsg("global sequence %s.%s is not initialized yet",
						get_namespace_name(RelationGetNamespace(seqrel)),
						RelationGetRelationName(seqrel)),
				 errhint("All nodes must agree before the sequence is usable. "
						 "Try again soon. Check all nodes are up if the condition "
						 "persists.")));

	curval = (BdrSequenceValues *) VARDATA_ANY(DatumGetByteaP(values));

	Assert(seq->increment_by == 1);
	/* XXX: check min/max */

	last = next = seq->last_value;

	fetch = cache = seq->cache_value;
	log = seq->log_cnt;

	/* check whether value can be satisfied without logging again */
	if (log < fetch || !seq->is_called || PageGetLSN(page) <= GetRedoRecPtr())
	{
		/* forced log to satisfy local demand for values */
		fetch = log = fetch + SEQ_LOG_VALS;
		logit = true;
	}

	/*
	 * try to fetch cache [+ log ] numbers, check all 10 possible chunks
	 */
	for (i = 0; i < 10; i++)
	{
		/* redo recovered after crash */
		if (seq->last_value >= curval->next_value &&
			seq->last_value < curval->end_value)
		{
			curval->next_value = seq->last_value + 1;
		}

		/* chunk empty */
		if (curval->next_value >= curval->end_value)
		{
			curval++;
			wakeup = true;
			continue;
		}


		/* there's space in current chunk, use it */
		result = curval->next_value;

		/* but not enough for all ..log values */
		if (result + log >= curval->end_value)
		{
			log = curval->end_value - curval->next_value;
			wakeup = true;
			logit = true;
		}

		/* but not enough for all ..cached values */
		last = result + cache - 1;
		if (last >= curval->end_value)
		{
			last = curval->end_value - 1;
			wakeup = true;
			logit = true;
		}

		curval->next_value = last;
		break;
	}

	/*
	 * XXX: This is hacky and only works with current bdr-pg but we'll need
	 * sequence rewrite to do things properly anyway so we'll live with it for
	 * now.
	 */
	if (result == 0)
	{
		ItemId		lp;
		int			rc;

		bdr_sequencer_wakeup();
		CHECK_FOR_INTERRUPTS();

		/* Give voting chance to progress (10 sleep). */
		LockBuffer(buf, BUFFER_LOCK_UNLOCK);
		rc = WaitLatch(&MyProc->procLatch,
					   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
					   10000L);
		ResetLatch(&MyProc->procLatch);
		LockBuffer(buf, BUFFER_LOCK_EXCLUSIVE);

		/* emergency bailout if postmaster has died */
		if (rc & WL_POSTMASTER_DEATH)
			proc_exit(1);

		/* Reread the tuple. */
		page = BufferGetPage(buf);
		lp = PageGetItemId(page, FirstOffsetNumber);
		Assert(ItemIdIsNormal(lp));

		/* Note we currently only bother to set these two fields of *seqtuple */
		seqtuple->t_data = (HeapTupleHeader) PageGetItem(page, lp);
		seqtuple->t_len = ItemIdGetLength(lp);

		/*
		 * No point in trying this forever. We waited for a minute by now so
		 * let's bail.
		 */
		if (retries++ > 5)
		{
			bdr_schedule_eoxact_sequencer_wakeup();

			ereport(ERROR,
					(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
					 errmsg("could not find free sequence value for global sequence %s.%s",
							get_namespace_name(RelationGetNamespace(seqrel)),
							RelationGetRelationName(seqrel)),
					 errhint("The sequence is refilling from remote nodes. Try again soon. "
							 "Check that all nodes are up if the condition persists.")));
		}

		goto retry;
	}

	if (wakeup)
	{
		bdr_sequencer_wakeup();
		bdr_schedule_eoxact_sequencer_wakeup();
	}

	next = result + log - 1;

	elm->last = result;
	elm->cached = result;
	elm->last_valid = true;

	/* ready to change the on-disk (or really, in-buffer) tuple */
	START_CRIT_SECTION();

	/*
	 * We must mark the buffer dirty before doing XLogInsert(); see notes in
	 * SyncOneBuffer().  However, we don't apply the desired changes just yet.
	 * This looks like a violation of the buffer update protocol, but it is in
	 * fact safe because we hold exclusive lock on the buffer.  Any other
	 * process, including a checkpoint, that tries to examine the buffer
	 * contents will block until we release the lock, and then will see the
	 * final state that we install below.
	 */
	MarkBufferDirty(buf);

	if (logit)
	{
		/*
		 * We don't log the current state of the tuple, but rather the state
		 * as it would appear after "log" more fetches.  This lets us skip
		 * that many future WAL records, at the cost that we lose those
		 * sequence values if we crash.
		 */
		seq->last_value = next;
		seq->is_called = true;
		seq->log_cnt = 0;
		log_sequence_tuple(seqrel, seqtuple, page);
	}

	/* Now update sequence tuple to the intended final state */
	seq->last_value = elm->last;	/* last fetched number */
	seq->is_called = true;
	seq->log_cnt = log - 1;		/* how much is logged */

	result = elm->last;

	END_CRIT_SECTION();

	/* schedule wakeup as soon as other xacts can see the sequence */
	bdr_schedule_eoxact_sequencer_wakeup();

	PG_RETURN_VOID();
}

PGDLLEXPORT Datum bdr_sequence_setval(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(bdr_sequence_setval);

Datum
bdr_sequence_setval(PG_FUNCTION_ARGS)
{
	Relation	seqrel = (Relation) PG_GETARG_POINTER(0);
	Buffer		buf = (Buffer) PG_GETARG_INT32(2);
	HeapTuple	seqtuple = (HeapTuple) PG_GETARG_POINTER(3);
	int64		next = PG_GETARG_INT64(4);
	bool		iscalled = PG_GETARG_BOOL(5);
	Page		page = BufferGetPage(buf);
	Form_pg_sequence seq = (Form_pg_sequence) GETSTRUCT(seqtuple);

	if (seq->last_value != next ||
		seq->is_called != iscalled)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot call setval() on global sequence %s.%s ",
						get_namespace_name(RelationGetNamespace(seqrel)),
						RelationGetRelationName(seqrel))));


	/* ready to change the on-disk (or really, in-buffer) tuple */
	START_CRIT_SECTION();

	/* set is_called, all AMs should need to do this */
	seq->is_called = iscalled;
	seq->last_value = next;		/* last fetched number */
	seq->log_cnt = 0;

	MarkBufferDirty(buf);

	log_sequence_tuple(seqrel, seqtuple, page);

	END_CRIT_SECTION();

	/* schedule wakeup as soon as other xacts can see the seuqence */
	bdr_schedule_eoxact_sequencer_wakeup();

	PG_RETURN_VOID();
}

PGDLLEXPORT Datum bdr_sequence_options(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(bdr_sequence_options);

typedef struct BDRSeqOptions
{
	int32		vl_len_;		/* varlena header (do not touch directly!) */
	int			cache_chunks;
}			BDRSeqOptions;

Datum
bdr_sequence_options(PG_FUNCTION_ARGS)
{
	Datum		reloptions = PG_GETARG_DATUM(0);
	bool		validate = PG_GETARG_BOOL(1);
	relopt_value *options;
	BDRSeqOptions *sopts;
	int			numoptions;
	static const relopt_parse_elt tab[] = {
		{"cache_chunks", RELOPT_TYPE_INT,
		offsetof(BDRSeqOptions, cache_chunks)}
	};

	options = parseRelOptions(reloptions, validate, bdr_seq_relopt_kind, &numoptions);

	/* if none set, we're done */
	if (numoptions == 0)
		PG_RETURN_NULL();

	sopts = allocateReloptStruct(sizeof(BDRSeqOptions), options, numoptions);

	fillRelOptions((void *) sopts, sizeof(BDRSeqOptions), options, numoptions,
				   validate, tab, lengthof(tab));

	pfree(options);

	/* schedule wakeup as soon as other xacts can see the seuqence */
	bdr_schedule_eoxact_sequencer_wakeup();

	if (sopts)
		PG_RETURN_BYTEA_P((bytea *) sopts);

	PG_RETURN_NULL();
}


/*
 * Resets the sequence cache, needed for initialization from binary clone.
 */
PGDLLEXPORT Datum bdr_internal_sequence_reset_cache(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(bdr_internal_sequence_reset_cache);

Datum
bdr_internal_sequence_reset_cache(PG_FUNCTION_ARGS)
{
	Oid			seqoid = PG_GETARG_OID(0);
	Buffer		buf;
	SeqTable	elm;
	Relation	rel;
	HeapTupleData seqtuple;
	Datum		values[SEQ_COL_LASTCOL];
	bool		nulls[SEQ_COL_LASTCOL];
	HeapTuple	newtup;
	Page		page,
				temppage;

	/* lock page, fill heaptup */
	init_sequence(seqoid, &elm, &rel);
	(void) read_seq_tuple(elm, rel, &buf, &seqtuple);

	/* get values */
	heap_deform_tuple(&seqtuple, RelationGetDescr(rel),
					  values, nulls);

	/* if already null nothing needs to be done */
	if (nulls[SEQ_COL_AMDATA - 1])
		goto done_with_sequence;

	/* otherwise create new tuple with null amdata and save it */
	nulls[SEQ_COL_AMDATA - 1] = true;
	newtup = heap_form_tuple(RelationGetDescr(rel), values, nulls);

	/* special requirements for sequence tuples */
	HeapTupleHeaderSetXmin(newtup->t_data, FrozenTransactionId);
	HeapTupleHeaderSetXminFrozen(newtup->t_data);
	HeapTupleHeaderSetCmin(newtup->t_data, FirstCommandId);
	HeapTupleHeaderSetXmax(newtup->t_data, InvalidTransactionId);
	newtup->t_data->t_infomask |= HEAP_XMAX_INVALID;
	ItemPointerSet(&newtup->t_data->t_ctid, 0, FirstOffsetNumber);

	page = BufferGetPage(buf);
	temppage = PageGetTempPage(page);

	/* replace page contents, the direct way */
	PageInit(temppage, BufferGetPageSize(buf), PageGetSpecialSize(page));
	memcpy(PageGetSpecialPointer(temppage),
		   PageGetSpecialPointer(page),
		   PageGetSpecialSize(page));

	if (PageAddItem(temppage, (Item) newtup->t_data, newtup->t_len,
					FirstOffsetNumber, false, false) == InvalidOffsetNumber)
		elog(PANIC, "reset_sequence_cache: failed to add item to page");

	PageSetLSN(temppage, PageGetLSN(page));

	START_CRIT_SECTION();

	MarkBufferDirty(buf);

	memcpy(page, temppage, BufferGetPageSize(buf));

	seqtuple.t_len = newtup->t_len;

	log_sequence_tuple(rel, &seqtuple, page);

	END_CRIT_SECTION();

done_with_sequence:

	UnlockReleaseBuffer(buf);
	heap_close(rel, NoLock);

	bdr_sequencer_wakeup();
	bdr_schedule_eoxact_sequencer_wakeup();

	PG_RETURN_VOID();
}


void
filter_CreateBdrSeqStmt(CreateSeqStmt *stmt)
{
	ListCell   *param;

	if (stmt->accessMethod == NULL || strcmp(stmt->accessMethod, "bdr") != 0)
		return;

	foreach(param, stmt->options)
	{
		DefElem    *defel = (DefElem *) lfirst(param);

		if (strcmp(defel->defname, "owned_by") != 0 &&
			strcmp(defel->defname, "start") != 0)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("CREATE SEQUENCE ... %s is not supported for bdr sequences",
							defel->defname)));
	}
}

void
filter_AlterBdrSeqStmt(AlterSeqStmt *stmt, Oid seqoid)
{
	Oid			seqamid;
	Form_pg_class pgcform;
	ListCell   *param;
	HeapTuple	ctup;

	seqamid = get_seqam_oid("bdr", true);

	/* No bdr sequences? */
	if (seqamid == InvalidOid)
		return;

	/* Fetch a tuple to check for relam */
	ctup = SearchSysCache1(RELOID, ObjectIdGetDatum(seqoid));
	if (!HeapTupleIsValid(ctup))
		elog(ERROR, "pg_class entry for sequence %u unavailable",
			 seqoid);
	pgcform = (Form_pg_class) GETSTRUCT(ctup);

	/* Not bdr sequence */
	if (pgcform->relam != seqamid)
	{
		ReleaseSysCache(ctup);
		return;
	}

	ReleaseSysCache(ctup);

	foreach(param, stmt->options)
	{
		DefElem    *defel = (DefElem *) lfirst(param);

		if (strcmp(defel->defname, "owned_by") != 0)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("ALTER SEQUENCE ... %s is not supported for bdr sequences",
							defel->defname)));
	}
}

void
bdr_maintain_seq_schema(Oid schema_oid)
{
	BdrVotesRelid =
		bdr_lookup_relid("bdr_votes", schema_oid);
	BdrSequenceValuesRelid =
		bdr_lookup_relid("bdr_sequence_values", schema_oid);
	BdrSequenceElectionsRelid =
		bdr_lookup_relid("bdr_sequence_elections", schema_oid);
	BdrSeqamOid = get_seqam_oid("bdr", false);
}
