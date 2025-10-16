/* -------------------------------------------------------------------------
 *
 * pgactive_executor.c
 *      Relation and index access and maintenance routines required by pgactive
 *
 * pgactive does a lot of direct access to indexes and relations, some of which
 * isn't handled by simple calls into the backend. Most of it lives here.
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *      pgactive_executor.c
 *
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include "pgactive.h"

#include "access/heapam.h"
#include "access/xact.h"

#include "catalog/indexing.h"
#include "catalog/pg_namespace.h"

#include "executor/executor.h"

#include "miscadmin.h"

#include "nodes/makefuncs.h"

#include "parser/parse_relation.h"
#include "parser/parsetree.h"

#include "replication/origin.h"

#include "storage/bufmgr.h"
#include "storage/lmgr.h"

#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"

static void pgactiveExecutorStart(QueryDesc *queryDesc, int eflags);

CommandTag	CreateWritableStmtTag(PlannedStmt *plannedstmt);

static ExecutorStart_hook_type PrevExecutorStart_hook = NULL;

static bool pgactive_always_allow_writes = false;

PGDLLEXPORT Datum pgactive_set_node_read_only(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pgactive_set_node_read_only);

EState *
pgactive_create_rel_estate(Relation rel, ResultRelInfo *resultRelInfo)
{
	EState	   *estate;

	estate = CreateExecutorState();

	resultRelInfo->ri_RangeTableIndex = 1;	/* dummy */
	resultRelInfo->ri_RelationDesc = rel;
	resultRelInfo->ri_TrigInstrument = NULL;
#if PG_VERSION_NUM < 140000
	estate->es_result_relations = resultRelInfo;
	estate->es_num_result_relations = 1;
	estate->es_result_relation_info = resultRelInfo;
#endif
	return estate;
}

void
UserTableUpdateIndexes(EState *estate, TupleTableSlot *slot, ResultRelInfo *relinfo)
{
	/* HOT update does not require index inserts */
	if (HeapTupleIsHeapOnly(TTS_TUP(slot)))
		return;

	ExecOpenIndices(relinfo, false);
	UserTableUpdateOpenIndexes(estate, slot, relinfo, false);
	ExecCloseIndices(relinfo);
}

void
UserTableUpdateOpenIndexes(EState *estate, TupleTableSlot *slot,
						   ResultRelInfo *relinfo, bool update)
{
	List	   *recheckIndexes = NIL;

	/* HOT update does not require index inserts */
	if (HeapTupleIsHeapOnly(TTS_TUP(slot)))
		return;

	if (relinfo->ri_NumIndices > 0)
	{
		recheckIndexes = ExecInsertIndexTuples(
#if PG_VERSION_NUM >= 140000
											   relinfo,
#endif
											   slot,
#if PG_VERSION_NUM < 120000
											   &slot->tts_tuple->t_self,
#endif
											   estate
#if PG_VERSION_NUM >= 140000
											   ,update
#endif
											   ,false
											   ,NULL
											   ,NIL
#if PG_VERSION_NUM >= 160000
											   ,false);
#else
			);
#endif
		if (recheckIndexes != NIL)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("pgactive doesn't support index rechecks")));
	}

	/* FIXME: recheck the indexes */
	list_free(recheckIndexes);
}

void
build_index_scan_keys(ResultRelInfo *relinfo, ScanKey *scan_keys, pgactiveTupleData * tup)
{
	int			i;

	/* build scankeys for each index */
	for (i = 0; i < relinfo->ri_NumIndices; i++)
	{
		IndexInfo  *ii = relinfo->ri_IndexRelationInfo[i];

		/*
		 * Only unique indexes are of interest here, and we can't deal with
		 * expression indexes so far. FIXME: predicates should be handled
		 * better.
		 */
		if (!ii->ii_Unique || ii->ii_Expressions != NIL)
		{
			scan_keys[i] = NULL;
			continue;
		}

		scan_keys[i] = palloc(ii->ii_NumIndexAttrs * sizeof(ScanKeyData));

		/*
		 * Only return index if we could build a key without NULLs.
		 */
		if (build_index_scan_key(scan_keys[i],
								 relinfo->ri_RelationDesc,
								 relinfo->ri_IndexRelationDescs[i],
								 tup))
		{
			pfree(scan_keys[i]);
			scan_keys[i] = NULL;
			continue;
		}
	}
}

/*
 * Setup a ScanKey for a search in the relation 'rel' for a tuple 'key' that
 * is setup to match 'rel' (*NOT* idxrel!).
 *
 * Returns whether any column contains NULLs.
 */
bool
build_index_scan_key(ScanKey skey, Relation rel, Relation idxrel, pgactiveTupleData * tup)
{
	int			attoff;
	Datum		indclassDatum;
	Datum		indkeyDatum;
	bool		isnull;
	oidvector  *opclass;
	int2vector *indkey;
	bool		hasnulls = false;

	indclassDatum = SysCacheGetAttr(INDEXRELID, idxrel->rd_indextuple,
									Anum_pg_index_indclass, &isnull);
	Assert(!isnull);
	opclass = (oidvector *) DatumGetPointer(indclassDatum);

	indkeyDatum = SysCacheGetAttr(INDEXRELID, idxrel->rd_indextuple,
								  Anum_pg_index_indkey, &isnull);
	Assert(!isnull);
	indkey = (int2vector *) DatumGetPointer(indkeyDatum);


	for (attoff = 0; attoff < RelationGetNumberOfAttributes(idxrel); attoff++)
	{
		Oid			operator;
		Oid			opfamily;
		RegProcedure regop;
		int			pkattno = attoff + 1;
		int			mainattno = indkey->values[attoff];
		Oid			atttype = attnumTypeId(rel, mainattno);
		Oid			optype = get_opclass_input_type(opclass->values[attoff]);

		opfamily = get_opclass_family(opclass->values[attoff]);

		operator = get_opfamily_member(opfamily, optype,
									   optype,
									   BTEqualStrategyNumber);

		if (!OidIsValid(operator))
			elog(ERROR,
				 "could not lookup equality operator for type %u, optype %u in opfamily %u",
				 atttype, optype, opfamily);

		regop = get_opcode(operator);

		/* FIXME: convert type? */
		ScanKeyInit(&skey[attoff],
					pkattno,
					BTEqualStrategyNumber,
					regop,
					tup->values[mainattno - 1]);

		if (tup->isnull[mainattno - 1])
		{
			hasnulls = true;
			skey[attoff].sk_flags |= SK_ISNULL;
		}
	}
	return hasnulls;
}

/*
 * Search the index 'idxrel' for a tuple identified by 'skey' in 'rel'.
 *
 * If a matching tuple is found setup 'tid' to point to it and return true,
 * false is returned otherwise.
 *
 * Populates 'slot' with a materialized copy of the found tuple in the memory
 * context of the passed slot.
 */
bool
find_pkey_tuple(ScanKey skey, pgactiveRelation * rel, Relation idxrel,
				TupleTableSlot *slot, bool lock, LockTupleMode mode)
{
#if PG_VERSION_NUM < 120000
	HeapTuple	scantuple;
#endif
	bool		found;
	IndexScanDesc scan;
	SnapshotData snap;
	TransactionId xwait;

	InitDirtySnapshot(snap);

retry:
	found = false;
	scan = index_beginscan(rel->rel, idxrel,
						   &snap,
#if PG_VERSION_NUM >= 180000
						   NULL,
#endif
						   RelationGetNumberOfAttributes(idxrel),
						   0);
	index_rescan(scan, skey, RelationGetNumberOfAttributes(idxrel), NULL, 0);
#if PG_VERSION_NUM >= 120000
	if (index_getnext_slot(scan, ForwardScanDirection, slot))
#else
	if ((scantuple = index_getnext(scan, ForwardScanDirection)) != NULL)
#endif
	{
		found = true;

		/*
		 * Store a copied physical tuple that doesn't reference shmem or hold
		 * any buffer pin, so it can live past the index scan. Any old tuple
		 * from a prior loop is cleared first.
		 */
		/* FIXME: Improve TupleSlot to not require copying the whole tuple */
#if PG_VERSION_NUM < 120000
		ExecStoreHeapTuple(scantuple, slot, false);
#endif
		ExecMaterializeSlot(slot);

		xwait = TransactionIdIsValid(snap.xmin) ? snap.xmin : snap.xmax;

		if (TransactionIdIsValid(xwait))
		{
			XactLockTableWait(xwait, NULL, NULL, XLTW_None);
			index_endscan(scan);
			goto retry;
		}
	}

	if (lock && found)
	{
#if PG_VERSION_NUM >= 120000
		TM_FailureData tmfd;
		TM_Result	res;
#else
		Buffer		buf;
		HeapUpdateFailureData hufd;
		HTSU_Result res;
		HeapTupleData locktup;

		ItemPointerCopy(&(TTS_TUP(slot)->t_self), &locktup.t_self);
#endif

		PushActiveSnapshot(GetLatestSnapshot());

#if PG_VERSION_NUM >= 120000
		res = table_tuple_lock(rel->rel, &(slot->tts_tid), GetLatestSnapshot(),
							   slot,
							   GetCurrentCommandId(false),
							   mode,
							   LockWaitBlock,
							   0 /* don't follow updates */ ,
							   &tmfd);
#else
		res = heap_lock_tuple(rel->rel, &locktup, GetCurrentCommandId(false), mode,
							  false /* wait */ ,
							  false /* don't follow updates */ ,
							  &buf, &hufd);
		/* the tuple slot already has the buffer pinned */
		ReleaseBuffer(buf);
#endif

		PopActiveSnapshot();

		switch (res)
		{
#if PG_VERSION_NUM >= 120000
			case TM_Ok:
#else
			case HeapTupleMayBeUpdated:
#endif
				break;
#if PG_VERSION_NUM >= 120000
			case TM_Updated:
#else
			case HeapTupleUpdated:
#endif
				/* XXX: Improve handling here */
				ereport(LOG,
						(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
						 errmsg("concurrent update, retrying")));
				index_endscan(scan);
				goto retry;
			default:
				elog(ERROR, "unexpected HTSU_Result after locking: %u", res);
				break;
		}
	}

	index_endscan(scan);

	return found;
}

void
pgactive_set_node_read_only_guts(char *node_name, bool read_only, bool force)
{
	HeapTuple	tuple = NULL;
	Relation	rel;
	RangeVar   *rv;
	SnapshotData SnapshotDirty;
	SysScanDesc scan;
	ScanKeyData key;
	pgactiveNodeStatus status;

	Assert(IsTransactionState());

	PushActiveSnapshot(GetTransactionSnapshot());

	/*
	 * We don't allow the user to clear read-only status while the local node
	 * is initing.
	 */
	status = pgactive_local_node_status();
	if ((status != pgactive_NODE_STATUS_READY && status != pgactive_NODE_STATUS_KILLED) && !force)
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("local node is still starting up, cannot change read-only status")));
	}

	InitDirtySnapshot(SnapshotDirty);

	rv = makeRangeVar("pgactive", "pgactive_nodes", -1);
	rel = table_openrv(rv, RowExclusiveLock);

	ScanKeyInit(&key,
				get_attnum(rel->rd_id, "node_name"),
				BTEqualStrategyNumber, F_TEXTEQ,
				PointerGetDatum(cstring_to_text(node_name)));

	scan = systable_beginscan(rel, InvalidOid,
							  true,
							  &SnapshotDirty,
							  1, &key);

	tuple = systable_getnext(scan);

	if (HeapTupleIsValid(tuple))
	{
		HeapTuple	newtuple;
		Datum	   *values;
		bool	   *nulls;
		TupleDesc	tupDesc;
		AttrNumber	attnum = get_attnum(rel->rd_id, "node_read_only");

		tupDesc = RelationGetDescr(rel);

		values = (Datum *) palloc(tupDesc->natts * sizeof(Datum));
		nulls = (bool *) palloc(tupDesc->natts * sizeof(bool));

		heap_deform_tuple(tuple, tupDesc, values, nulls);

		values[attnum - 1] = BoolGetDatum(read_only);

		newtuple = heap_form_tuple(RelationGetDescr(rel),
								   values, nulls);
		/* simple_heap_update(rel, &tuple->t_self, newtuple); */
		CatalogTupleUpdate(rel, &tuple->t_self, newtuple);
	}
	else
		elog(ERROR, "node %s not found.", node_name);

	systable_endscan(scan);

	PopActiveSnapshot();

	CommandCounterIncrement();

	/* now release lock again,  */
	table_close(rel, RowExclusiveLock);

	pgactive_connections_changed(NULL);
}

/*
 * Set node_read_only field in pgactive_nodes entry for given node.
 *
 * This has to be C function to avoid being subject to the executor read-only
 * filtering.
 */
Datum
pgactive_set_node_read_only(PG_FUNCTION_ARGS)
{
	char	   *node_name = text_to_cstring(PG_GETARG_TEXT_PP(0));
	bool		read_only = PG_GETARG_BOOL(1);

	pgactive_set_node_read_only_guts(node_name, read_only, false);

	PG_RETURN_VOID();
}


void
pgactive_executor_always_allow_writes(bool always_allow)
{
	Assert(IsUnderPostmaster);
	pgactive_always_allow_writes = always_allow;
}

CommandTag
CreateWritableStmtTag(PlannedStmt *plannedstmt)
{
	if (plannedstmt->commandType == CMD_SELECT)
#if PG_VERSION_NUM < 130000
		return "DML";			/* SELECT INTO/WCTE */
#else
		return CMDTAG_SELECT_INTO;
#endif

	return CreateCommandTag((Node *) plannedstmt);
}

/*
 * The pgactive ExecutorStart_hook that does DDL lock checks and forbids
 * writing into tables without replica identity index.
 *
 * Runs in all backends and workers.
 */
static void
pgactiveExecutorStart(QueryDesc *queryDesc, int eflags)
{
	bool		performs_writes = false;
	bool		read_only_node;
	ListCell   *l;
	List	   *rangeTable;
	PlannedStmt *plannedstmt = queryDesc->plannedstmt;
	Oid			idxoid;

	if (pgactive_always_allow_writes)
		goto done;

	/* don't perform filtering while replaying */
	if (replorigin_session_origin != InvalidRepOriginId)
		goto done;

	/* identify whether this is a modifying statement */
	if (plannedstmt != NULL &&
		(plannedstmt->hasModifyingCTE ||
		 plannedstmt->rowMarks != NIL))
		performs_writes = true;
	else if (queryDesc->operation != CMD_SELECT)
		performs_writes = true;

	if (!performs_writes)
		goto done;

	if (!pgactive_is_pgactive_activated_db(MyDatabaseId))
		goto done;

	/*
	 * replace pgactive_permit_unsafe_commands by
	 * pgactive_skip_ddl_replication for now
	 */
	read_only_node = pgactive_local_node_read_only() && !pgactive_skip_ddl_replication;

	/* check for concurrent global DDL locks */
	pgactive_locks_check_dml();

	/*
	 * Are we in pgactive.replicate_ddl_command? If so, it's not safe to do
	 * DML, since this will basically do statement based replication that'll
	 * mess up volatile functions etc. If we skipped replicating it as rows
	 * and just replicated statements, we'd get wrong sequences and so on.
	 *
	 * We can't just ignore the DML and leave it in the command string, then
	 * replicate its effects with rows, either. Otherwise DDL like this would
	 * break:
	 *
	 * pgactive.replicate_ddl_command($$ ALTER TABLE foo ADD COLUMN bar ...;
	 * UPDATE foo SET bar = baz WHERE ...; ALTER TABLE foo DROP COLUMN baz;
	 * $$);
	 *
	 * ... because we'd apply the DROP COLUMN before we replicated the rows,
	 * since we execute a DDL string as a single operation. Then row apply
	 * would fail because the incoming rows would have data for dropped column
	 * 'baz'.
	 */
	if (in_pgactive_replicate_ddl_command && !pgactive_in_extension)
	{
		if (queryDesc->operation == CMD_INSERT
			|| queryDesc->operation == CMD_UPDATE
			|| queryDesc->operation == CMD_DELETE)
		{
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("row-data-modifying statements INSERT, UPDATE and DELETE are not permitted inside pgactive.replicate_ddl_command"),
					 errhint("Split up scripts, putting DDL in pgactive.replicate_ddl_command and DML as normal statements.")));
		}
	}

	/* plain INSERTs are ok beyond this point if node is not read-only */
	if (queryDesc->operation == CMD_INSERT &&
		!plannedstmt->hasModifyingCTE && !read_only_node)
		goto done;

	/* Fail if query tries to UPDATE or DELETE any of tables without PK */
	rangeTable = plannedstmt->rtable;
	foreach(l, plannedstmt->resultRelations)
	{
		Index		rtei = lfirst_int(l);
		RangeTblEntry *rte = rt_fetch(rtei, rangeTable);
		Relation	rel;

		rel = RelationIdGetRelation(rte->relid);

		/* Skip UNLOGGED and TEMP tables */
		if (!RelationNeedsWAL(rel))
		{
			RelationClose(rel);
			continue;
		}

		/*
		 * Since changes to pg_catalog aren't replicated directly there's no
		 * strong need to suppress direct UPDATEs on them. The usual rule of
		 * "it's dumb to modify the catalogs directly if you don't know what
		 * you're doing" applies.
		 */
		if (RelationGetNamespace(rel) == PG_CATALOG_NAMESPACE)
		{
			RelationClose(rel);
			continue;
		}

		if (read_only_node)
			ereport(ERROR,
					(errcode(ERRCODE_READ_ONLY_SQL_TRANSACTION),
					 errmsg("%s may only affect UNLOGGED or TEMPORARY tables " \
							"on read-only pgactive node; %s is a regular table",
							GetCommandTagName(CreateWritableStmtTag(plannedstmt)),
							RelationGetRelationName(rel))));

		idxoid = RelationGetReplicaIndex(rel);
		if (!OidIsValid(idxoid))
#if PG_VERSION_NUM >= 180000
			idxoid = RelationGetPrimaryKeyIndex(rel, false);
#else
			idxoid = RelationGetPrimaryKeyIndex(rel);
#endif
		if (OidIsValid(idxoid))
		{
			RelationClose(rel);
			continue;
		}

		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot run UPDATE or DELETE on table %s because it does not have a PRIMARY KEY",
						RelationGetRelationName(rel)),
				 errhint("Add a PRIMARY KEY to the table.")));

		RelationClose(rel);
	}

done:
	if (PrevExecutorStart_hook)
		(*PrevExecutorStart_hook) (queryDesc, eflags);
	else
		standard_ExecutorStart(queryDesc, eflags);
}


void
pgactive_executor_init(void)
{
	PrevExecutorStart_hook = ExecutorStart_hook;
	ExecutorStart_hook = pgactiveExecutorStart;
}
