/* -------------------------------------------------------------------------
 *
 * pgactive_node_identifier.c
 *		pgactive node identifier related code - user-facing functions, static
 *      getter function, shmem cache for storing per-db pgactive node
 * 		identifier etc.
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_node_identifier.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pgactive.h"

#include "access/xact.h"
#include "catalog/pg_type.h"
#include "commands/dbcommands.h"
#include "executor/spi.h"
#include "parser/parse_func.h"
#include "storage/proc.h"
#include "utils/fmgrprotos.h"
#include "utils/lsyscache.h"
#include "utils/snapmgr.h"

PGDLLEXPORT Datum pgactive_generate_node_identifier(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum pgactive_get_node_identifier(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pgactive_generate_node_identifier);
PG_FUNCTION_INFO_V1(pgactive_get_node_identifier);

static shmem_startup_hook_type prev_shmem_startup_hook = NULL;
pgactiveNodeIdentifierControl *pgactiveNodeIdentifierCtl = NULL;

/* callback to cleanup on abort */
bool		cb_registered = false;

/* global indicator we are manipulating pgactive artifacts */
bool		pgactiveart = false;

#define SET_pgactiveART \
do { \
	if (!cb_registered) \
	{ \
		RegisterXactCallback(pg_pgactive_xact_callback, NULL); \
		cb_registered = true; \
	} \
	pgactiveart = true; \
} while (0)

#define UNSET_pgactiveART \
do { \
	pgactiveart = false; \
} while (0)

static size_t pgactive_nid_shmem_size(void);
static void pgactive_nid_shmem_startup(void);
static void pgactive_nid_shmem_reset(Oid dboid);
static void pgactive_nid_shmem_reset_all(bool need_lock);
static void pgactive_nid_shmem_set(Oid dboid, uint64 nid);
static uint64 pgactive_nid_shmem_get(Oid dboid);
static void pg_pgactive_xact_callback(XactEvent event, void *arg);
static void pgactive_spi_exec(const char *cmd, int ret);
static bool get_pgactive_nid_getter_function_dependency(void);
static bool is_pgactive_nid_getter_function_in_stmt(ObjectType objtype,
											   Node *object);

static bool
get_pgactive_nid_getter_function_dependency(void)
{
	StringInfoData cmd;
	char	   *getter_func_dependency;
	bool		is_getter_func_part_of_extension;

	initStringInfo(&cmd);
	appendStringInfo(&cmd, "SELECT EXISTS ( "
					 "SELECT 1 FROM pg_proc p JOIN pg_depend d ON p.oid = d.objid "
					 "WHERE p.proname = '%s' "
					 "AND deptype = 'e' AND refobjid = "
					 "(SELECT oid FROM pg_extension WHERE extname = 'pgactive'));",
					 pgactive_NID_GETTER_FUNC_NAME);

	if (SPI_connect() != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed");

	if (SPI_execute(cmd.data, false, 0) != SPI_OK_SELECT)
		elog(ERROR, "SPI_execute failed: %s", cmd.data);

	Assert(SPI_processed == 1);
	Assert(SPI_tuptable->tupdesc->natts == 1);
	getter_func_dependency = SPI_getvalue(SPI_tuptable->vals[0],
										  SPI_tuptable->tupdesc, 1);

	if (strcmp(getter_func_dependency, "f") == 0)
		is_getter_func_part_of_extension = false;
	else
		is_getter_func_part_of_extension = true;

	if (SPI_finish() != SPI_OK_FINISH)
		elog(ERROR, "SPI_finish failed");

	return is_getter_func_part_of_extension;
}

/*
 * Generate a pgactive node identifier and store it in a static getter function. The
 * static getter function approach not only helps each database joining pgactive
 * group get a unique identifier, but also helps achieve failover of pgactive node
 * in streaming replication (while keeping the same node identifiers) as the
 * standby will have the getter function replicated to it.
 */
Datum
pgactive_generate_node_identifier(PG_FUNCTION_ARGS)
{
	uint64		nid;
	StringInfoData cmd;
	char		buf[256];
	bool		is_getter_func_part_of_extension;

	Assert(IsTransactionState());

	/*
	 * Clear the node id in cache because it can happen that the previous
	 * attempt to generate node identifier, or join a node to pgactive group may
	 * have failed.
	 */
	pgactive_nid_shmem_reset(MyDatabaseId);

	/*
	 * Generate pgactive node identifier using similar logic that Postgres uses to
	 * generate system_identifier.
	 */
	nid = GenerateNodeIdentifier();

	snprintf(buf, sizeof(buf), UINT64_FORMAT, nid);

	initStringInfo(&cmd);
	appendStringInfo(&cmd, "CREATE OR REPLACE FUNCTION pgactive.%s() RETURNS numeric AS $$ "
					 "SELECT %s::numeric $$ LANGUAGE SQL;",
					 pgactive_NID_GETTER_FUNC_NAME,
					 buf);

	/* flag that we are manipulating pgactive artifacts */
	SET_pgactiveART;

	pgactive_spi_exec(cmd.data, SPI_OK_UTILITY);

	is_getter_func_part_of_extension =
		get_pgactive_nid_getter_function_dependency();

	/* If getter function isn't part of pgactive extension, add it */
	if (!is_getter_func_part_of_extension)
	{
		resetStringInfo(&cmd);
		appendStringInfo(&cmd, "ALTER EXTENSION pgactive ADD FUNCTION pgactive.%s();",
						 pgactive_NID_GETTER_FUNC_NAME);

		pgactive_spi_exec(cmd.data, SPI_OK_UTILITY);
	}

	/* done manipulating pgactive artifacts */
	UNSET_pgactiveART;

	pgactive_nid_shmem_set(MyDatabaseId, nid);
	pfree(cmd.data);
	PG_RETURN_VOID();
}

/*
 * Get pgactive node identifier for current database. First it looks in shmem cache,
 * upon cache miss, reads from the getter function and fills the cache.
 */
uint64
pgactive_get_nid_internal(void)
{
	StringInfoData cmd;
	char	   *nid_str;
	uint64		nid;
	bool		tx_started = false;

	nid = pgactive_nid_shmem_get(MyDatabaseId);

	if (nid != 0)
		return nid;

	if (!IsTransactionState())
	{
		tx_started = true;
		StartTransactionCommand();
	}

	initStringInfo(&cmd);
	appendStringInfo(&cmd, "SELECT * FROM pgactive.%s();",
					 pgactive_NID_GETTER_FUNC_NAME);

	if (SPI_connect() != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed");

	if (SPI_execute(cmd.data, false, 0) != SPI_OK_SELECT)
		elog(ERROR, "SPI_execute failed: %s", cmd.data);

	Assert(SPI_processed == 1);
	Assert(SPI_tuptable->tupdesc->natts == 1);
	nid_str = SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1);

	if (sscanf(nid_str, UINT64_FORMAT, &nid) != 1)
		elog(ERROR, "parsing pgactive node identifier to uint64 from %s failed",
			 nid_str);

	if (SPI_finish() != SPI_OK_FINISH)
		elog(ERROR, "SPI_finish failed");

	if (tx_started)
		CommitTransactionCommand();

	Assert(nid != 0);

	/* Save the read node identifier in the cache. */
	pgactive_nid_shmem_set(MyDatabaseId, nid);
	pfree(cmd.data);

	return nid;
}

/*
 * User-facing function to get pgactive node identifier for current database.
 */
Datum
pgactive_get_node_identifier(PG_FUNCTION_ARGS)
{
	uint64		nid;
	char		buf[256];
	Datum		result;

	nid = pgactive_get_nid_internal();

	/* Convert to numeric. */
	snprintf(buf, sizeof(buf), UINT64_FORMAT, nid);
	result = DirectFunctionCall3(numeric_in,
								 CStringGetDatum(buf),
								 ObjectIdGetDatum(0),
								 Int32GetDatum(-1));

	PG_RETURN_DATUM(result);
}

/*
 * Cleanup at main-transaction end.
 */
static void
pg_pgactive_xact_callback(XactEvent event, void *arg)
{
	/* end pgactive artifacts */
	UNSET_pgactiveART;
}

/*
 * Check if pgactive is creating pgactive node identifier getter function.
 */
bool
is_pgactive_creating_nid_getter_function(void)
{
	return pgactiveart;
}

/*
 * A helper for pgactive around SPI facility.
 */
static void
pgactive_spi_exec(const char *cmd, int ret)
{
	if (SPI_connect() != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed");

	if (SPI_execute(cmd, false, 0) != ret)
		elog(ERROR, "SPI_execute failed: %s", cmd);

	if (SPI_finish() != SPI_OK_FINISH)
		elog(ERROR, "SPI_finish failed");
}

/*
 * Check if pgactive node identifier getter function exists.
 */
Oid
find_pgactive_nid_getter_function(void)
{
	List	   *funcname;
	Oid			funcoid;
	bool		tx_started = false;
	Oid			args[1] = {VOIDOID};

	if (!IsTransactionState())
	{
		tx_started = true;
		StartTransactionCommand();
	}

	funcname = list_make2(makeString("pgactive"), makeString(pgactive_NID_GETTER_FUNC_NAME));
	funcoid = LookupFuncName(funcname, 0, args, true);

	if (tx_started)
		CommitTransactionCommand();

	return funcoid;
}

/*
 * Check if pgactive node identifier getter function is being created.
 */
bool
is_pgactive_nid_getter_function_create(CreateFunctionStmt *stmt)
{
	char	   *funcname;
	char	   *schemaname;

	/* deconstruct the name list */
	DeconstructQualifiedName(stmt->funcname,
							 &schemaname,
							 &funcname);
	if (funcname != NULL &&
		pg_strcasecmp(funcname, pgactive_NID_GETTER_FUNC_NAME) == 0)
		return true;

	return false;
}

/*
 * Check if pgactive node identifier getter function is being dropped.
 */
bool
is_pgactive_nid_getter_function_drop(DropStmt *stmt)
{
	ListCell   *lc;

	foreach(lc, stmt->objects)
	{
		Node	   *object = lfirst(lc);

		if (is_pgactive_nid_getter_function_in_stmt(stmt->removeType,
											   object))
			return true;
	}

	return false;
}

/*
 * Check if pgactive node identifier getter function is being altered.
 */
bool
is_pgactive_nid_getter_function_alter(AlterFunctionStmt *stmt)
{
	Oid			funcoid;
	char	   *funcname;

	funcoid = LookupFuncWithArgs(stmt->objtype, stmt->func, true);

	if (!OidIsValid(funcoid))
		return false;

	funcname = get_func_name(funcoid);

	if (funcname != NULL &&
		pg_strcasecmp(funcname, pgactive_NID_GETTER_FUNC_NAME) == 0)
		return true;

	return false;
}

/*
 * Check if pgactive node identifier getter function is being altered
 * (ALTER FUNCTION OWNER TO).
 */
bool
is_pgactive_nid_getter_function_alter_owner(AlterOwnerStmt *stmt)
{
	return is_pgactive_nid_getter_function_in_stmt(stmt->objectType,
											  stmt->object);
}

/*
 * Check if pgactive node identifier getter function is being altered
 * (ALTER FUNCTION RENAME TO).
 */
bool
is_pgactive_nid_getter_function_alter_rename(RenameStmt *stmt)
{
	return is_pgactive_nid_getter_function_in_stmt(stmt->renameType,
											  stmt->object);
}

static bool
is_pgactive_nid_getter_function_in_stmt(ObjectType objtype, Node *object)
{
	Relation	relation;
	ObjectAddress address;
	char	   *funcname;

	if (objtype != OBJECT_FUNCTION)
		return false;

	address = get_object_address(objtype,
								 object,
								 &relation,
								 AccessExclusiveLock,
								 true);
	Assert(relation == NULL);

	if (!OidIsValid(address.objectId))
		return false;

	funcname = get_func_name(address.objectId);

	if (funcname != NULL &&
		pg_strcasecmp(funcname, pgactive_NID_GETTER_FUNC_NAME) == 0)
		return true;

	return false;
}

/*
 * pgactive node identifier shared memory functions.
 */

static size_t
pgactive_nid_shmem_size(void)
{
	Size		size = 0;

	size = add_size(size, sizeof(pgactiveNodeIdentifierControl));
	size = add_size(size, mul_size(pgactive_max_databases, sizeof(pgactiveNodeIdentifier)));

	return size;
}

static void
pgactive_nid_shmem_startup(void)
{
	bool		found;

	if (prev_shmem_startup_hook != NULL)
		prev_shmem_startup_hook();

	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
	pgactiveNodeIdentifierCtl = ShmemInitStruct("pgactive_nid",
										   pgactive_nid_shmem_size(),
										   &found);
	if (!found)
	{
		memset(pgactiveNodeIdentifierCtl, 0, pgactive_nid_shmem_size());
		pgactiveNodeIdentifierCtl->lock = &(GetNamedLWLockTranche("pgactive_nid")->lock);
		pgactive_nid_shmem_reset_all(false);
	}
	LWLockRelease(AddinShmemInitLock);
}

/* Needs to be called from a shared_preload_library _PG_init() */
void
pgactive_nid_shmem_init(void)
{
	/* Must be called from postmaster its self */
	Assert(IsPostmasterEnvironment && !IsUnderPostmaster);

	pgactiveNodeIdentifierCtl = NULL;

	RequestAddinShmemSpace(pgactive_nid_shmem_size());
	RequestNamedLWLockTranche("pgactive_nid", 1);

	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = pgactive_nid_shmem_startup;
}

static void
pgactive_nid_shmem_reset(Oid dboid)
{
	int			i;

	LWLockAcquire(pgactiveNodeIdentifierCtl->lock, LW_EXCLUSIVE);
	for (i = 0; i < pgactive_max_databases; i++)
	{
		pgactiveNodeIdentifier *w = &pgactiveNodeIdentifierCtl->nids[i];

		if (w->dboid == dboid && w->nid != 0)
		{
			w->dboid = InvalidOid;
			w->nid = 0;
			break;
		}
	}
	LWLockRelease(pgactiveNodeIdentifierCtl->lock);
}

static void
pgactive_nid_shmem_reset_all(bool need_lock)
{
	int			i;

	if (need_lock)
		LWLockAcquire(pgactiveNodeIdentifierCtl->lock, LW_EXCLUSIVE);

	for (i = 0; i < pgactive_max_databases; i++)
	{
		pgactiveNodeIdentifier *w = &pgactiveNodeIdentifierCtl->nids[i];

		w->dboid = InvalidOid;
		w->nid = 0;
	}

	if (need_lock)
		LWLockRelease(pgactiveNodeIdentifierCtl->lock);
}

static void
pgactive_nid_shmem_set(Oid dboid, uint64 nid)
{
	int			i;

	LWLockAcquire(pgactiveNodeIdentifierCtl->lock, LW_EXCLUSIVE);
	for (i = 0; i < pgactive_max_databases; i++)
	{
		pgactiveNodeIdentifier *w = &pgactiveNodeIdentifierCtl->nids[i];

		if (w->dboid == InvalidOid && w->nid == 0)
		{
			w->dboid = dboid;
			w->nid = nid;
			break;
		}
	}
	LWLockRelease(pgactiveNodeIdentifierCtl->lock);
}

static uint64
pgactive_nid_shmem_get(Oid dboid)
{
	int			i;
	uint64		nid = 0;

	LWLockAcquire(pgactiveNodeIdentifierCtl->lock, LW_SHARED);
	for (i = 0; i < pgactive_max_databases; i++)
	{
		pgactiveNodeIdentifier *w = &pgactiveNodeIdentifierCtl->nids[i];

		if (w->dboid == dboid && w->nid != 0)
		{
			nid = w->nid;
			break;
		}
	}
	LWLockRelease(pgactiveNodeIdentifierCtl->lock);

	return nid;
}
