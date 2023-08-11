/* -------------------------------------------------------------------------
 *
 * bdr_apply.c
 *		Replication!!!
 *
 * Replication???
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		bdr_apply.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include <unistd.h>

#include "bdr.h"
#include "bdr_locks.h"
#include "bdr_messaging.h"

#include "funcapi.h"
#include "libpq-fe.h"
#include "miscadmin.h"
#include "pgstat.h"

#include "access/commit_ts.h"
#include "access/htup_details.h"
#include "access/relscan.h"
#include "access/skey.h"
#include "access/xact.h"

#include "catalog/catversion.h"
#include "catalog/dependency.h"
#include "catalog/index.h"
#include "catalog/namespace.h"
#include "catalog/objectaddress.h"
#include "catalog/pg_type.h"

#include "executor/executor.h"
#include "executor/spi.h"

#include "libpq/pqformat.h"

#include "mb/pg_wchar.h"

#include "parser/parse_type.h"

#include "replication/logical.h"
#include "replication/origin.h"

#include "storage/ipc.h"
#include "storage/lmgr.h"
#include "storage/lwlock.h"
#include "storage/proc.h"

#include "tcop/pquery.h"
#include "tcop/tcopprot.h"
#include "tcop/utility.h"

#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/datetime.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/snapmgr.h"

/* Useful for development:
#define VERBOSE_INSERT
#define VERBOSE_DELETE
#define VERBOSE_UPDATE
*/

/* Relation oid cache; initialized then left unchanged */
Oid			QueuedDDLCommandsRelid = InvalidOid;
Oid			QueuedDropsRelid = InvalidOid;

/* Global apply worker state */
BDRNodeId	origin;
bool		started_transaction = false;

/* During apply, holds xid of remote transaction */
TransactionId replication_origin_xid = InvalidTransactionId;

/*
 * For tracking of the remote origin's information when in catchup mode
 * (BDR_OUTPUT_TRANSACTION_HAS_ORIGIN).
 */
static BDRNodeId remote_origin;
static XLogRecPtr remote_origin_lsn = InvalidXLogRecPtr;

/* The local identifier for the remote's origin, if any. */
static RepOriginId remote_origin_id = InvalidRepOriginId;

/*
 * A message counter for the xact, for debugging. We don't send
 * the remote change LSN with messages, so this aids identification
 * of which change causes an error.
 */
static uint32 xact_action_counter;

/*
 * This code only runs within an apply bgworker, so we can stash a pointer to our
 * state in shm in a global for convenient access.
 */
static BdrApplyWorker * bdr_apply_worker = NULL;

static BdrConnectionConfig * bdr_apply_config = NULL;

dlist_head	bdr_lsn_association = DLIST_STATIC_INIT(bdr_lsn_association);

struct ActionErrCallbackArg
{
	const char *action_name;
	const char *remote_nspname;
	const char *remote_relname;
	bool		is_ddl_or_drop;
	bool		suppress_output;
};

static BDRRelation * read_rel(StringInfo s, LOCKMODE mode, struct ActionErrCallbackArg *cbarg);
static void read_tuple_parts(StringInfo s, BDRRelation * rel, BDRTupleData * tup);

static void check_apply_update(BdrConflictType conflict_type,
							   RepOriginId local_node_id, TimestampTz local_ts,
							   BDRRelation * rel, HeapTuple local_tuple,
							   HeapTuple remote_tuple, HeapTuple *new_tuple,
							   bool *perform_update, bool *log_update,
							   BdrConflictResolution * resolution);

static void check_bdr_wakeups(BDRRelation * rel);
static HeapTuple process_queued_drop(HeapTuple cmdtup);
static void process_queued_ddl_command(HeapTuple cmdtup, bool tx_just_started);
static bool bdr_performing_work(void);

static void process_remote_begin(StringInfo s);
static void process_remote_commit(StringInfo s);
static void process_remote_insert(StringInfo s);
static void process_remote_update(StringInfo s);
static void process_remote_delete(StringInfo s);

static void get_local_tuple_origin(HeapTuple tuple,
								   TimestampTz *commit_ts,
								   RepOriginId *node_id);
static void abs_timestamp_difference(TimestampTz start_time,
									 TimestampTz stop_time,
									 long *secs, int *microsecs);

#if defined(VERBOSE_INSERT) || defined(VERBOSE_UPDATE) || defined(VERBOSE_DELETE)
static void log_tuple(const char *format, TupleDesc desc, HeapTuple tup);
#endif

static void
format_action_description(
						  StringInfo si,
						  const char *action_name,
						  const char *remote_nspname,
						  const char *remote_relname,
						  bool is_ddl_or_drop)
{
	appendStringInfoString(si, "apply ");
	appendStringInfoString(si, action_name);

	if (remote_nspname != NULL
		&& remote_relname != NULL
		&& !is_ddl_or_drop)
	{
		appendStringInfo(si, " from remote relation %s.%s",
						 remote_nspname, remote_relname);
	}

	appendStringInfo(si,
					 " in commit before %X/%X, xid %u commited at %s (action #%u)",
					 LSN_FORMAT_ARGS(replorigin_session_origin_lsn),
					 replication_origin_xid,
					 timestamptz_to_str(replorigin_session_origin_timestamp),
					 xact_action_counter);

	if (replorigin_session_origin != InvalidRepOriginId)
	{
		appendStringInfo(si, " from node " BDR_NODEID_FORMAT_WITHNAME,
						 BDR_NODEID_FORMAT_WITHNAME_ARGS(origin));
	}

	if (remote_origin_id != InvalidRepOriginId)
	{
		appendStringInfo(si, " forwarded from commit %X/%X on node " BDR_NODEID_FORMAT_WITHNAME,
						 LSN_FORMAT_ARGS(remote_origin_lsn),
						 BDR_NODEID_FORMAT_WITHNAME_ARGS(remote_origin));
	}
}

static void
action_error_callback(void *arg)
{
	struct ActionErrCallbackArg *action = (struct ActionErrCallbackArg *) arg;
	StringInfoData si;

	if (!action->suppress_output)
	{
		initStringInfo(&si);

		format_action_description(&si,
								  action->action_name,
								  action->remote_nspname,
								  action->remote_relname,
								  action->is_ddl_or_drop);

		errcontext("%s", si.data);
	}
}

static void
process_remote_begin(StringInfo s)
{
	XLogRecPtr	commit_afterend_lsn;
	TimestampTz committime;
	TransactionId remote_xid;
	char		statbuf[100];
	int			apply_delay = bdr_apply_config->apply_delay;
	int			flags = 0;
	ErrorContextCallback errcallback;
	struct ActionErrCallbackArg cbarg;

	Assert(bdr_apply_worker != NULL);

	xact_action_counter = 1;
	memset(&cbarg, 0, sizeof(struct ActionErrCallbackArg));
	cbarg.action_name = "BEGIN";
	errcallback.callback = action_error_callback;
	errcallback.arg = &cbarg;
	errcallback.previous = error_context_stack;
	error_context_stack = &errcallback;

	started_transaction = false;
	remote_origin_id = InvalidRepOriginId;

	flags = pq_getmsgint(s, 4);

	/*
	 * This is the LSN of the end of the commit xlog record + 1, even though
	 * we're in BEGIN. We have it because we process the whole reorder buffer
	 * only at commit time.
	 */
	commit_afterend_lsn = pq_getmsgint64(s);
	Assert(commit_afterend_lsn != InvalidXLogRecPtr);
	committime = pq_getmsgint64(s);
	remote_xid = pq_getmsgint(s, 4);

	if (flags & BDR_OUTPUT_TRANSACTION_HAS_ORIGIN)
	{
		bdr_getmsg_nodeid(s, &remote_origin, false);
		remote_origin_lsn = pq_getmsgint64(s);
	}
	else
	{
		/* Transaction originated directly from remote node */
		remote_origin.sysid = 0;
		remote_origin.timeline = 0;
		remote_origin.dboid = InvalidOid;
		remote_origin_lsn = InvalidXLogRecPtr;
	}


	/*
	 * Set up state for commit and conflict detection. The timestamp will be
	 * recorded as the replicated xact's commit timestamp, and the LSN will be
	 * used to advance the replication origin for the node at local COMMIT
	 * time. Set the remote LSN to the end of the remote commit record + 1 so
	 * we know exactly when the next record to start processing is.
	 *
	 * This means that replorigin_session_origin_lsn and our replication
	 * origin doesn't actually point to the last-processed commit record, but
	 * just after it.
	 */
	replorigin_session_origin_lsn = commit_afterend_lsn;
	replorigin_session_origin_timestamp = committime;

	/* store remote xid for logging and debugging */
	replication_origin_xid = remote_xid;

	snprintf(statbuf, sizeof(statbuf),
			 "bdr_apply: BEGIN origin(orig_lsn, timestamp): %X/%X, %s",
			 LSN_FORMAT_ARGS(replorigin_session_origin_lsn),
			 timestamptz_to_str(committime));

	pgstat_report_activity(STATE_RUNNING, statbuf);

	if (apply_delay == -1)
		apply_delay = bdr_default_apply_delay;

	/*
	 * If we're in catchup mode, see if this transaction is relayed from
	 * elsewhere and prepare to advance the appropriate replication origin.
	 */
	if (flags & BDR_OUTPUT_TRANSACTION_HAS_ORIGIN)
	{
		char	   *remote_ident;
		MemoryContext old_ctx;
		BDRNodeId	my_nodeid;

		bdr_make_my_nodeid(&my_nodeid);

		if (bdr_nodeid_eq(&remote_origin, &my_nodeid))
		{
			/*
			 * This might not have to be an error condition, but we don't cope
			 * with it for now and it shouldn't arise for use of catchup mode
			 * for init_replica.
			 */
			ereport(ERROR,
					(errmsg("replication loop in catchup mode"),
					 errdetail("Received a transaction from the remote node that originated on this node.")));
		}

		/*
		 * To determine whether the commit was forwarded by the upstream from
		 * another node, we need to get the local RepOriginId for that node
		 * based on the (sysid, timelineid, dboid) supplied in catchup mode.
		 */
		remote_ident = bdr_replident_name(&remote_origin, MyDatabaseId);

		old_ctx = CurrentMemoryContext;
		StartTransactionCommand();
		remote_origin_id = replorigin_by_name(remote_ident, false);
		CommitTransactionCommand();
		MemoryContextSwitchTo(old_ctx);

		pfree(remote_ident);
	}

	if (bdr_trace_replay)
	{
		StringInfoData si;

		initStringInfo(&si);
		format_action_description(&si, "BEGIN", NULL, NULL, false);
		cbarg.suppress_output = true;
		elog(LOG, "TRACE: %s", si.data);
		cbarg.suppress_output = false;
		pfree(si.data);
	}

	/* don't want the overhead otherwise */
	if (apply_delay > 0)
	{
		/* loop in case we're woken early in a sleep by an interrupt */
		while (true)
		{
			long		sec;
			int			usec;
			int			ret;
			long		delay_ms;
			TimestampTz current;

			current = GetCurrentTimestamp();

			/*
			 * Some amount of clock drift/skew is normal, so we must handle
			 * remote commits that are in the future according to our local
			 * clock.
			 */
			if (current < replorigin_session_origin_timestamp)
			{
				TimestampDifference(replorigin_session_origin_timestamp, current,
									&sec, &usec);

				/* ignore small skews */
				if (sec > 1)
					ereport(WARNING,
							(errmsg("clock skew detected: node " BDR_NODEID_FORMAT_WITHNAME " clock is ahead of local clock by at least %ld.%03d seconds",
									BDR_NODEID_FORMAT_WITHNAME_ARGS(origin), sec,
									usec / 1000)));

				/*
				 * Now clamp the delay to max apply delay. If we're woken
				 * mid-sleep this could mean we repeat the warning and wait
				 * longer than apply_delay but ... don't have your clocks
				 * ahead, then!
				 */
				delay_ms = apply_delay;
			}
			else
			{
				current = TimestampTzPlusMilliseconds(current,
													  -apply_delay);

				TimestampDifference(current, replorigin_session_origin_timestamp,
									&sec, &usec);

				/*
				 * WaitLatch doesn't support > INT_MAX ms, including any us
				 * component, and we have to guard against overflow anyway.
				 */
				if (sec >= (INT_MAX / 1000 - 1000))
				{
					elog(WARNING, "ignoring absurd remote commit timestamp and/or apply_delay");
					delay_ms = 0;
				}
				else
					delay_ms = sec * 1000 + usec / 1000L;

				/* TimestampDifference returns 0 if start >= end, so: */
				if (delay_ms == 0)
					break;
			}

			ret = WaitLatch(&MyProc->procLatch,
							WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
							delay_ms, PG_WAIT_EXTENSION);

			if (ret & WL_POSTMASTER_DEATH)
				proc_exit(1);

			ResetLatch(&MyProc->procLatch);

			CHECK_FOR_INTERRUPTS();
		}
	}

	if (error_context_stack == &errcallback)
		error_context_stack = errcallback.previous;
}

/*
 * Process a commit message from the output plugin, advance replication
 * identifiers, commit the local transaction, and determine whether replay
 * should continue.
 *
 * Returns true if apply should continue with the next record, false if replay
 * should stop after this record.
 */
static void
process_remote_commit(StringInfo s)
{
	XLogRecPtr	commit_lsn PG_USED_FOR_ASSERTS_ONLY;
	TimestampTz committime PG_USED_FOR_ASSERTS_ONLY;
	XLogRecPtr	commit_afterend_lsn;
	int			flags;
	ErrorContextCallback errcallback;
	struct ActionErrCallbackArg cbarg;

	Assert(bdr_apply_worker != NULL);

	xact_action_counter++;
	memset(&cbarg, 0, sizeof(struct ActionErrCallbackArg));
	cbarg.action_name = "COMMIT";
	errcallback.callback = action_error_callback;
	errcallback.arg = &cbarg;
	errcallback.previous = error_context_stack;
	error_context_stack = &errcallback;

	flags = pq_getmsgint(s, 4);

	if (flags != 0)
		elog(ERROR, "commit flags are currently unused, but flags was set to %i", flags);

	/* order of access to fields after flags is important */
	commit_lsn = pq_getmsgint64(s); /* start of commit record; not used
									 * anymore */
	commit_afterend_lsn = pq_getmsgint64(s);	/* end of commit record + 1 */
	committime = pq_getmsgint64(s);

	if (bdr_trace_replay)
	{
		StringInfoData si;

		initStringInfo(&si);
		format_action_description(&si, "COMMIT", NULL, NULL, false);
		cbarg.suppress_output = true;
		elog(LOG, "TRACE: %s", si.data);
		cbarg.suppress_output = false;
		pfree(si.data);
	}

	Assert(committime == replorigin_session_origin_timestamp);

	Assert(replorigin_session_origin_lsn == commit_afterend_lsn /* bdr 2.0 msg */
		   || replorigin_session_origin_lsn == commit_lsn); /* bdr 1.0 msg */

	/*
	 * BDR 1.0 used to send the start-of-commit lsn (commit_lsn) in BEGIN, not
	 * the position of the end of the commit record, and we might have used
	 * that in the replorigin settings if that's all we had.
	 *
	 * That's wrong; we're supposed to use end-of-commit + 1. But with BDR 1.0
	 * we don't have that information. To protect against replaying the same
	 * commit again, report that we've flushed at least 1 byte past
	 * start-of-commit.
	 */
	if (replorigin_session_origin_lsn == commit_lsn)
		replorigin_session_origin_lsn += 1;

	if (started_transaction)
	{
		BdrFlushPosition *flushpos;

		CommitTransactionCommand();
		MemoryContextSwitchTo(MessageContext);

		/*
		 * Associate the end of the remote commit lsn with the local end of
		 * the commit record.
		 */
		flushpos = (BdrFlushPosition *)
			MemoryContextAlloc(TopMemoryContext, sizeof(BdrFlushPosition));
		flushpos->local_end = XactLastCommitEnd;
		/* Feedback is supposed to be the last flushed LSN + 1 */
		flushpos->remote_end = replorigin_session_origin_lsn;

		dlist_push_tail(&bdr_lsn_association, &flushpos->node);

		/* report stats, only relevant if something was actually written */
		pgstat_report_stat(false);
	}

	pgstat_report_activity(STATE_IDLE, NULL);

	/*
	 * We set the session origin flush point up as
	 * replorigin_session_origin_lsn in process_remote_begin, so no further
	 * action is required here. XactLogCommitRecord(...) will write out the
	 * replorigin_session_origin_lsn and advance our session's replication
	 * origin in-memory state accordingly.
	 *
	 * However, if we're in catchup mode, see if the commit is relayed from
	 * elsewhere and advance the replication origin corresponding to the
	 * appropriate node, since that won't get advanced automatically on
	 * commit.
	 */
	if (remote_origin_id != InvalidRepOriginId &&
		remote_origin_id != replorigin_session_origin)
	{
		/*
		 * The row isn't from the immediate upstream; advance the slot of the
		 * node it originally came from so we start replay of that node's
		 * change data at the right place.
		 */
		replorigin_advance(remote_origin_id, remote_origin_lsn,
						   XactLastCommitEnd, false, true);
	}

	CurrentResourceOwner = bdr_saved_resowner;

	bdr_count_commit();

	replication_origin_xid = InvalidTransactionId;
	replorigin_session_origin_lsn = InvalidXLogRecPtr;
	replorigin_session_origin_timestamp = 0;

	xact_action_counter = 0;

	/*
	 * Stop replay if we're doing limited replay and we've replayed up to the
	 * last record we're supposed to process. Since the end lsn points to the
	 * start of the next record, we should stop if replay equals it.
	 */
	if (bdr_apply_worker->replay_stop_lsn != InvalidXLogRecPtr
		&& bdr_apply_worker->replay_stop_lsn <= commit_afterend_lsn)
	{
		ereport(LOG,
				(errmsg("bdr apply finished processing; replayed up to %X/%X of required %X/%X",
						LSN_FORMAT_ARGS(commit_afterend_lsn),
						LSN_FORMAT_ARGS(bdr_apply_worker->replay_stop_lsn))));

		/*
		 * We clear the replay_stop_lsn field to indicate successful catchup,
		 * so we don't need a separate flag field in shmem for all apply
		 * workers.
		 */
		bdr_apply_worker->replay_stop_lsn = InvalidXLogRecPtr;

		/*
		 * flush all writes so the latest position can be reported back to the
		 * sender
		 */
		XLogFlush(GetXLogWriteRecPtr());

		/* Stop gracefully */
		proc_exit(0);
	}

	if (error_context_stack == &errcallback)
		error_context_stack = errcallback.previous;
}

static void
process_remote_insert(StringInfo s)
{
	char		action;
	EState	   *estate;
	BDRTupleData new_tuple;
	TupleTableSlot *newslot;
	TupleTableSlot *oldslot;
	BDRRelation *rel;
	bool		started_tx;
	ResultRelInfo *relinfo = makeNode(ResultRelInfo);
	ItemPointer conflicts;
	bool		conflict = false;
	ScanKey    *index_keys;
	int			i;
	ItemPointerData conflicting_tid;
	ErrorContextCallback errcallback;
	struct ActionErrCallbackArg cbarg;

	ItemPointerSetInvalid(&conflicting_tid);

	xact_action_counter++;
	memset(&cbarg, 0, sizeof(struct ActionErrCallbackArg));
	cbarg.action_name = "INSERT";
	errcallback.callback = action_error_callback;
	errcallback.arg = &cbarg;
	errcallback.previous = error_context_stack;
	error_context_stack = &errcallback;

	started_tx = bdr_performing_work();

	Assert(bdr_apply_worker != NULL);

	rel = read_rel(s, RowExclusiveLock, &cbarg);

	if (bdr_trace_replay)
	{
		StringInfoData si;

		initStringInfo(&si);
		format_action_description(&si, "INSERT",
								  cbarg.remote_nspname, cbarg.remote_relname, false);
		cbarg.suppress_output = true;
		elog(LOG, "TRACE: %s", si.data);
		cbarg.suppress_output = false;
		pfree(si.data);
	}

	action = pq_getmsgbyte(s);
	if (action != 'N')
		elog(ERROR, "expected new tuple but got %d", action);

	estate = bdr_create_rel_estate(rel->rel, relinfo);

#if PG_VERSION_NUM >= 120000
	oldslot = table_slot_create(rel->rel, &estate->es_tupleTable);
#else
	oldslot = ExecInitExtraTupleSlotBdr(estate, NULL);
	ExecSetSlotDescriptor(oldslot, RelationGetDescr(rel->rel));
#endif

	newslot = ExecInitExtraTupleSlotBdr(estate, NULL);
	ExecSetSlotDescriptor(newslot, RelationGetDescr(rel->rel));

	read_tuple_parts(s, rel, &new_tuple);
	{
		HeapTuple	tup;

		tup = heap_form_tuple(RelationGetDescr(rel->rel),
							  new_tuple.values, new_tuple.isnull);
		ExecStoreHeapTuple(tup, newslot, true);
	}

	if (rel->rel->rd_rel->relkind != RELKIND_RELATION)
		elog(ERROR, "unexpected relkind '%c' rel \"%s\"",
			 rel->rel->rd_rel->relkind, RelationGetRelationName(rel->rel));

	/* debug output */
#ifdef VERBOSE_INSERT
	log_tuple("INSERT:%s", RelationGetDescr(rel->rel), newslot->tts_tuple);
#endif

	/*
	 * Search for conflicting tuples.
	 */
	ExecOpenIndices(relinfo, false);

	index_keys = palloc0(relinfo->ri_NumIndices * sizeof(ScanKeyData *));
	conflicts = palloc0(relinfo->ri_NumIndices * sizeof(ItemPointerData));

	build_index_scan_keys(relinfo, index_keys, &new_tuple);

	/* do a SnapshotDirty search for conflicting tuples */
	for (i = 0; i < relinfo->ri_NumIndices; i++)
	{
		IndexInfo  *ii = relinfo->ri_IndexRelationInfo[i];
		bool		found = false;

		/*
		 * Only unique indexes are of interest here, and we can't deal with
		 * expression indexes so far. FIXME: predicates should be handled
		 * better.
		 *
		 * NB: Needs to match expression in build_index_scan_key
		 */
		if (!ii->ii_Unique || ii->ii_Expressions != NIL)
			continue;

		if (index_keys[i] == NULL)
			continue;

		Assert(ii->ii_Expressions == NIL);

		/* if conflict: wait */
		found = find_pkey_tuple(index_keys[i],
								rel, relinfo->ri_IndexRelationDescs[i],
								oldslot, true, LockTupleExclusive);

		/* alert if there's more than one conflicting unique key */
		if (found &&
			ItemPointerIsValid(&conflicting_tid) &&
#if PG_VERSION_NUM >= 120000
			!ItemPointerEquals(&(oldslot->tts_tid),
#else
			!ItemPointerEquals(&oldslot->tts_tuple->t_self,
#endif
							   &conflicting_tid))
		{
			/* TODO: Report tuple identity in log */
			ereport(ERROR,
					(errcode(ERRCODE_UNIQUE_VIOLATION),
					 errmsg("multiple unique constraints violated by remotely INSERTed tuple"),
					 errdetail("Cannot apply transaction because remotely INSERTed tuple "
							   "conflicts with a local tuple on more than one UNIQUE "
							   "constraint and/or PRIMARY KEY."),
					 errhint("Resolve the conflict by removing or changing the conflicting local tuple.")));
		}
		else if (found)
		{
#if PG_VERSION_NUM >= 120000
			ItemPointerCopy(&(oldslot->tts_tid), &conflicting_tid);
#else
			ItemPointerCopy(&oldslot->tts_tuple->t_self, &conflicting_tid);
#endif
			conflict = true;
			break;
		}
		else
			ItemPointerSetInvalid(&conflicts[i]);

		CHECK_FOR_INTERRUPTS();
	}

	PushActiveSnapshot(GetTransactionSnapshot());

	/*
	 * If there's a conflict use the version created later, otherwise do a
	 * plain insert.
	 */
	if (conflict)
	{
		TimestampTz local_ts;
		RepOriginId local_node_id;
		bool		apply_update;
		bool		log_update;
		HeapTuple	user_tuple = NULL;
		BdrApplyConflict *apply_conflict = NULL;	/* Mute compiler */
		BdrConflictResolution resolution;

		get_local_tuple_origin(TTS_TUP(oldslot), &local_ts, &local_node_id);

		/*
		 * Use conflict triggers and/or last-update-wins to decide which tuple
		 * to retain.
		 */
		check_apply_update(BdrConflictType_InsertInsert,
						   local_node_id, local_ts, rel,
						   TTS_TUP(oldslot), TTS_TUP(newslot), &user_tuple,
						   &apply_update, &log_update, &resolution);

		/*
		 * Log conflict to server log.
		 */
		if (log_update)
		{
			apply_conflict = bdr_make_apply_conflict(
													 BdrConflictType_InsertInsert, resolution,
													 replication_origin_xid, rel, oldslot, local_node_id,
													 newslot, local_ts, NULL /* no error */ );

			bdr_conflict_log_serverlog(apply_conflict);

			bdr_count_insert_conflict();
		}

		/*
		 * Finally, apply the update.
		 */
		if (apply_update)
		{
#if PG_VERSION_NUM >= 120000
			bool		update_indexes;
#endif

			/*
			 * User specified conflict handler provided a new tuple; form it
			 * to a bdr tuple.
			 */
			if (user_tuple)
			{
#ifdef VERBOSE_INSERT
				log_tuple("USER tuple:%s", RelationGetDescr(rel->rel), user_tuple);
#endif
				ExecStoreHeapTuple(user_tuple, newslot, true);
			}

#if PG_VERSION_NUM >= 120000
			simple_table_tuple_update(rel->rel,
									  &(oldslot->tts_tid),
									  newslot,
									  estate->es_snapshot,
									  &update_indexes);

			if (update_indexes)
#else
			simple_heap_update(rel->rel,
							   &(TTS_TUP(oldslot)->t_self),
							   TTS_TUP(newslot));
#endif
			/* races will be resolved by abort/retry */
			UserTableUpdateOpenIndexes(estate, newslot, relinfo, false);

			bdr_count_insert();
		}

		/* Log conflict to table */
		if (log_update)
		{
			bdr_conflict_log_table(apply_conflict);
			bdr_conflict_logging_cleanup();
		}
	}
	else
	{
#if PG_VERSION_NUM >= 120000
		simple_table_tuple_insert(relinfo->ri_RelationDesc, newslot);
#else
		simple_heap_insert(rel->rel, TTS_TUP(newslot));
#endif
		UserTableUpdateOpenIndexes(estate, newslot, relinfo, false);
		bdr_count_insert();
	}

	PopActiveSnapshot();

	ExecCloseIndices(relinfo);

	check_bdr_wakeups(rel);

	/*
	 * execute DDL if insertion was into the ddl command queue and if ddl
	 * replication is wanted
	 */
	if (!prev_bdr_skip_ddl_replication && (RelationGetRelid(rel->rel) == QueuedDDLCommandsRelid ||
										   RelationGetRelid(rel->rel) == QueuedDropsRelid))
	{
		HeapTuple	ht;
		LockRelId	lockid = rel->rel->rd_lockInfo.lockRelId;
		TransactionId oldxid = GetTopTransactionId();
		Oid			relid = RelationGetRelid(rel->rel);
		Relation	qrel;

		/* there never should be conflicts on these */
		Assert(!conflict);

		cbarg.is_ddl_or_drop = true;

		/*
		 * Release transaction bound resources for CONCURRENTLY support.
		 */
		MemoryContextSwitchTo(MessageContext);
		ht = heap_copytuple(TTS_TUP(newslot));

		LockRelationIdForSession(&lockid, RowExclusiveLock);
		bdr_table_close(rel, NoLock);

		ExecResetTupleTable(estate->es_tupleTable, true);
		FreeExecutorState(estate);

		if (relid == QueuedDDLCommandsRelid)
		{
			cbarg.action_name = "QUEUED_DDL";
			process_queued_ddl_command(ht, started_tx);
		}
		if (relid == QueuedDropsRelid)
		{
			cbarg.action_name = "QUEUED_DROP";
			process_queued_drop(ht);
		}

		qrel = table_open(QueuedDDLCommandsRelid, RowExclusiveLock);

		UnlockRelationIdForSession(&lockid, RowExclusiveLock);

		table_close(qrel, NoLock);

		if (oldxid != GetTopTransactionId())
		{
			CommitTransactionCommand();
			MemoryContextSwitchTo(MessageContext);
			started_transaction = false;
		}
	}
	else
	{
		bdr_table_close(rel, NoLock);
		ExecResetTupleTable(estate->es_tupleTable, true);
		FreeExecutorState(estate);
	}

	CommandCounterIncrement();

	if (error_context_stack == &errcallback)
		error_context_stack = errcallback.previous;
}

static void
process_remote_update(StringInfo s)
{
	char		action;
	EState	   *estate;
	TupleTableSlot *newslot;
	TupleTableSlot *oldslot;
	bool		pkey_sent;
	bool		found_tuple;
	BDRTupleData old_tuple;
	BDRTupleData new_tuple;
	Oid			idxoid;
	BDRRelation *rel;
	Relation	idxrel;
	ScanKeyData skey[INDEX_MAX_KEYS];
	HeapTuple	user_tuple = NULL,
				remote_tuple = NULL;
	ErrorContextCallback errcallback;
	struct ActionErrCallbackArg cbarg;
	ResultRelInfo *relinfo = makeNode(ResultRelInfo);

	xact_action_counter++;
	memset(&cbarg, 0, sizeof(struct ActionErrCallbackArg));
	cbarg.action_name = "UPDATE";
	errcallback.callback = action_error_callback;
	errcallback.arg = &cbarg;
	errcallback.previous = error_context_stack;
	error_context_stack = &errcallback;

	bdr_performing_work();

	rel = read_rel(s, RowExclusiveLock, &cbarg);

	if (bdr_trace_replay)
	{
		StringInfoData si;

		initStringInfo(&si);
		format_action_description(&si, "UPDATE",
								  cbarg.remote_nspname, cbarg.remote_relname, false);
		cbarg.suppress_output = true;
		elog(LOG, "TRACE: %s", si.data);
		cbarg.suppress_output = false;
		pfree(si.data);
	}

	action = pq_getmsgbyte(s);

	/* old key present, identifying key changed */
	if (action != 'K' && action != 'N')
		elog(ERROR, "expected action 'N' or 'K', got %c", action);

	estate = bdr_create_rel_estate(rel->rel, relinfo);
#if PG_VERSION_NUM >= 120000
	oldslot = table_slot_create(rel->rel, &estate->es_tupleTable);
#else
	oldslot = ExecInitExtraTupleSlotBdr(estate, NULL);
	ExecSetSlotDescriptor(oldslot, RelationGetDescr(rel->rel));
#endif

	newslot = ExecInitExtraTupleSlotBdr(estate, NULL);
	ExecSetSlotDescriptor(newslot, RelationGetDescr(rel->rel));

	if (action == 'K')
	{
		pkey_sent = true;
		read_tuple_parts(s, rel, &old_tuple);
		action = pq_getmsgbyte(s);
	}
	else
		pkey_sent = false;

	/* check for new  tuple */
	if (action != 'N')
		elog(ERROR, "expected action 'N', got %c", action);

	if (rel->rel->rd_rel->relkind != RELKIND_RELATION)
		elog(ERROR, "unexpected relkind '%c' rel \"%s\"",
			 rel->rel->rd_rel->relkind, RelationGetRelationName(rel->rel));

	/* read new tuple */
	read_tuple_parts(s, rel, &new_tuple);

	/* lookup index to build scankey */
	if (rel->rel->rd_indexvalid == 0)
		RelationGetIndexList(rel->rel);
	idxoid = rel->rel->rd_replidindex;
	if (!OidIsValid(idxoid))
		elog(ERROR, "could not find primary key for table with oid %u",
			 RelationGetRelid(rel->rel));

	/* open index, so we can build scan key for row */
	idxrel = index_open(idxoid, RowExclusiveLock);

	Assert(idxrel->rd_index->indisunique);

	/* Use columns from the new tuple if the key didn't change. */
	build_index_scan_key(skey, rel->rel, idxrel,
						 pkey_sent ? &old_tuple : &new_tuple);

	PushActiveSnapshot(GetTransactionSnapshot());

	/* look for tuple identified by the (old) primary key */
	found_tuple = find_pkey_tuple(skey, rel, idxrel, oldslot, true,
								  pkey_sent ? LockTupleExclusive : LockTupleNoKeyExclusive);

	if (found_tuple)
	{
		TimestampTz local_ts;
		RepOriginId local_node_id;
		bool		apply_update;
		bool		log_update;
		BdrApplyConflict *apply_conflict = NULL;	/* Mute compiler */
		BdrConflictResolution resolution;

		remote_tuple = heap_modify_tuple(TTS_TUP(oldslot),
										 RelationGetDescr(rel->rel),
										 new_tuple.values,
										 new_tuple.isnull,
										 new_tuple.changed);

		ExecStoreHeapTuple(remote_tuple, newslot, true);

#ifdef VERBOSE_UPDATE
		{
			StringInfoData o;

			initStringInfo(&o);
			tuple_to_stringinfo(&o, RelationGetDescr(rel->rel), TTS_TUP(oldslot));
			appendStringInfo(&o, " to");
			tuple_to_stringinfo(&o, RelationGetDescr(rel->rel), remote_tuple);
			elog(DEBUG1, "UPDATE: %s", o.data);
			resetStringInfo(&o);
		}
#endif

		get_local_tuple_origin(TTS_TUP(oldslot), &local_ts, &local_node_id);

		/*
		 * Use conflict triggers and/or last-update-wins to decide which tuple
		 * to retain.
		 */
		check_apply_update(BdrConflictType_UpdateUpdate,
						   local_node_id, local_ts, rel,
						   TTS_TUP(oldslot), TTS_TUP(newslot),
						   &user_tuple, &apply_update,
						   &log_update, &resolution);

		/*
		 * Log conflict to server log
		 */
		if (log_update)
		{
			apply_conflict = bdr_make_apply_conflict(
													 BdrConflictType_UpdateUpdate, resolution,
													 replication_origin_xid, rel, oldslot, local_node_id,
													 newslot, local_ts, NULL /* no error */ );

			bdr_conflict_log_serverlog(apply_conflict);

			bdr_count_update_conflict();
		}

		if (apply_update)
		{
#if PG_VERSION_NUM >= 120000
			bool		update_indexes;
#endif

			/*
			 * User specified conflict handler provided a new tuple; form it
			 * to a bdr tuple.
			 */
			if (user_tuple)
			{
#ifdef VERBOSE_UPDATE
				log_tuple("USER tuple:%s", RelationGetDescr(rel->rel), user_tuple);
#endif
				ExecStoreHeapTuple(user_tuple, newslot, true);
			}

#if PG_VERSION_NUM >= 120000
			simple_table_tuple_update(rel->rel,
									  &(oldslot->tts_tid),
									  newslot,
									  estate->es_snapshot,
									  &update_indexes);
			if (update_indexes)
#else
			simple_heap_update(rel->rel, &(TTS_TUP(oldslot)->t_self), TTS_TUP(newslot));
#endif
			UserTableUpdateIndexes(estate, newslot, relinfo);
			bdr_count_update();
		}

		/* Log conflict to table */
		if (log_update)
		{
			bdr_conflict_log_table(apply_conflict);
			bdr_conflict_logging_cleanup();
		}
	}
	else
	{
		/*
		 * Update target is missing. We don't know if this is an
		 * update-vs-delete conflict or if the target tuple came from some 3rd
		 * node and hasn't yet been applied to the local node.
		 */

		bool		skip = false;
		BdrApplyConflict *apply_conflict;
		BdrConflictResolution resolution;

		remote_tuple = heap_form_tuple(RelationGetDescr(rel->rel),
									   new_tuple.values,
									   new_tuple.isnull);

		ExecStoreHeapTuple(remote_tuple, newslot, true);

		user_tuple = bdr_conflict_handlers_resolve(rel, NULL,
												   remote_tuple, "UPDATE",
												   BdrConflictType_UpdateDelete,
												   0, &skip);

		bdr_count_update_conflict();

		if (skip)
			resolution = BdrConflictResolution_ConflictTriggerSkipChange;
		else if (user_tuple)
			resolution = BdrConflictResolution_ConflictTriggerReturnedTuple;
		else
			resolution = BdrConflictResolution_DefaultSkipChange;


		apply_conflict = bdr_make_apply_conflict(
												 BdrConflictType_UpdateDelete, resolution, replication_origin_xid,
												 rel, NULL, InvalidRepOriginId, newslot, 0, NULL /* no error */ );

		bdr_conflict_log_serverlog(apply_conflict);

		/*
		 * If the user specified conflict handler returned tuple, we insert it
		 * since there is nothing to update.
		 */
		if (resolution == BdrConflictResolution_ConflictTriggerReturnedTuple)
		{
#ifdef VERBOSE_UPDATE
			log_tuple("USER tuple:%s", RelationGetDescr(rel->rel), user_tuple);
#endif
			ExecStoreHeapTuple(user_tuple, newslot, true);

#if PG_VERSION_NUM >= 120000
			simple_table_tuple_insert(relinfo->ri_RelationDesc, newslot);
#else
			simple_heap_insert(rel->rel, TTS_TUP(newslot));
#endif
			UserTableUpdateOpenIndexes(estate, newslot, relinfo, false);
		}

		bdr_conflict_log_table(apply_conflict);
		bdr_conflict_logging_cleanup();
	}

	PopActiveSnapshot();

	check_bdr_wakeups(rel);

	/* release locks upon commit */
	index_close(idxrel, NoLock);
	bdr_table_close(rel, NoLock);

	ExecResetTupleTable(estate->es_tupleTable, true);
	FreeExecutorState(estate);

	CommandCounterIncrement();

	if (error_context_stack == &errcallback)
		error_context_stack = errcallback.previous;
}

static void
process_remote_delete(StringInfo s)
{
	char		action;
	EState	   *estate;
	BDRTupleData oldtup;
	TupleTableSlot *oldslot;
	Oid			idxoid;
	BDRRelation *rel;
	Relation	idxrel;
	ScanKeyData skey[INDEX_MAX_KEYS];
	bool		found_old;
	ErrorContextCallback errcallback;
	struct ActionErrCallbackArg cbarg;
	ResultRelInfo *relinfo = makeNode(ResultRelInfo);

	Assert(bdr_apply_worker != NULL);

	xact_action_counter++;
	memset(&cbarg, 0, sizeof(struct ActionErrCallbackArg));
	cbarg.action_name = "DELETE";
	errcallback.callback = action_error_callback;
	errcallback.arg = &cbarg;
	errcallback.previous = error_context_stack;
	error_context_stack = &errcallback;

	bdr_performing_work();

	rel = read_rel(s, RowExclusiveLock, &cbarg);

	if (bdr_trace_replay)
	{
		StringInfoData si;

		initStringInfo(&si);
		format_action_description(&si, "DELETE",
								  cbarg.remote_nspname, cbarg.remote_relname, false);
		cbarg.suppress_output = true;
		elog(LOG, "TRACE: %s", si.data);
		cbarg.suppress_output = false;
		pfree(si.data);
	}

	action = pq_getmsgbyte(s);

	if (action != 'K' && action != 'E')
		elog(ERROR, "expected action K or E got %c", action);

	if (action == 'E')
	{
		elog(WARNING, "got delete without pkey");
		bdr_table_close(rel, NoLock);
		return;
	}

	estate = bdr_create_rel_estate(rel->rel, relinfo);
#if PG_VERSION_NUM >= 120000
	oldslot = table_slot_create(rel->rel, &estate->es_tupleTable);
#else
	oldslot = ExecInitExtraTupleSlotBdr(estate, NULL);
	ExecSetSlotDescriptor(oldslot, RelationGetDescr(rel->rel));
#endif

	read_tuple_parts(s, rel, &oldtup);

	/* lookup index to build scankey */
	if (rel->rel->rd_indexvalid == 0)
		RelationGetIndexList(rel->rel);
	idxoid = rel->rel->rd_replidindex;
	if (!OidIsValid(idxoid))
		elog(ERROR, "could not find primary key for table with oid %u",
			 RelationGetRelid(rel->rel));

	/* Now open the primary key index */
	idxrel = index_open(idxoid, RowExclusiveLock);

	if (rel->rel->rd_rel->relkind != RELKIND_RELATION)
		elog(ERROR, "unexpected relkind '%c' rel \"%s\"",
			 rel->rel->rd_rel->relkind, RelationGetRelationName(rel->rel));

#ifdef VERBOSE_DELETE
	{
		HeapTuple	tup;

		tup = heap_form_tuple(RelationGetDescr(rel->rel),
							  oldtup.values, oldtup.isnull);
		ExecStoreHeapTuple(tup, oldslot, true);
	}
	log_tuple("DELETE old-key:%s", RelationGetDescr(rel->rel), TTS_TUP(oldslot));
#endif

	PushActiveSnapshot(GetTransactionSnapshot());

	build_index_scan_key(skey, rel->rel, idxrel, &oldtup);

	/* try to find tuple via a (candidate|primary) key */
	found_old = find_pkey_tuple(skey, rel, idxrel, oldslot, true, LockTupleExclusive);

	if (found_old)
	{
		simple_heap_delete(rel->rel, &(TTS_TUP(oldslot)->t_self));
		bdr_count_delete();
	}
	else
	{
		/*
		 * The tuple to be deleted could not be found. This could be a replay
		 * order issue, where node A created a tuple then node B deleted it,
		 * and we've received the changes from node B before the changes from
		 * node A.
		 *
		 * Or it could be a conflict where two nodes deleted the same tuple.
		 * We can't tell the difference. We also can't afford to ignore the
		 * delete in case it is just an ordering issue.
		 *
		 * (This can also arise with an UPDATE that changes the PRIMARY KEY,
		 * as that's effectively a DELETE + INSERT).
		 */

		bool		skip = false;
		HeapTuple	remote_tuple,
					user_tuple = NULL;
		BdrApplyConflict *apply_conflict;

		bdr_count_delete_conflict();

		/* Since the local tuple is missing, fill slot from the received data. */
		remote_tuple = heap_form_tuple(RelationGetDescr(rel->rel),
									   oldtup.values, oldtup.isnull);

#if PG_VERSION_NUM >= 120000

		/*
		 * For heap AM, table_slot_create returns a slot of type
		 * TTSOpsBufferHeapTuple, see heapam_slot_callbacks(). When tuple to
		 * be deleted is not found, the target slot (oldslot) type will remain
		 * TTSOpsBufferHeapTuple. Since the target slot is not guaranteed to
		 * be TTSOpsHeapTuple type slot here, hence, we use
		 * ExecForceStoreHeapTuple().
		 */
		ExecForceStoreHeapTuple(remote_tuple, oldslot, false);
#else
		ExecStoreHeapTuple(remote_tuple, oldslot, true);
#endif

		/*
		 * Trigger user specified conflict handler so that application may
		 * react accordingly. Unlike other conflict types we don't allow the
		 * trigger to return new tuple here (it's DELETE vs DELETE after all).
		 */
		user_tuple = bdr_conflict_handlers_resolve(rel, NULL,
												   remote_tuple, "DELETE",
												   BdrConflictType_DeleteDelete,
												   0, &skip);

		/* DELETE vs DELETE can't return new tuple. */
		if (user_tuple)
			ereport(ERROR,
					(errmsg("DELETE vs DELETE handler returned a row which isn't allowed")));

		apply_conflict = bdr_make_apply_conflict(
												 BdrConflictType_DeleteDelete,
												 skip ? BdrConflictResolution_ConflictTriggerSkipChange :
												 BdrConflictResolution_DefaultSkipChange,
												 replication_origin_xid, rel, NULL, InvalidRepOriginId,
												 oldslot, 0, NULL /* no error */ );

		bdr_conflict_log_serverlog(apply_conflict);
		bdr_conflict_log_table(apply_conflict);
		bdr_conflict_logging_cleanup();
	}

	PopActiveSnapshot();

	check_bdr_wakeups(rel);

	index_close(idxrel, NoLock);
	bdr_table_close(rel, NoLock);

	ExecResetTupleTable(estate->es_tupleTable, true);
	FreeExecutorState(estate);

	CommandCounterIncrement();

	if (error_context_stack == &errcallback)
		error_context_stack = errcallback.previous;
}

/*
 * Get commit timestamp and origin of the tuple
 */
static void
get_local_tuple_origin(HeapTuple tuple, TimestampTz *commit_ts, RepOriginId *node_id)
{
	TransactionId xmin;
	RepOriginId node_id_raw;

	/* refetch tuple, check for old commit ts & origin */
	xmin = HeapTupleHeaderGetXmin(tuple->t_data);

	TransactionIdGetCommitTsData(xmin, commit_ts, &node_id_raw);
	*node_id = node_id_raw;
}

/*
 * Last update wins conflict handling.
 */
static void
bdr_conflict_last_update_wins(RepOriginId local_node_id,
							  RepOriginId remote_node_id,
							  TimestampTz local_ts,
							  TimestampTz remote_ts,
							  bool *perform_update, bool *log_update,
							  BdrConflictResolution * resolution)
{
	int			cmp;

	cmp = timestamptz_cmp_internal(remote_ts, local_ts);
	if (cmp > 0)
	{
		/* The most recent update is the remote one; apply it */
		*perform_update = true;
		*resolution = BdrConflictResolution_LastUpdateWins_KeepRemote;
		return;
	}
	else if (cmp < 0)
	{
		/* The most recent update is the local one; retain it */
		*log_update = true;
		*perform_update = false;
		*resolution = BdrConflictResolution_LastUpdateWins_KeepLocal;
		return;
	}
	else
	{
		BDRNodeId	local,
					remote;

		/*
		 * Timestamps are equal. Use sysid + timeline id to decide which tuple
		 * to retain.
		 */
		bdr_fetch_sysid_via_node_id(local_node_id, &local);
		bdr_fetch_sysid_via_node_id(remote_node_id, &remote);

		/*
		 * As the timestamps were equal, we have to break the tie in a
		 * consistent manner that'll match across all nodes.
		 *
		 * Use the ordering of the node's unique identifier, the tuple of
		 * (sysid, timelineid, dboid).
		 */
		if (local.sysid < remote.sysid)
			*perform_update = true;
		else if (local.sysid > remote.sysid)
			*perform_update = false;
		else if (local.timeline < remote.timeline)
			*perform_update = true;
		else if (local.timeline > remote.timeline)
			*perform_update = false;
		else if (local.dboid < remote.dboid)
			*perform_update = true;
		else if (local.dboid > remote.dboid)
			*perform_update = false;
		else
			/* shouldn't happen */
			elog(ERROR, "unsuccessful node comparison");

		/*
		 * We don't log whether we used timestamp, sysid or timeline id to
		 * decide which tuple to retain. That'll be in the log record anyway,
		 * so we can reconstruct the decision from the log record later.
		 */
		if (*perform_update)
		{
			*resolution = BdrConflictResolution_LastUpdateWins_KeepRemote;
		}
		else
		{
			*resolution = BdrConflictResolution_LastUpdateWins_KeepLocal;
			*log_update = true;
		}
	}
}

/*
 * Check whether a remote insert or update conflicts with the local row
 * version.
 *
 * User-defined conflict triggers get invoked here.
 *
 * perform_update and log_update are set to true if the update should be
 * performed and logged, respectively
 *
 * resolution is set to indicate how the conflict was resolved if log_update
 * is true. Its value is undefined if log_update is false.
 */
static void
check_apply_update(BdrConflictType conflict_type,
				   RepOriginId local_node_id, TimestampTz local_ts,
				   BDRRelation * rel, HeapTuple local_tuple,
				   HeapTuple remote_tuple, HeapTuple *new_tuple,
				   bool *perform_update, bool *log_update,
				   BdrConflictResolution * resolution)
{
	int			microsecs;
	long		secs;

	bool		skip = false;

	/*
	 * ensure that new_tuple is initialized with NULL; there are cases where
	 * we wouldn't touch it otherwise.
	 */
	if (new_tuple)
		*new_tuple = NULL;

	*log_update = false;

	if (local_node_id == replorigin_session_origin)
	{
		/*
		 * If the row got updated twice within a single node, just apply the
		 * update with no conflict.  Don't warn/log either, regardless of the
		 * timing; that's just too common and valid since normal row level
		 * locking guarantees are met.
		 */
		*perform_update = true;
		return;
	}

	/*
	 * Decide whether to keep the remote or local tuple based on a conflict
	 * trigger (if defined) or last-update-wins.
	 *
	 * If the caller doesn't provide storage for the conflict handler to store
	 * a new tuple in, don't fire any conflict triggers.
	 */

	if (new_tuple)
	{
		/*
		 * -------------- Conflict trigger conflict handling - let the user
		 * decide whether to: - Ignore the remote update; - Supply a new tuple
		 * to replace the current tuple; or - Take no action and fall through
		 * to the next handling option --------------
		 */

		abs_timestamp_difference(replorigin_session_origin_timestamp, local_ts,
								 &secs, &microsecs);

		*new_tuple = bdr_conflict_handlers_resolve(rel, local_tuple, remote_tuple,
												   conflict_type == BdrConflictType_InsertInsert ?
												   "INSERT" : "UPDATE",
												   conflict_type,
												   labs(secs) * 1000000 + labs(microsecs),
												   &skip);

		if (skip)
		{
			*log_update = true;
			*perform_update = false;
			*resolution = BdrConflictResolution_ConflictTriggerSkipChange;
			return;
		}
		else if (*new_tuple)
		{
			/* Custom conflict handler returned tuple, log it. */
			*log_update = true;
			*perform_update = true;
			*resolution = BdrConflictResolution_ConflictTriggerReturnedTuple;
			return;
		}

		/*
		 * if user decided not to skip the conflict but didn't provide a
		 * resolving tuple we fall back to default handling
		 */
	}

	/* Use last update wins conflict handling. */
	bdr_conflict_last_update_wins(local_node_id,
								  replorigin_session_origin,
								  local_ts,
								  replorigin_session_origin_timestamp,
								  perform_update, log_update,
								  resolution);
}

static void
queued_command_error_callback(void *arg)
{
	errcontext("during DDL replay of ddl statement: %s", (char *) arg);
}

void
bdr_execute_ddl_command(char *cmdstr, char *perpetrator, char *search_path,
						bool tx_just_started)
{
	List	   *commands;
	ListCell   *command_i;
	bool		isTopLevel;
	MemoryContext oldcontext;
	ErrorContextCallback errcallback;
	int			guc_nestlevel;

	oldcontext = MemoryContextSwitchTo(MessageContext);

	errcallback.callback = queued_command_error_callback;
	errcallback.arg = cmdstr;
	errcallback.previous = error_context_stack;
	error_context_stack = &errcallback;

	guc_nestlevel = NewGUCNestLevel();

	/* Force everything in the query to be fully qualified. */
	(void) set_config_option("search_path", search_path,
							 PGC_USERSET, PGC_S_SESSION,
							 GUC_ACTION_SAVE, true, 0, false);

	commands = pg_parse_query(cmdstr);

	MemoryContextSwitchTo(oldcontext);

	/*
	 * Do a limited amount of safety checking against CONCURRENTLY commands
	 * executed in situations where they aren't allowed. The sender side
	 * should provide protection, but better be safe than sorry.
	 */
	isTopLevel = (list_length(commands) == 1) && tx_just_started;

	foreach(command_i, commands)
	{
		List	   *plantree_list;
		List	   *querytree_list;
		RawStmt    *command = (RawStmt *) lfirst(command_i);
		CommandTag	commandTag;
		Portal		portal;
		DestReceiver *receiver;

		/* temporarily push snapshot for parse analysis/planning */
		PushActiveSnapshot(GetTransactionSnapshot());

		oldcontext = MemoryContextSwitchTo(MessageContext);

		/*
		 * Set the current role to the user that executed the command on the
		 * origin server.
		 */
		SetConfigOption("role", perpetrator, PGC_INTERNAL, PGC_S_OVERRIDE);

		commandTag = CreateCommandTag(command->stmt);

		querytree_list = pg_analyze_and_rewrite(command, cmdstr, NULL, 0);

		plantree_list = pg_plan_queries(querytree_list, cmdstr, 0, NULL);

		PopActiveSnapshot();

		portal = CreatePortal("bdr", true, true);
		PortalDefineQuery(portal, NULL,
						  cmdstr, commandTag,
						  plantree_list, NULL);
		PortalStart(portal, NULL, 0, InvalidSnapshot);

		receiver = CreateDestReceiver(DestNone);

		(void) PortalRun(portal, FETCH_ALL,
						 isTopLevel, true,
						 receiver, receiver,
						 NULL);
		(*receiver->rDestroy) (receiver);

		PortalDrop(portal, false);

		CommandCounterIncrement();

		MemoryContextSwitchTo(oldcontext);
	}

	/*
	 * To protect against stack resets during CONCURRENTLY processing, only
	 * restore the errcontext if it's how we left it.
	 */
	if (error_context_stack == &errcallback)
		error_context_stack = errcallback.previous;

	/* Restore the old search_path */
	AtEOXact_GUC(false, guc_nestlevel);
}

static void
process_queued_ddl_command(HeapTuple cmdtup, bool tx_just_started)
{
	Relation	cmdsrel;
	Datum		datum;
	char	   *command_tag;
	char	   *cmdstr;
	bool		isnull;
	char	   *perpetrator;
	char	   *search_path;
	MemoryContext oldcontext;

	/* ----
	 * We can't use spi here, because it implicitly assumes a transaction
	 * context. As we want to be able to replicate CONCURRENTLY commands,
	 * that's not going to work...
	 * So instead do all the work manually, being careful about managing the
	 * lifecycle of objects.
	 * ----
	 */
	oldcontext = MemoryContextSwitchTo(MessageContext);

	cmdsrel = table_open(QueuedDDLCommandsRelid, NoLock);

	/* fetch the perpetrator user identifier */
	datum = heap_getattr(cmdtup, 3,
						 RelationGetDescr(cmdsrel),
						 &isnull);
	if (isnull)
		elog(ERROR, "null command perpetrator in command tuple in \"%s\"",
			 RelationGetRelationName(cmdsrel));
	perpetrator = TextDatumGetCString(datum);

	/* fetch the command tag */
	datum = heap_getattr(cmdtup, 4,
						 RelationGetDescr(cmdsrel),
						 &isnull);
	if (isnull)
		elog(ERROR, "null command tag in command tuple in \"%s\"",
			 RelationGetRelationName(cmdsrel));
	command_tag = TextDatumGetCString(datum);

	/* finally fetch and execute the command */
	datum = heap_getattr(cmdtup, 5,
						 RelationGetDescr(cmdsrel),
						 &isnull);
	if (isnull)
		elog(ERROR, "null command for \"%s\" command tuple", command_tag);

	cmdstr = TextDatumGetCString(datum);

	datum = heap_getattr(cmdtup, 6,
						 RelationGetDescr(cmdsrel),
						 &isnull);
	if (isnull)
	{
		/*
		 * Older BDR versions didn't have search_path and we can't UPDATE old
		 * rows, so there will be nulls. Those prior versions also forced
		 * search_path to '' so we can safely assume as much.
		 */
		search_path = "";
	}
	else
		search_path = TextDatumGetCString(datum);

	/* close relation, command execution might end/start xact */
	table_close(cmdsrel, NoLock);

	MemoryContextSwitchTo(oldcontext);

	if (bdr_trace_replay)
	{
		elog(LOG, "TRACE: QUEUED_DDL: [%s] with search_path [%s]",
			 cmdstr, search_path);
	}

	bdr_execute_ddl_command(cmdstr, perpetrator, search_path, tx_just_started);
}


/*
 * ugly hack: Copied struct from dependency.c - there doesn't seem to be a
 * supported way of iterating ObjectAddresses otherwise.
 */
struct ObjectAddresses
{
	ObjectAddress *refs;		/* => palloc'd array */
	void	   *extras;			/* => palloc'd array, or NULL if not used */
	int			numrefs;		/* current number of references */
	int			maxrefs;		/* current size of palloc'd array(s) */
};

static void
format_drop_objectlist(StringInfo si, ObjectAddresses *addrs)
{
	int			i;

	for (i = addrs->numrefs - 1; i >= 0; i--)
	{
		ObjectAddress *obj = addrs->refs + i;

		appendStringInfo(si, "\n  * %s", getObjectDescription(obj));
	}
}

static void
queued_drop_error_callback(void *arg)
{
	ObjectAddresses *addrs = (ObjectAddresses *) arg;
	StringInfoData si;

	initStringInfo(&si);

	format_drop_objectlist(&si, addrs);

	errcontext("during DDL replay object drop:%s", si.data);
	resetStringInfo(&si);
}

static TypeName *
oper_typeStringToTypeName(const char *str)
{
	if (pg_strcasecmp(str, "none") == 0)
		return NULL;
	else
		return typeStringToTypeName(str);
}

static HeapTuple
process_queued_drop(HeapTuple cmdtup)
{
	Relation	cmdsrel;
	HeapTuple	newtup;
	Datum		arrayDatum;
	ArrayType  *array;
	bool		null;
	Oid			elmtype;
	int16		elmlen;
	bool		elmbyval;
	char		elmalign;
	Oid			elmoutoid;
	bool		elmisvarlena;
	TupleDesc	elemdesc;
	Datum	   *values;
	int			nelems;
	int			i;
	ObjectAddresses *addresses;
	ErrorContextCallback errcallback;

	cmdsrel = table_open(QueuedDropsRelid, AccessShareLock);
	arrayDatum = heap_getattr(cmdtup, 3,
							  RelationGetDescr(cmdsrel),
							  &null);
	if (null)
	{
		elog(WARNING, "null dropped object array in command tuple in \"%s\"",
			 RelationGetRelationName(cmdsrel));
		return cmdtup;
	}
	array = DatumGetArrayTypeP(arrayDatum);
	elmtype = ARR_ELEMTYPE(array);

	get_typlenbyvalalign(elmtype, &elmlen, &elmbyval, &elmalign);
	deconstruct_array(array, elmtype,
					  elmlen, elmbyval, elmalign,
					  &values, NULL, &nelems);

	getTypeOutputInfo(elmtype, &elmoutoid, &elmisvarlena);
	elemdesc = TypeGetTupleDesc(elmtype, NIL);

	addresses = new_object_addresses();

	for (i = 0; i < nelems; i++)
	{
		HeapTupleHeader elemhdr;
		HeapTupleData tmptup;
		ObjectType	objtype;
		Datum		datum;
		bool		isnull;
		char	   *type;
		List	   *objnames;
		List	   *objargs = NIL;
		Relation	objrel;
		ObjectAddress addr;

		elemhdr = (HeapTupleHeader) DatumGetPointer(values[i]);
		tmptup.t_len = HeapTupleHeaderGetDatumLength(elemhdr);
		ItemPointerSetInvalid(&(tmptup.t_self));
		tmptup.t_tableOid = InvalidOid;
		tmptup.t_data = elemhdr;

		/* obtain the object type as a C-string ... */
		datum = heap_getattr(&tmptup, 1, elemdesc, &isnull);
		if (isnull)
		{
			elog(WARNING, "null type !?");
			continue;
		}
		type = TextDatumGetCString(datum);
		objtype = (ObjectType) read_objtype_from_string(type);

		/*
		 * ignore objects that don't unstringify properly; those are
		 * "internal" objects anyway.
		 */
		if (objtype == -1)
			continue;

		if (objtype == OBJECT_TYPE ||
			objtype == OBJECT_DOMAIN)
		{
			Datum	   *values;
			bool	   *nulls;
			int			nelems;
			char	   *typestring;
			TypeName   *typeName;

			datum = heap_getattr(&tmptup, 2, elemdesc, &isnull);
			if (isnull)
			{
				elog(WARNING, "null typename !?");
				continue;
			}

			deconstruct_array(DatumGetArrayTypeP(datum),
							  TEXTOID, -1, false, 'i',
							  &values, &nulls, &nelems);

			typestring = TextDatumGetCString(values[0]);
			typeName = typeStringToTypeName(typestring);
			objnames = typeName->names;
		}
		else if (objtype == OBJECT_FUNCTION ||
				 objtype == OBJECT_AGGREGATE ||
				 objtype == OBJECT_OPERATOR)
		{
			Datum	   *values;
			bool	   *nulls;
			int			nelems;
			int			i;
			char	   *typestring;

			/* objname */
			objnames = NIL;
			datum = heap_getattr(&tmptup, 2, elemdesc, &isnull);
			if (isnull)
			{
				elog(WARNING, "null objname !?");
				continue;
			}

			deconstruct_array(DatumGetArrayTypeP(datum),
							  TEXTOID, -1, false, 'i',
							  &values, &nulls, &nelems);
			for (i = 0; i < nelems; i++)
				objnames = lappend(objnames,
								   makeString(TextDatumGetCString(values[i])));

			/* objargs are type names */
			datum = heap_getattr(&tmptup, 3, elemdesc, &isnull);
			if (isnull)
			{
				if (objtype == OBJECT_OPERATOR)
				{
					elog(WARNING, "null typename !?");
					continue;
				}
			}
			else
			{
				deconstruct_array(DatumGetArrayTypeP(datum),
								  TEXTOID, -1, false, 'i',
								  &values, &nulls, &nelems);

				for (i = 0; i < nelems; i++)
				{
					typestring = TextDatumGetCString(values[i]);
					objargs = lappend(objargs, objtype == OBJECT_OPERATOR ?
									  oper_typeStringToTypeName(typestring) :
									  typeStringToTypeName(typestring));
				}
			}
		}
		else
		{
			Datum	   *values;
			bool	   *nulls;
			int			nelems;
			int			i;

			/* objname */
			objnames = NIL;
			datum = heap_getattr(&tmptup, 2, elemdesc, &isnull);
			if (isnull)
			{
				elog(WARNING, "null objname !?");
				continue;
			}

			deconstruct_array(DatumGetArrayTypeP(datum),
							  TEXTOID, -1, false, 'i',
							  &values, &nulls, &nelems);
			for (i = 0; i < nelems; i++)
				objnames = lappend(objnames,
								   makeString(TextDatumGetCString(values[i])));

			datum = heap_getattr(&tmptup, 3, elemdesc, &isnull);
			if (!isnull)
			{
				Datum	   *values;
				bool	   *nulls;
				int			nelems;
				int			i;

				deconstruct_array(DatumGetArrayTypeP(datum),
								  TEXTOID, -1, false, 'i',
								  &values, &nulls, &nelems);
				for (i = 0; i < nelems; i++)
					objargs = lappend(objargs,
									  makeString(TextDatumGetCString(values[i])));
			}
		}

		addr = get_object_address(objtype, (Node *) objnames, &objrel,
								  AccessExclusiveLock, false);

		/* unsupported object? */
		if (addr.classId == InvalidOid)
			continue;

		/*
		 * For certain objects, get_object_address returned us an open and
		 * locked relation.  Close it because we have no use for it; but
		 * keeping the lock seems easier than figure out lock level to
		 * release.
		 */
		if (objrel != NULL)
			relation_close(objrel, NoLock);

		add_exact_object_address(&addr, addresses);
	}

	if (bdr_trace_replay)
	{
		StringInfoData si;

		initStringInfo(&si);
		format_drop_objectlist(&si, addresses);
		elog(LOG, "TRACE: QUEUED_DROP: %s", si.data);
		pfree(si.data);
	}

	errcallback.callback = queued_drop_error_callback;
	errcallback.arg = addresses;
	errcallback.previous = error_context_stack;
	error_context_stack = &errcallback;

	performMultipleDeletions(addresses, DROP_RESTRICT, 0);

	/* protect against stack resets during CONCURRENTLY processing */
	if (error_context_stack == &errcallback)
		error_context_stack = errcallback.previous;

	newtup = cmdtup;

	table_close(cmdsrel, AccessShareLock);

	return newtup;
}

static bool
bdr_performing_work(void)
{
	if (started_transaction)
	{
		if (CurrentMemoryContext != MessageContext)
			MemoryContextSwitchTo(MessageContext);
		return false;
	}

	started_transaction = true;
	StartTransactionCommand();
	MemoryContextSwitchTo(MessageContext);
	return true;
}

static void
check_bdr_wakeups(BDRRelation * rel)
{
	Oid			schemaoid = RelationGetNamespace(rel->rel);
	Oid			reloid = RelationGetRelid(rel->rel);

	if (schemaoid != BdrSchemaOid)
		return;

	/* has the node/connection state been changed on another system? */
	if (reloid == BdrNodesRelid || reloid == BdrConnectionsRelid)
		bdr_connections_changed(NULL);
}

static void
read_tuple_parts_error_badatts(BDRRelation * rel, TupleDesc desc, int rnatts)
{
	ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
			 errmsg("remote tuple has different column count to local table and mismatched columns were not null/dropped"),
			 errdetail("Table \"%s\".\"%s\" (%u) has %u columns on local node " BDR_NODEID_FORMAT_WITHNAME " vs %u on remote node " BDR_NODEID_FORMAT_WITHNAME ".",
					   get_namespace_name(RelationGetNamespace(rel->rel)), RelationGetRelationName(rel->rel),
					   RelationGetRelid(rel->rel),
					   desc->natts,
					   BDR_LOCALID_FORMAT_WITHNAME_ARGS,
					   rnatts,
					   BDR_NODEID_FORMAT_WITHNAME_ARGS(origin)),
			 errhint("This error arises if the number of columns on two nodes differ and BDR cannot right-pad with nulls or ignore extra right-hand nulls. This is most commonly caused by unsafe use of the bdr.skip_ddl_replication and/or bdr.skip_ddl_locking settings.")));
}

static void
read_tuple_parts(StringInfo s, BDRRelation * rel, BDRTupleData * tup)
{
	TupleDesc	desc = RelationGetDescr(rel->rel);
	int			i;
	int			rnatts;
	char		action;

	action = pq_getmsgbyte(s);

	if (action != 'T')
		elog(ERROR, "expected TUPLE, got %c", action);

	memset(tup->isnull, 1, sizeof(tup->isnull));
	memset(tup->changed, 1, sizeof(tup->changed));

	rnatts = pq_getmsgint(s, 4);

	/* FIXME: unaligned data accesses */

	/* Consume remote data as long as there's a local column to put it in */
	for (i = 0; i < Min(desc->natts, rnatts); i++)
	{
		Form_pg_attribute att = &desc->attrs[i];
		char		kind;
		const char *data;
		int			len;

		kind = pq_getmsgbyte(s);

		switch (kind)
		{
			case 'n':			/* null */
				/* already marked as null */
				tup->values[i] = 0xdeadbeef;
				break;
			case 'u':			/* unchanged column */
				tup->isnull[i] = true;
				tup->changed[i] = false;
				tup->values[i] = 0xdeadbeef;	/* make bad usage more obvious */

				break;

			case 'b':			/* binary format */
				tup->isnull[i] = false;
				len = pq_getmsgint(s, 4);	/* read length */

				data = pq_getmsgbytes(s, len);

				/* and data */
				if (att->attbyval)
					tup->values[i] = fetch_att(data, true, len);
				else
					tup->values[i] = PointerGetDatum(data);
				break;
			case 's':			/* send/recv format */
				{
					Oid			typreceive;
					Oid			typioparam;
					StringInfoData buf;

					tup->isnull[i] = false;
					len = pq_getmsgint(s, 4);	/* read length */

					getTypeBinaryInputInfo(att->atttypid,
										   &typreceive, &typioparam);

					/* create StringInfo pointing into the bigger buffer */
					initStringInfo(&buf);
					/* and data */
					buf.data = (char *) pq_getmsgbytes(s, len);
					buf.len = len;
					tup->values[i] = OidReceiveFunctionCall(
															typreceive, &buf, typioparam, att->atttypmod);

					if (buf.len != buf.cursor)
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
								 errmsg("incorrect binary data format")));
					break;
				}
			case 't':			/* text format */
				{
					Oid			typinput;
					Oid			typioparam;

					tup->isnull[i] = false;
					len = pq_getmsgint(s, 4);	/* read length */

					getTypeInputInfo(att->atttypid, &typinput, &typioparam);
					/* and data */
					data = (char *) pq_getmsgbytes(s, len);
					tup->values[i] = OidInputFunctionCall(
														  typinput, (char *) data, typioparam, att->atttypmod);
				}
				break;
			default:
				elog(ERROR, "unknown column type '%c'", kind);
		}

		if (att->attisdropped && !tup->isnull[i])
			elog(ERROR, "data for dropped column");
	}

	/*
	 * Handle some remote rows that are narrower than the local table. Hotfix
	 * for RT#61148 and other schema de-sync issues; see RM#2814
	 */
	for (i = rnatts; i < desc->natts; i++)
	{
		Form_pg_attribute att = &desc->attrs[i];

		/*
		 * If the remote-missing attribute(s) are locally dropped or nullable,
		 * we can right-pad the local tuple with nulls. Attnos can never be
		 * *inserted* in a Pg table only *appended* so this is ... fairly
		 * safe. You could get in situations where the same attno meant
		 * something different on different nodes, but you can do the same
		 * when the attno count matches, so it's no worse doing the right
		 * padding than any other time.
		 */
		if (!att->attisdropped && att->attnotnull)
		{
			/*
			 * Theoretically we could fill DEFAULTs for NOT NULL local columns
			 * here too, at least with bdr.ignore_mismatched_rows enabled. See
			 * pglogical's `fill_missing_defaults` for how. But it's not meant
			 * to be necessary anyway, so we don't. The user can recover by
			 * ALTERing the table to be NULLable with a non-replicated DDL.
			 */
			elog(WARNING, "cannot right-pad mismatched attributes; attno %u is missing in remote data and local attribute is not-dropped and not nullable",
				 i);
			read_tuple_parts_error_badatts(rel, desc, rnatts);
		}

		/*
		 * Flag it null, and put some garbage in so we crash clearly if we try
		 * to use it anyway.
		 */
		tup->isnull[i] = true;
		tup->values[i] = 0xdeadbeef;
	}

	/*
	 * Discard trailing nulls on a too-wide remote tuple. See RM#2814.
	 *
	 * There are too many attributes in the incoming tuple. We can accept this
	 * if we can confirm that the incoming attributes are all null, since
	 * we're not discarding anything interesting then, and the outcome will be
	 * the same when the missing col is added locally since we disallow table
	 * rewrites like ALTER TABLE ... ADD COLUMN ... DEFAULT .
	 *
	 * Note that there's no storage for this attribute in 'tup' since it's
	 * only as wide as the local table. As far as the caller is concerned the
	 * extra values were never there.
	 */
	for (i = desc->natts; i < rnatts; i++)
	{
		char		kind;

		kind = pq_getmsgbyte(s);
		if (kind != 'n' && bdr_discard_mismatched_row_attributes)
		{
			/*
			 * User has overridden behaviour and forced discarding of row
			 * data. This can be useful to recover from broken replication,
			 * but it's a bad idea since it results in divergence on these
			 * values.
			 *
			 * LOG is stronger than WARNING for server log and this is
			 * important to record. Log flooding could be an issue, but this
			 * shouldn't be used anyway, and we really want to be able to
			 * diagnose it.
			 */
			elog(LOG, "WARNING: discarding non-null remote value for attribute %u on relation \"%s\".\"%s\" (%u) due to bdr.discard_mismatched_row_attributes setting. Remote natts = %u, local natts = %u",
				 i, get_namespace_name(RelationGetNamespace(rel->rel)),
				 RelationGetRelationName(rel->rel), RelationGetRelid(rel->rel),
				 rnatts, desc->natts);
		}
		else if (kind != 'n')
		{
			elog(WARNING, "cannot right-pad mismatched attributes; attno %u is missing in local table and remote row has non-null, non-dropped value for this attribute",
				 i);
			read_tuple_parts_error_badatts(rel, desc, rnatts);
		}
	}

	/* Don't test pq_getmsgend, there might be another message chunk */
}

static BDRRelation *
read_rel(StringInfo s, LOCKMODE mode, struct ActionErrCallbackArg *cbarg)
{
	int			relnamelen;
	int			nspnamelen;
	RangeVar   *rv;
	Oid			relid;

	rv = makeNode(RangeVar);

	nspnamelen = pq_getmsgint(s, 2);
	rv->schemaname = (char *) pq_getmsgbytes(s, nspnamelen);
	cbarg->remote_nspname = rv->schemaname;

	relnamelen = pq_getmsgint(s, 2);
	rv->relname = (char *) pq_getmsgbytes(s, relnamelen);
	cbarg->remote_relname = rv->relname;

	relid = RangeVarGetRelidExtended(rv, mode, 0, NULL, NULL);

	return bdr_table_open(relid, NoLock);
}

/*
 * Read a remote action type and process the action record.
 *
 * May set got_SIGTERM to stop processing before next record.
 */
static void
bdr_process_remote_action(StringInfo s)
{
	char		action = pq_getmsgbyte(s);

	Assert(CurrentMemoryContext == MessageContext);
	switch (action)
	{
			/* BEGIN */
		case 'B':
			process_remote_begin(s);
			break;
			/* COMMIT */
		case 'C':
			process_remote_commit(s);
			break;
			/* INSERT */
		case 'I':
			process_remote_insert(s);
			break;
			/* UPDATE */
		case 'U':
			process_remote_update(s);
			break;
			/* DELETE */
		case 'D':
			process_remote_delete(s);
			break;
		case 'M':
			bdr_process_remote_message(s);
			break;
		default:
			elog(ERROR, "unknown action of type %c", action);
	}
	Assert(CurrentMemoryContext == MessageContext);

	if (action == 'C')
	{
		/*
		 * We clobber MessageContext on commit. It doesn't matter much when we
		 * do it so long as we do so periodically, to prevent the context from
		 * growing too much.
		 */
		MemoryContextReset(MessageContext);
	}
}


/*
 * Figure out which write/flush positions to report to the walsender process.
 *
 * We can't simply report back the last LSN the walsender sent us because the
 * local transaction might not yet be flushed to disk locally. Instead we
 * build a list that associates local with remote LSNs for every commit. When
 * reporting back the flush position to the sender we iterate that list and
 * check which entries on it are already locally flushed. Those we can report
 * as having been flushed.
 *
 * Returns true if there's no outstanding transactions that need to be
 * flushed.
 */
static bool
bdr_get_flush_position(XLogRecPtr *write, XLogRecPtr *flush)
{
	dlist_mutable_iter iter;
	XLogRecPtr	local_flush = GetFlushRecPtr();

	*write = InvalidXLogRecPtr;
	*flush = InvalidXLogRecPtr;

	dlist_foreach_modify(iter, &bdr_lsn_association)
	{
		BdrFlushPosition *pos =
			dlist_container(BdrFlushPosition, node, iter.cur);

		*write = pos->remote_end;

		if (pos->local_end <= local_flush)
		{
			*flush = pos->remote_end;
			dlist_delete(iter.cur);
			pfree(pos);
		}
		else
		{
			/*
			 * Don't want to uselessly iterate over the rest of the list which
			 * could potentially be long. Instead get the last element and
			 * grab the write position from there.
			 */
			pos = dlist_tail_element(BdrFlushPosition, node,
									 &bdr_lsn_association);
			*write = pos->remote_end;
			return false;
		}
	}

	return dlist_is_empty(&bdr_lsn_association);
}

/*
 * Send a Standby Status Update message to server.
 *
 * 'recvpos' is the latest LSN we've received data to, force is set if we need
 * to send a response to avoid timeouts.
 */
static bool
bdr_send_feedback(PGconn *conn, XLogRecPtr recvpos, int64 now, bool force)
{
	char		replybuf[1 + 8 + 8 + 8 + 8 + 1];
	int			len = 0;

	static XLogRecPtr last_recvpos = InvalidXLogRecPtr;
	static XLogRecPtr last_writepos = InvalidXLogRecPtr;
	static XLogRecPtr last_flushpos = InvalidXLogRecPtr;

	XLogRecPtr	writepos;
	XLogRecPtr	flushpos;

	/* It's legal to not pass a recvpos */
	if (recvpos < last_recvpos)
		recvpos = last_recvpos;

	if (bdr_get_flush_position(&writepos, &flushpos))
	{
		/*
		 * No outstanding transactions to flush, we can report the latest
		 * received position. This is important for synchronous replication.
		 */
		flushpos = writepos = recvpos;
	}

	if (writepos < last_writepos)
		writepos = last_writepos;

	if (flushpos < last_flushpos)
		flushpos = last_flushpos;

	/* if we've already reported everything we're good */
	if (!force &&
		writepos == last_writepos &&
		flushpos == last_flushpos)
		return true;

	replybuf[len] = 'r';
	len += 1;
	bdr_sendint64(recvpos, &replybuf[len]); /* write */
	len += 8;
	bdr_sendint64(flushpos, &replybuf[len]);	/* flush */
	len += 8;
	bdr_sendint64(writepos, &replybuf[len]);	/* apply */
	len += 8;
	bdr_sendint64(now, &replybuf[len]); /* sendTime */
	len += 8;
	replybuf[len] = false;		/* replyRequested */
	len += 1;

	elog(DEBUG2, "sending feedback (force %d) to recv %X/%X, write %X/%X, flush %X/%X",
		 force, LSN_FORMAT_ARGS(recvpos), LSN_FORMAT_ARGS(writepos),
		 LSN_FORMAT_ARGS(flushpos));

	if (PQputCopyData(conn, replybuf, len) <= 0 || PQflush(conn))
	{
		ereport(ERROR,
				(errcode(ERRCODE_CONNECTION_FAILURE),
				 errmsg("could not send feedback packet: %s",
						PQerrorMessage(conn))));
		return false;
	}

	if (recvpos > last_recvpos)
		last_recvpos = recvpos;
	if (writepos > last_writepos)
		last_writepos = writepos;
	if (flushpos > last_flushpos)
		last_flushpos = flushpos;

	return true;
}

/*
 * abs_timestamp_difference -- convert the difference between two timestamps
 *		into integer seconds and microseconds
 *
 * The result is always the absolute (positive difference), so the order
 * of input is not important.
 *
 * If either input is not finite, we return zeroes.
 *
 * BDR version of TimestampDifference() with absolute difference.
 */
static void
abs_timestamp_difference(TimestampTz start_time, TimestampTz stop_time,
						 long *secs, int *microsecs)
{
	if (TIMESTAMP_NOT_FINITE(start_time) || TIMESTAMP_NOT_FINITE(stop_time))
	{
		*secs = 0;
		*microsecs = 0;
	}
	else
	{
		TimestampTz diff = labs(stop_time - start_time);

		*secs = (long) (diff / USECS_PER_SEC);
		*microsecs = (int) (diff % USECS_PER_SEC);
	}
}

#if defined(VERBOSE_INSERT) || defined(VERBOSE_UPDATE) || defined(VERBOSE_DELETE)
static void
log_tuple(const char *format, TupleDesc desc, HeapTuple tup)
{
	StringInfoData o;

	initStringInfo(&o);
	tuple_to_stringinfo(&o, desc, tup);
	elog(DEBUG1, format, o.data);
	resetStringInfo(&o);

}
#endif

/*
 * When the apply worker's latch is set it reloads its configuration
 * from the database, checking for new replication sets, connection
 * strings, etc.
 *
 * At this time many changes simply force the apply worker to exit and restart,
 * since we'd have to reconnect to apply most kinds of change anyway.
 */
static void
bdr_apply_reload_config()
{
	BdrConnectionConfig *new_apply_config;

	/* Fetch our config from the DB */
	new_apply_config = bdr_get_connection_config(
												 &bdr_apply_worker->remote_node,
												 false);

	Assert(bdr_nodeid_eq(&new_apply_config->remote_node, &bdr_apply_worker->remote_node));

	/*
	 * Got default remote connection info, read also local defaults. Otherwise
	 * we would be using replication sets and apply delay from the remote node
	 * instead of the local one.
	 *
	 * Note: this is slightly hacky and we should probably use the bdr_nodes
	 * for this instead.
	 */
	if (!new_apply_config->origin_is_my_id)
	{
		BdrConnectionConfig *cfg = bdr_get_my_connection_config(false);

		new_apply_config->apply_delay = cfg->apply_delay;
		pfree(new_apply_config->replication_sets);
		new_apply_config->replication_sets = pstrdup(cfg->replication_sets);
		bdr_free_connection_config(cfg);
	}

	if (bdr_apply_config == NULL)
	{
		/* First run, carry on loading */
		bdr_apply_config = new_apply_config;
	}
	else
	{
		/* If the DSN or replication sets changed we must restart */
		if (strcmp(bdr_apply_config->dsn, new_apply_config->dsn) != 0)
		{
			elog(LOG, "apply worker exiting to apply new DSN configuration");
			proc_exit(1);
		}

		if (strcmp(bdr_apply_config->replication_sets, new_apply_config->replication_sets) != 0)
		{
			elog(LOG, "apply worker exiting to apply new replication set configuration");
			proc_exit(1);
		}

	}
}

/*
 * The actual main loop of a BDR apply worker.
 */
static void
bdr_apply_work(PGconn *streamConn)
{
	int			fd;
	char	   *copybuf = NULL;
	XLogRecPtr	last_received = InvalidXLogRecPtr;

	fd = PQsocket(streamConn);

	MessageContext = AllocSetContextCreate(TopMemoryContext,
										   "MessageContext",
										   ALLOCSET_DEFAULT_MINSIZE,
										   ALLOCSET_DEFAULT_INITSIZE,
										   ALLOCSET_DEFAULT_MAXSIZE);

	/* mark as idle, before starting to loop */
	pgstat_report_activity(STATE_IDLE, NULL);

	while (!got_SIGTERM)
	{
		int			rc;
		int			r;

		/*
		 * Background workers mustn't call usleep() or any direct equivalent:
		 * instead, they may wait on their process latch, which sleeps as
		 * necessary, but is awakened if postmaster dies.  That way the
		 * background process goes away immediately in an emergency.
		 */
		rc = WaitLatchOrSocket(&MyProc->procLatch,
							   WL_SOCKET_READABLE | WL_LATCH_SET |
							   WL_TIMEOUT | WL_POSTMASTER_DEATH,
							   fd, 1000L, PG_WAIT_EXTENSION);

		ResetLatch(&MyProc->procLatch);

		MemoryContextSwitchTo(MessageContext);

		/* emergency bailout if postmaster has died */
		if (rc & WL_POSTMASTER_DEATH)
			proc_exit(1);

		CHECK_FOR_INTERRUPTS();

		if (PQstatus(streamConn) == CONNECTION_BAD)
		{
			bdr_count_disconnect();
			elog(ERROR, "connection to other side has died");
		}

		if (got_SIGHUP)
		{
			got_SIGHUP = false;
			ProcessConfigFile(PGC_SIGHUP);
			/* set log_min_messages */
			SetConfigOption("log_min_messages", bdr_error_severity(bdr_log_min_messages),
							PGC_POSTMASTER, PGC_S_OVERRIDE);
		}

		if (rc & WL_LATCH_SET)
		{
			/*
			 * Apply worker latch was set. This could be an attempt to resume
			 * apply that wasn't paused in the first place, or could be a
			 * request to reload our config. It's safe to reload, so just do
			 * so.
			 */
			bdr_apply_reload_config();
		}

		if (rc & WL_SOCKET_READABLE)
			PQconsumeInput(streamConn);

		for (;;)
		{
			if (got_SIGTERM)
				break;

			if (copybuf != NULL)
			{
				PQfreemem(copybuf);
				copybuf = NULL;
			}

			r = PQgetCopyData(streamConn, &copybuf, 1);

			if (r == -1)
				elog(ERROR, "data stream ended");
			else if (r == -2)
				elog(ERROR, "could not read COPY data: %s",
					 PQerrorMessage(streamConn));
			else if (r < 0)
				elog(ERROR, "invalid COPY status %d", r);
			else if (r == 0)
				break;			/* need to wait for new data */
			else
			{
				int			c;
				StringInfoData s;

				MemoryContextSwitchTo(MessageContext);

				initStringInfo(&s);
				s.data = copybuf;
				s.len = r;
				s.maxlen = -1;

				c = pq_getmsgbyte(&s);

				if (c == 'w')
				{
					XLogRecPtr	start_lsn;
					XLogRecPtr	end_lsn;

					start_lsn = pq_getmsgint64(&s);
					end_lsn = pq_getmsgint64(&s);
					pq_getmsgint64(&s); /* sendTime */

					if (last_received < start_lsn)
						last_received = start_lsn;

					if (last_received < end_lsn)
						last_received = end_lsn;

					bdr_process_remote_action(&s);
				}
				else if (c == 'k')
				{
					XLogRecPtr	endpos;
					bool		reply_requested;

					/*
					 * Walsender keepalive/feedback message. We'll get these
					 * both while idle and while we're replaying a transaction
					 * data stream, to ensure the walsender knows we're alive
					 * and kicking.  There's no need for us to schedule our
					 * own keepalives, the walsender will send one with
					 * reply-requested when needed, even in the middle of
					 * sending a single big row or Datum.
					 *
					 * Keepalives are also sent by the upstream when the
					 * server is making changes that don't result in logical
					 * decoding activity, so that it can advance the
					 * catalog_xmin and restart_lsn of idle slots. It's also
					 * important for synchronous replication so the upstream
					 * can confirm commits since our reply tells the upstream
					 * we've flushed anything we needed to.
					 *
					 * See:  WalSndKeepaliveIfNecessary(...),
					 * WalSndWriteData(...) in walsender.c .
					 */

					endpos = pq_getmsgint64(&s);
					 /* timestamp = */ pq_getmsgint64(&s);
					reply_requested = pq_getmsgbyte(&s);

					bdr_send_feedback(streamConn, endpos,
									  GetCurrentTimestamp(),
									  reply_requested);
				}
				/* other message types are purposefully ignored */
			}

		}

		/* confirm all writes at once */
		bdr_send_feedback(streamConn, last_received,
						  GetCurrentTimestamp(), false);

		/*
		 * If the user has paused replication with bdr_apply_pause(), we wait
		 * on our procLatch until pg_bdr_apply_resume() unsets the flag in
		 * shmem. We don't pause until the end of the current transaction, to
		 * avoid sleeping with locks held.
		 *
		 * Sleep for 5 minutes before re-checking. We shouldn't really need to
		 * since we set the proc latch on resume, but it doesn't hurt to be
		 * careful.
		 */
		while (BdrWorkerCtl->pause_apply && !IsTransactionState())
		{
			ResetLatch(&MyProc->procLatch);
			rc = WaitLatch(&MyProc->procLatch,
						   WL_TIMEOUT | WL_LATCH_SET | WL_POSTMASTER_DEATH,
						   300000L, PG_WAIT_EXTENSION);

			if (rc & WL_POSTMASTER_DEATH)
				proc_exit(1);

			if (rc & WL_LATCH_SET)
			{
				/*
				 * Setting the apply worker latch causes a recheck of pause
				 * state, but it could also be an attempt to reload the
				 * worker's configuration. Check whether anything has changed.
				 */
				bdr_apply_reload_config();
			}

			CHECK_FOR_INTERRUPTS();
		}

		MemoryContextResetAndDeleteChildren(MessageContext);
	}
}


/*
 * Entry point for a BDR apply worker.
 *
 * Responsible for establishing a replication connection, creating slots
 * and starting the reply loop.
 */
void
bdr_apply_main(Datum main_arg)
{
	PGconn	   *streamConn;
	PGresult   *res;
	StringInfoData query;
	char	   *sqlstate;
	RepOriginId replication_identifier;
	XLogRecPtr	start_from;
	NameData	slot_name;
	char		status;

	bdr_bgworker_init(DatumGetInt32(main_arg), BDR_WORKER_APPLY);

	bdr_apply_worker = &bdr_worker_slot->data.apply;

	initStringInfo(&query);

	Assert(MyDatabaseId == bdr_apply_worker->dboid);

	/*
	 * Store our proclatch in our shmem segment.
	 *
	 * This must be protected by a lock so that nobody tries to set our latch
	 * field while we're writing to it.
	 */
	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);
	if (BdrWorkerCtl->worker_management_paused)
	{
		LWLockRelease(BdrWorkerCtl->lock);
		elog(ERROR, "BDR worker management is currently paused, apply worker exiting, retry later");
	}
	bdr_apply_worker->proclatch = &MyProc->procLatch;
	LWLockRelease(BdrWorkerCtl->lock);

	/*
	 * Check if we decided to kill off this connection
	 */
	StartTransactionCommand();
	SPI_connect();
	PushActiveSnapshot(GetTransactionSnapshot());
	status = bdr_nodes_get_local_status(&bdr_apply_worker->remote_node, false);
	SPI_finish();
	PopActiveSnapshot();
	CommitTransactionCommand();
	if (status == BDR_NODE_STATUS_KILLED)
	{
		elog(LOG, "unregistering apply worker due to remote node " BDR_NODEID_FORMAT " detach",
			 BDR_NODEID_FORMAT_ARGS(bdr_apply_worker->remote_node));
		bdr_worker_shmem_free(bdr_worker_slot, NULL);
		bdr_worker_slot = NULL;
		proc_exit(0);			/* unregister */
	}

	/* Read our connection configuration from the database */
	bdr_apply_reload_config();

	/*
	 * Set our local application_name for our SPI connections. We want to see
	 * the remote name in pg_stat_activity here.
	 */
	resetStringInfo(&query);
	appendStringInfo(&query, "%s:%s", bdr_apply_config->node_name, "apply");
	if (bdr_apply_worker->forward_changesets)
		appendStringInfoString(&query, " catchup");

	if (bdr_apply_worker->replay_stop_lsn != InvalidXLogRecPtr)
		appendStringInfo(&query, " up to %X/%X",
						 LSN_FORMAT_ARGS(bdr_apply_worker->replay_stop_lsn));

	SetConfigOption("application_name", query.data, PGC_USERSET, PGC_S_SESSION);

	CurrentResourceOwner = ResourceOwnerCreate(NULL, "bdr apply top-level resource owner");
	bdr_saved_resowner = CurrentResourceOwner;

	/*
	 * Form an application_name string to send to the remote end. From the
	 * remote's perspective this is a send connection.
	 */
	resetStringInfo(&query);
	appendStringInfoString(&query, "send");

	if (bdr_apply_worker->forward_changesets)
		appendStringInfoString(&query, " catchup");

	if (bdr_apply_worker->replay_stop_lsn != InvalidXLogRecPtr)
		appendStringInfo(&query, " up to %X/%X",
						 LSN_FORMAT_ARGS(bdr_apply_worker->replay_stop_lsn));

	/* Make the replication connection to the remote end */
	streamConn = bdr_establish_connection_and_slot(bdr_apply_config->dsn,
												   query.data, &slot_name, &origin, &replication_identifier, NULL);


	/* initialize stat subsystem, our id won't change further */
	bdr_count_set_current_node(replication_identifier);

	/*
	 * tell replication_identifier.c about our identifier so it can cache the
	 * search in shared memory.
	 */
	replorigin_session_setup(replication_identifier);

	/*
	 * Check whether we already replayed something so we don't replay it
	 * multiple times.
	 *
	 * The replication origin will contain the end of the last flushed commit
	 * record + 1, which is the startpoint we want for the (inclusive)
	 * argument to START_REPLICATION.
	 */
	start_from = replorigin_session_get_progress(false);

	elog(INFO, "starting up replication from %u at %X/%X (inclusive)",
		 replication_identifier, LSN_FORMAT_ARGS(start_from));

	resetStringInfo(&query);
	appendStringInfo(&query, "START_REPLICATION SLOT \"%s\" LOGICAL %X/%X ("
					 "pg_version '%u', pg_catversion '%u', bdr_version '%u',"
					 "bdr_variant '%s', min_bdr_version '%u', sizeof_int '%zu',"
					 "sizeof_long '%zu', sizeof_datum '%zu', maxalign '%d',"
					 "float4_byval '%d', float8_byval '%d', integer_datetimes '%d',"
					 "bigendian '%d', db_encoding '%s'",
					 NameStr(slot_name), LSN_FORMAT_ARGS(start_from),
					 PG_VERSION_NUM, CATALOG_VERSION_NO, BDR_VERSION_NUM,
					 BDR_VARIANT, BDR_MIN_REMOTE_VERSION_NUM, sizeof(int),
					 sizeof(long), sizeof(Datum), MAXIMUM_ALIGNOF,
					 bdr_get_float4byval(), bdr_get_float8byval(),
					 bdr_get_integer_timestamps(), bdr_get_bigendian(),
					 GetDatabaseEncodingName());

	if (bdr_apply_config->replication_sets != NULL &&
		bdr_apply_config->replication_sets[0] != 0)
		appendStringInfo(&query, ", replication_sets '%s'",
						 bdr_apply_config->replication_sets);

	if (bdr_apply_worker->forward_changesets)
		appendStringInfo(&query, ", forward_changesets 't'");

	appendStringInfoChar(&query, ')');

	elog(DEBUG3, "sending replication command: %s", query.data);

	res = PQexec(streamConn, query.data);

	sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE);

	if (PQresultStatus(res) != PGRES_COPY_BOTH)
	{
		elog(FATAL, "could not send replication command \"%s\": %s\n, sqlstate: %s",
			 query.data, PQresultErrorMessage(res), sqlstate);
	}
	PQclear(res);

	replorigin_session_origin = replication_identifier;

	bdr_conflict_logging_startup();

	PG_TRY();
	{
		bdr_apply_work(streamConn);
	}
	PG_CATCH();
	{
		if (IsTransactionState())
			bdr_count_rollback();
		PG_RE_THROW();
	}
	PG_END_TRY();

	/*
	 * never exit gracefully (as that'd unregister the worker) unless
	 * explicitly asked to do so.
	 */
	proc_exit(1);
}

bool
IsBdrApplyWorker(void)
{
	return bdr_apply_worker != NULL;
}
