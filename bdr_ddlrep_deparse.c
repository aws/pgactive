/* -------------------------------------------------------------------------
 *
 * bdr_ddlrep_deparse.c
 *      DDL deparse based DDL repliation
 *
 * DDL deparse based DDL replication is used on 9.4bdr.
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *      bdr_executor.c
 *
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include "bdr.h"

#include "access/heapam.h"
#include "access/skey.h"
#include "access/xact.h"
#include "access/xlog_fn.h"

#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_trigger.h"

#include "commands/event_trigger.h"
#include "commands/trigger.h"

#include "executor/executor.h"
#include "executor/spi.h"
#include "executor/tuptable.h"

#include "funcapi.h"
#include "miscadmin.h"

#include "nodes/execnodes.h"
#include "nodes/makefuncs.h"
#include "nodes/parsenodes.h"

#include "parser/parse_func.h"
#include "parser/parse_relation.h"
#include "parser/parsetree.h"

#include "replication/origin.h"

#include "storage/bufmgr.h"
#include "storage/lmgr.h"

#include "tcop/utility.h"

#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "utils/tqual.h"

PGDLLEXPORT Datum bdr_queue_ddl_commands(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(bdr_queue_ddl_commands);

PGDLLEXPORT Datum bdr_queue_dropped_objects(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(bdr_queue_dropped_objects);

/*
 * bdr_queue_ddl_commands
 * 		ddl_command_end event triggger handler for BDR
 *
 * This function queues all commands reported in a replicated table, so that
 * they can be replayed by remote BDR nodes.
 */
Datum
bdr_queue_ddl_commands(PG_FUNCTION_ARGS)
{
	char	   *skip_ddl;
	int			res;
	int			i;
	MemoryContext tupcxt;
	uint32		nprocessed;
	SPITupleTable *tuptable;

	if (!CALLED_AS_EVENT_TRIGGER(fcinfo))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("bdr_queue_ddl_commands")));

	/*
	 * If the trigger comes from DDL executed by bdr_replicate_ddl_command,
	 * don't queue it as it would insert duplicate commands into the queue.
	 */
	if (in_bdr_replicate_ddl_command)
		PG_RETURN_VOID();		/* XXX return type? */

	/*
	 * If we're currently replaying something from a remote node, don't queue
	 * the commands; that would cause recursion.
	 */
	if (replorigin_session_origin != InvalidRepOriginId)
		PG_RETURN_VOID();		/* XXX return type? */

	/*
	 * Similarly, if configured to skip queueing DDL, don't queue.  This is
	 * mostly used when pg_restore brings a remote node state, so all objects
	 * will be copied over in the dump anyway.
	 */
	skip_ddl = GetConfigOptionByName("bdr.skip_ddl_replication", NULL, false);
	if (strcmp(skip_ddl, "on") == 0)
		PG_RETURN_VOID();

	/*
	 * Connect to SPI early, so that all memory allocated in this routine is
	 * released when we disconnect.  Also create a memory context that's reset
	 * for each iteration, to avoid per-tuple leakage.  Normally there would
	 * be very few tuples, but it's possible to create larger commands and
	 * it's pretty easy to fix the issue anyway.
	 */
	SPI_connect();
	PushActiveSnapshot(GetTransactionSnapshot());

	tupcxt = AllocSetContextCreate(CurrentMemoryContext,
								   "per-tuple DDL queue cxt",
								   ALLOCSET_DEFAULT_MINSIZE,
								   ALLOCSET_DEFAULT_INITSIZE,
								   ALLOCSET_DEFAULT_MAXSIZE);

	res = SPI_execute("SELECT "
					  "   command_tag, object_type, schema, identity, "
					  "   in_extension, "
					  "   pg_event_trigger_expand_command(command) AS command "
					  "FROM "
					  "   pg_catalog.pg_event_trigger_get_creation_commands()",
					  false, 0);
	if (res != SPI_OK_SELECT)
		elog(ERROR, "SPI query failed: %d", res);

	/*
	 * For each command row reported by the event trigger facility, insert
	 * zero or one row in the BDR queued commands table specifying how to
	 * replicate it.
	 */
	MemoryContextSwitchTo(tupcxt);
	nprocessed = SPI_processed;
	tuptable = SPI_tuptable;
	for (i = 0; i < nprocessed; i++)
	{
		Datum		cmdvalues[6];	/* # cols returned by above query */
		bool		cmdnulls[6];

		MemoryContextReset(tupcxt);

		/* this is the tuple reported by event triggers */
		heap_deform_tuple(tuptable->vals[i], tuptable->tupdesc,
						  cmdvalues, cmdnulls);

		/* if a temp object, ignore it */
		if (!cmdnulls[2] &&
			(strcmp(TextDatumGetCString(cmdvalues[2]), "pg_temp") == 0))
			continue;

		/* if in_extension, ignore the command */
		if (DatumGetBool(cmdvalues[4]))
			continue;

		bdr_queue_ddl_command(TextDatumGetCString(cmdvalues[0]),
							  TextDatumGetCString(cmdvalues[5]),
							  NULL);
	}

	SPI_finish();
	PopActiveSnapshot();

	PG_RETURN_VOID();
}

/*
 * bdr_queue_dropped_objects
 * 		sql_drop event triggger handler for BDR
 *
 * This function queues DROPs for replay by other BDR nodes.
 */
Datum
bdr_queue_dropped_objects(PG_FUNCTION_ARGS)
{
	char	   *skip_ddl;
	int			res;
	int			i;
	Oid			schema_oid;
	Oid			elmtype;
	int16		elmlen;
	bool		elmbyval;
	char		elmalign;
	int			droppedcnt = 0;
	Datum	   *droppedobjs;
	ArrayType  *droppedarr;
	TupleDesc	tupdesc;
	uint32		nprocessed;
	SPITupleTable *tuptable;

	if (!CALLED_AS_EVENT_TRIGGER(fcinfo))	/* internal error */
		elog(ERROR, "%s: not fired by event trigger manager",
			 "bdr_queue_dropped_objects");

	/*
	 * If the trigger comes from DDL executed by bdr_replicate_ddl_command,
	 * don't queue it as it would insert duplicate commands into the queue.
	 */
	if (in_bdr_replicate_ddl_command)
		PG_RETURN_VOID();		/* XXX return type? */

	/*
	 * If we're currently replaying something from a remote node, don't queue
	 * the commands; that would cause recursion.
	 */
	if (replorigin_session_origin != InvalidRepOriginId)
		PG_RETURN_VOID();		/* XXX return type? */

	/*
	 * Similarly, if configured to skip queueing DDL, don't queue.  This is
	 * mostly used when pg_restore brings a remote node state, so all objects
	 * will be copied over in the dump anyway.
	 */
	skip_ddl = GetConfigOptionByName("bdr.skip_ddl_replication", NULL, false);
	if (strcmp(skip_ddl, "on") == 0)
		PG_RETURN_VOID();

	/*
	 * We don't support DDL replication on bdr9.6 alpha yet. At all. So we
	 * should not replicate drops either.
	 *
	 * XXX TODO
	 */
	if (PG_VERSION_NUM >= 90600)
	{
		ereport(DEBUG1,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("DROP not replicated due to missing replication support"),
				 errhint("Use bdr.bdr_replicate_ddl_command(...) instead")));
		PG_RETURN_VOID();
	}

	/*
	 * Connect to SPI early, so that all memory allocated in this routine is
	 * released when we disconnect.
	 */
	SPI_connect();

	res = SPI_execute("SELECT "
					  "   original, normal, object_type, "
					  "   address_names, address_args "
					  "FROM pg_event_trigger_dropped_objects()"
#if PG_VERSION_NUM >= 90600
					  "WHERE !is_temporary"
#endif
					  ,false, 0);
	if (res != SPI_OK_SELECT)
		elog(ERROR, "SPI query failed: %d", res);

	/*
	 * Build array of dropped objects based on the results of the query.
	 */
	nprocessed = SPI_processed;
	tuptable = SPI_tuptable;

	droppedobjs = (Datum *) MemoryContextAlloc(fcinfo->flinfo->fn_mcxt,
											   sizeof(Datum) * nprocessed);

	schema_oid = get_namespace_oid("bdr", false);
	elmtype = bdr_lookup_relid("dropped_object", schema_oid);
	elmtype = get_rel_type_id(elmtype);

	get_typlenbyvalalign(elmtype, &elmlen, &elmbyval, &elmalign);
	tupdesc = TypeGetTupleDesc(elmtype, NIL);

	for (i = 0; i < nprocessed; i++)
	{
		Datum		cmdvalues[5];	/* # cols returned by above query */
		bool		cmdnulls[5];
		Datum		values[3];
		bool		nulls[3];
		HeapTuple	tuple;
		MemoryContext oldcontext;

		/* this is the tuple reported by event triggers */
		heap_deform_tuple(tuptable->vals[i], tuptable->tupdesc,
						  cmdvalues, cmdnulls);

		/* if not original or normal skip */
		if ((cmdnulls[0] || !DatumGetBool(cmdvalues[0])) &&
			(cmdnulls[1] || !DatumGetBool(cmdvalues[1])))
			continue;

		nulls[0] = cmdnulls[2];
		nulls[1] = cmdnulls[3];
		nulls[2] = cmdnulls[4];
		values[0] = cmdvalues[2];
		values[1] = cmdvalues[3];
		values[2] = cmdvalues[4];

		oldcontext = MemoryContextSwitchTo(fcinfo->flinfo->fn_mcxt);
		tuple = heap_form_tuple(tupdesc, values, nulls);
		droppedobjs[droppedcnt] = HeapTupleGetDatum(tuple);
		droppedcnt++;
		MemoryContextSwitchTo(oldcontext);
	}

	SPI_finish();

	/* No objects dropped? */
	if (droppedcnt == 0)
		PG_RETURN_VOID();

	droppedarr = construct_array(droppedobjs, droppedcnt,
								 elmtype, elmlen, elmbyval, elmalign);

	/*
	 * Insert the dropped object(s) info into the bdr_queued_drops table
	 */
	{
		EState	   *estate;
		TupleTableSlot *slot;
		RangeVar   *rv;
		Relation	queuedcmds;
		HeapTuple	newtup = NULL;
		Datum		values[5];
		bool		nulls[5];

		/*
		 * Prepare bdr.bdr_queued_drops for insert. Can't use preloaded table
		 * oid since this method is executed under normal backends and not
		 * inside BDR worker. The tuple slot here is only needed for updating
		 * indexes.
		 */
		rv = makeRangeVar("bdr", "bdr_queued_drops", -1);
		queuedcmds = heap_openrv(rv, RowExclusiveLock);
		slot = MakeSingleTupleTableSlot(RelationGetDescr(queuedcmds));
		estate = bdr_create_rel_estate(queuedcmds);
		ExecOpenIndices(estate->es_result_relation_info, false);

		/* lsn, queued_at, dropped_objects */
		values[0] = pg_current_wal_lsn(NULL);
		values[1] = now(NULL);
		values[2] = PointerGetDatum(droppedarr);
		MemSet(nulls, 0, sizeof(nulls));

		newtup = heap_form_tuple(RelationGetDescr(queuedcmds), values, nulls);
		simple_heap_insert(queuedcmds, newtup);
		ExecStoreTuple(newtup, slot, InvalidBuffer, false);
		UserTableUpdateOpenIndexes(estate, slot);

		ExecCloseIndices(estate->es_result_relation_info);
		ExecDropSingleTupleTableSlot(slot);
		heap_close(queuedcmds, RowExclusiveLock);
	}

	PG_RETURN_VOID();
}
