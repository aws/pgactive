/* -------------------------------------------------------------------------
 *
 * bdr_ddlrep_truncate.c
 *      Support for replicating table truncation
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *      bdr_ddlrep_truncate.c
 *
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include "bdr.h"

#include "access/xact.h"

#include "catalog/pg_proc.h"
#include "catalog/pg_trigger.h"

#include "commands/event_trigger.h"
#include "commands/trigger.h"

#include "nodes/makefuncs.h"

#include "parser/parse_func.h"

#include "replication/origin.h"

#include "storage/lmgr.h"

#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"

PGDLLEXPORT Datum bdr_queue_truncate(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(bdr_queue_truncate);
PGDLLEXPORT Datum bdr_truncate_trigger_add(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(bdr_truncate_trigger_add);
PGDLLEXPORT Datum bdr_internal_create_truncate_trigger(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(bdr_internal_create_truncate_trigger);

static List *bdr_truncated_tables = NIL;

/*
 * Create a TRUNCATE trigger for a persistent table and mark
 * it tgisinternal so that it's not dumped by pg_dump.
 *
 * We create such triggers automatically on restore or
 * bdr_group_create so dumping the triggers isn't necessary,
 * and dumping them makes it harder to restore to a DB
 * without BDR.
 *
 * The target object oid may be InvalidOid, in which case
 * it will be looked up from the catalogs.
 */
static void
bdr_create_truncate_trigger(char *schemaname, char *relname, Oid relid)
{
	CreateTrigStmt *tgstmt;
	RangeVar   *relrv = makeRangeVar(schemaname, relname, -1);
	Relation	rel;
	List	   *funcname;
	ObjectAddress tgaddr,
				procaddr;
	int			nfound;
	Oid			fargtypes[1];	/* dummy, see 0a52d378 */

	if (OidIsValid(relid))
		rel = table_open(relid, AccessExclusiveLock);
	else
		rel = table_openrv(relrv, AccessExclusiveLock);

	funcname = list_make2(makeString("bdr"), makeString("queue_truncate"));


	/*
	 * Check for already existing trigger on the table to avoid adding
	 * duplicate ones.
	 */
	if (rel->trigdesc)
	{
		Trigger    *trigger = rel->trigdesc->triggers;
		int			i;
		Oid			funcoid = LookupFuncName(funcname, 0, &fargtypes[0], false);

		for (i = 0; i < rel->trigdesc->numtriggers; i++)
		{
			if (!TRIGGER_FOR_TRUNCATE(trigger->tgtype))
				continue;

			if (trigger->tgfoid == funcoid)
			{
				table_close(rel, AccessExclusiveLock);
				return;
			}

			trigger++;
		}
	}

	tgstmt = makeNode(CreateTrigStmt);
	tgstmt->trigname = "truncate_trigger";
	tgstmt->relation = copyObject(relrv);
	tgstmt->funcname = funcname;
	tgstmt->args = NIL;
	tgstmt->row = false;
	tgstmt->timing = TRIGGER_TYPE_AFTER;
	tgstmt->events = TRIGGER_TYPE_TRUNCATE;
	tgstmt->columns = NIL;
	tgstmt->whenClause = NULL;
	tgstmt->isconstraint = false;
	tgstmt->deferrable = false;
	tgstmt->initdeferred = false;
	tgstmt->constrrel = NULL;

	tgaddr = CreateTrigger(tgstmt, NULL, rel->rd_id, InvalidOid,
						   InvalidOid, InvalidOid,
						   InvalidOid, InvalidOid, NULL,
						   true /* tgisinternal */ , false);

	/*
	 * The trigger was created with a 'n'ormal dependency on
	 * bdr.queue_truncate(), which will cause DROP EXTENSION bdr to fail with
	 * something like:
	 *
	 * trigger truncate_trigger_26908 on table sometable depends on function
	 * bdr.queue_truncate()
	 *
	 * We want the trigger to bdr dropped if EITHER the BDR extension is
	 * dropped (thus so is bdr.queue_truncate()) OR if the table the trigger
	 * is attached to is dropped, so we want an automatic dependency on the
	 * target table. CreateTrigger doesn't offer this directly and we'd rather
	 * not cause an API break by adding a param, so just twiddle the created
	 * dependency.
	 */

	procaddr.classId = ProcedureRelationId;
	procaddr.objectId = LookupFuncName(list_make2(makeString("bdr"), makeString("queue_truncate")), 0, &fargtypes[0], false);
	procaddr.objectSubId = 0;

	/* We need to be able to see the pg_depend entry to delete it */
	CommandCounterIncrement();

	if ((nfound = deleteDependencyRecordsForClass(tgaddr.classId, tgaddr.objectId, ProcedureRelationId, 'n')) != 1)
	{
		ereport(ERROR,
				(errmsg_internal("expected exectly one 'n'ormal dependency from a newly created trigger to a pg_proc entry, got %u", nfound)));
	}

	recordDependencyOn(&tgaddr, &procaddr, DEPENDENCY_AUTO);

	/* We should also record that the trigger is part of the extension */
	recordDependencyOnCurrentExtension(&tgaddr, false);

	table_close(rel, AccessExclusiveLock);

	/* Make the new trigger visible within this session */
	CommandCounterIncrement();
}

/*
 * Wrapper to call bdr_create_truncate_trigger from SQL for
 * during bdr_group_create(...).
 */
Datum
bdr_internal_create_truncate_trigger(PG_FUNCTION_ARGS)
{
	Oid			relid = PG_GETARG_OID(0);
	Relation	rel = table_open(relid, AccessExclusiveLock);
	char	   *schemaname = get_namespace_name(RelationGetNamespace(rel));

	bdr_create_truncate_trigger(schemaname, RelationGetRelationName(rel), relid);
	pfree(schemaname);
	table_close(rel, AccessExclusiveLock);
	PG_RETURN_VOID();
}


/*
 * bdr_truncate_trigger_add
 *
 * This function, which is called as an event trigger handler, adds TRUNCATE
 * trigger to newly created tables where appropriate.
 *
 * Note: it's important that this function be named so that it comes
 * after bdr_queue_ddl_commands when triggers are alphabetically sorted.
 */
Datum
bdr_truncate_trigger_add(PG_FUNCTION_ARGS)
{
	EventTriggerData *trigdata;

	if (!CALLED_AS_EVENT_TRIGGER(fcinfo))	/* internal error */
		elog(ERROR, "not fired by event trigger manager");

	/*
	 * Since triggers are created tgisinternal and their creation is not
	 * replicated or dumped we must create truncate triggers on tables even if
	 * they're created by a replicated command or restore of a dump. Recursion
	 * is not a problem since we don't queue anything for replication anymore.
	 */

	trigdata = (EventTriggerData *) fcinfo->context;

	if (strcmp(GetCommandTagName(trigdata->tag), "CREATE TABLE") == 0 &&
		IsA(trigdata->parsetree, CreateStmt))
	{
		CreateStmt *stmt = (CreateStmt *) trigdata->parsetree;
		char	   *nspname;

		/* Skip temporary and unlogged tables */
		if (stmt->relation->relpersistence != RELPERSISTENCE_PERMANENT)
			PG_RETURN_VOID();

		nspname = get_namespace_name(RangeVarGetCreationNamespace(stmt->relation));

		/*
		 * By this time the relation has been created so it's safe to call
		 * RangeVarGetRelid
		 */
		bdr_create_truncate_trigger(nspname, stmt->relation->relname, InvalidOid);

		pfree(nspname);
	}

	PG_RETURN_VOID();
}


/*
 * Initializes the internal table list.
 */
void
bdr_start_truncate(void)
{
	bdr_truncated_tables = NIL;
}

/*
 * Write the list of truncated tables to the replication queue.
 */
void
bdr_finish_truncate(void)
{
	ListCell   *lc;
	char	   *sep = "";
	StringInfoData buf;

	/* Nothing to do if the list of truncated table is empty. */
	if (list_length(bdr_truncated_tables) < 1)
		return;

	initStringInfo(&buf);
	appendStringInfoString(&buf, "TRUNCATE TABLE ONLY ");

	foreach(lc, bdr_truncated_tables)
	{
		Oid			reloid = lfirst_oid(lc);
		char	   *relname;

		relname = quote_qualified_identifier(
											 get_namespace_name(get_rel_namespace(reloid)),
											 get_rel_name(reloid));

		appendStringInfoString(&buf, sep);
		appendStringInfoString(&buf, relname);
		sep = ", ";
	}

	bdr_queue_ddl_command("TRUNCATE (automatic)", buf.data, NULL);

	list_free(bdr_truncated_tables);
	bdr_truncated_tables = NIL;
}

/*
 * bdr_queue_truncate
 * 		TRUNCATE trigger
 *
 * This function only writes to internal linked list, actual queueing is done
 * by bdr_finish_truncate().
 */
Datum
bdr_queue_truncate(PG_FUNCTION_ARGS)
{
	TriggerData *tdata = (TriggerData *) fcinfo->context;
	MemoryContext oldcontext;

	if (!CALLED_AS_TRIGGER(fcinfo)) /* internal error */
		ereport(ERROR,
				(errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
				 errmsg("function \"%s\" was not called by trigger manager",
						"bdr_queue_truncate")));

	if (!TRIGGER_FIRED_BY_TRUNCATE(tdata->tg_event))	/* internal error */
		elog(ERROR, "function \"%s\" was not called by TRUNCATE",
			 "bdr_queue_truncate");

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

	/* Make sure the list change survives the trigger call. */
	oldcontext = MemoryContextSwitchTo(TopTransactionContext);
	bdr_truncated_tables = lappend_oid(bdr_truncated_tables,
									   RelationGetRelid(tdata->tg_relation));
	MemoryContextSwitchTo(oldcontext);

	PG_RETURN_VOID();
}
