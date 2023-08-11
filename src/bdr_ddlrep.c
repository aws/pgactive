/* -------------------------------------------------------------------------
 *
 * bdr_ddlrep.c
 *      DDL replication
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *      bdr_ddlrep.c
 *
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include "bdr.h"

#include "access/xlog.h"

#include "catalog/catalog.h"
#include "catalog/namespace.h"

#include "executor/executor.h"

#include "miscadmin.h"

#include "replication/origin.h"

#include "nodes/makefuncs.h"

#include "storage/lmgr.h"

#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"

bool		in_bdr_replicate_ddl_command = false;

PGDLLEXPORT Datum bdr_replicate_ddl_command(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(bdr_replicate_ddl_command);

/*
 * bdr_queue_ddl_command
 *
 * Insert DDL command into the bdr.bdr_queued_commands table.
 */
void
bdr_queue_ddl_command(const char *command_tag, const char *command, const char *search_path)
{
	EState	   *estate;
	TupleTableSlot *slot;
	RangeVar   *rv;
	Relation	queuedcmds;
	ResultRelInfo *relinfo = makeNode(ResultRelInfo);
	HeapTuple	newtup = NULL;
	Datum		values[6];
	bool		nulls[6];

	elog(DEBUG2, "node %s enqueuing DDL command \"%s\" "
		 "with search_path \"%s\"",
		 bdr_local_node_name(false), command,
		 search_path == NULL ? "" : search_path);

	if (search_path == NULL)
		search_path = "";

	/* prepare bdr.bdr_queued_commands for insert */
	rv = makeRangeVar("bdr", "bdr_queued_commands", -1);
	queuedcmds = table_openrv(rv, RowExclusiveLock);
	slot = MakeSingleTupleTableSlot(RelationGetDescr(queuedcmds));
	estate = bdr_create_rel_estate(queuedcmds, relinfo);
#if PG_VERSION_NUM < 140000
	relinfo = estate->es_result_relation_info;
#endif

	ExecOpenIndices(relinfo, false);

	/* lsn, queued_at, perpetrator, command_tag, command */
	MemSet(nulls, 0, sizeof(nulls));
	values[0] = pg_current_wal_lsn(NULL);
	values[1] = now(NULL);
	values[2] = PointerGetDatum(cstring_to_text(GetUserNameFromId(GetUserId(), false)));
	values[3] = CStringGetTextDatum(command_tag);
	values[4] = CStringGetTextDatum(command);
	values[5] = CStringGetTextDatum(search_path);

	newtup = heap_form_tuple(RelationGetDescr(queuedcmds), values, nulls);
	simple_heap_insert(queuedcmds, newtup);
	ExecStoreHeapTuple(newtup, slot, false);
	UserTableUpdateOpenIndexes(estate, slot, relinfo, false);

	ExecCloseIndices(relinfo);
	ExecDropSingleTupleTableSlot(slot);
	table_close(queuedcmds, RowExclusiveLock);
}

/*
 * bdr_replicate_ddl_command
 *
 * Queues the input SQL for replication.
 *
 * Note that we don't allow CONCURRENTLY commands here, this is mainly because
 * we queue command before we actually execute it, which we currently need
 * to make the bdr_truncate_trigger_add work correctly. As written there
 * the in_bdr_replicate_ddl_command concept is ugly.
 */
Datum
bdr_replicate_ddl_command(PG_FUNCTION_ARGS)
{
	text	   *command = PG_GETARG_TEXT_PP(0);
	char	   *query = text_to_cstring(command);
	int			nestlevel = -1;

	if (bdr_skip_ddl_replication)
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("bdr_replicate_ddl_command execution attempt rejected by configuration"),
				 errdetail("bdr.skip_ddl_replication is true."),
				 errhint("See the 'DDL replication' chapter of the documentation.")));
	}

	nestlevel = NewGUCNestLevel();

	/* Force everything in the query to be fully qualified. */
	(void) set_config_option("search_path", "",
							 PGC_USERSET, PGC_S_SESSION,
							 GUC_ACTION_SAVE, true, 0, false);

	/* Execute the query locally. */
	in_bdr_replicate_ddl_command = true;

	PG_TRY();
	{
		/* Queue the query for replication. */
		bdr_queue_ddl_command("SQL", query, NULL);

		/* Execute the query locally. */
		bdr_execute_ddl_command(query, GetUserNameFromId(GetUserId(), false), "" /* search_path */ , false);
	}
	PG_CATCH();
	{
		if (nestlevel > 0)
			AtEOXact_GUC(true, nestlevel);
		in_bdr_replicate_ddl_command = false;
		PG_RE_THROW();
	}
	PG_END_TRY();

	if (nestlevel > 0)
		AtEOXact_GUC(true, nestlevel);
	in_bdr_replicate_ddl_command = false;
	PG_RETURN_VOID();
}

/* -------------------------------------------------------------------------
 * bdr_capture_ddl: remodeled DDL replication for BDR on PostgreSQL 9.6. this
 * approach eschews use of DDL deparse, instead capturing raw SQL at
 * ProcessUtility_hook and the associated search_path. It's called from the
 * command filter.
 *
 * There's an unavoidable flaw with this approach, which is that differences in
 * object existence on upstream and downstream can cause DDL to have silently
 * different results. For example, if s_p is
 *
 *   schema1, schema2
 *
 * and schema1 is nonexistent on the upstream node, we'll CREATE TABLE in schema2
 * on the upstream. But if schema1 exists on the downstream we'll CREATE TABLE
 * on schema1 there. Oops. Our row replication is always schema-qualified so
 * subsequent data replication fail fail due to a missing table.
 *
 * Similarly, an ALTER TABLE or DROP TABLE can go to the wrong place if the
 * table exists in an earlier schema on the downstream than in the upstream.
 *
 * In BDR none of these situations should arise in the first place, since we
 * expect the schema to be consistent across nodes. If they do, it's a mess.
 * But deparse has proved to be less robust than originally expected too, and
 * it's hard to support in 9.6, so this will do.
 *
 * Users should be encouraged to
 *
 *   SET search_path = ''
 *
 * before running DDL then explicitly schema-qualify everything. pg_catalog
 * will still be implicitly searched so they don't have to qualify basic types
 * and operators.
 *
 * This function leaks all over the place; we rely on the statement context
 * to clean up.
 *
 * -------------------------------------------------------------------------
 */
void
bdr_capture_ddl(Node *parsetree, const char *queryString,
				ProcessUtilityContext context, ParamListInfo params,
				DestReceiver *dest, CommandTag completionTag)
{
	ListCell   *lc;
	StringInfoData si;
	List	   *active_search_path;
	CommandTag	tag = completionTag;
	bool		first;

	initStringInfo(&si);

	/*
	 * If the call comes from DDL executed by bdr_replicate_ddl_command, don't
	 * queue it as it would insert duplicate commands into the queue.
	 */
	if (in_bdr_replicate_ddl_command)
		return;

	/*
	 * If we're currently replaying something from a remote node, don't queue
	 * the commands; that would cause recursion.
	 */
	if (replorigin_session_origin != InvalidRepOriginId)
		return;

	/*
	 * Similarly, if configured to skip queueing DDL, don't queue.  This is
	 * mostly used when pg_restore brings a remote node state, so all objects
	 * will be copied over in the dump anyway.
	 */
	if (bdr_skip_ddl_replication)
		return;

	/*
	 * We can't use namespace_search_path since there might be an override
	 * search path active right now, so:
	 */
	active_search_path = fetch_search_path(true);

	/*
	 * We have to look up each namespace name by oid and reconstruct a
	 * search_path string. It's lucky DDL is already expensive.
	 *
	 * Note that this means we'll ignore search_path entries that don't exist
	 * on the upstream since they never made it onto active_search_path.
	 */
	first = true;
	foreach(lc, active_search_path)
	{
		Oid			nspid = lfirst_oid(lc);
		char	   *nspname;

		if (IsCatalogNamespace(nspid) || IsToastNamespace(nspid) || isTempOrTempToastNamespace(nspid))
			continue;
		nspname = get_namespace_name(nspid);
		if (!first)
			appendStringInfoString(&si, ",");
		appendStringInfoString(&si, quote_identifier(nspname));
	}

	if (!IsKnownTag(tag))
		tag = CreateCommandTag(parsetree);

	bdr_queue_ddl_command(GetCommandTagName(tag), queryString, si.data);

	resetStringInfo(&si);
}
