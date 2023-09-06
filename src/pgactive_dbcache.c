/* -------------------------------------------------------------------------
 *
 * pgactive_dbcache.c
 *		coherent, per database, cache for pgactive.
 *
 * Copyright (c) 2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_dbcache.c
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include "pgactive.h"

#include "miscadmin.h"

#include "access/xact.h"

#include "catalog/pg_database.h"

#include "commands/seclabel.h"

#include "utils/catcache.h"
#include "utils/inval.h"
#include "utils/jsonb.h"
#include "utils/builtins.h"

/* Cache entry. */
typedef struct pgactiveDatabaseCacheEntry
{
	Oid			oid;			/* cache key, needs to be first */
	const char *dbname;
	bool		valid;
	bool		pgactive_activated;
}			pgactiveDatabaseCacheEntry;

static HTAB *pgactiveDatabaseCacheHash = NULL;

static pgactiveDatabaseCacheEntry * pgactive_dbcache_lookup(Oid dboid, bool missing_ok);

static void
pgactive_dbcache_invalidate_entry(Datum arg, int cacheid, uint32 hashvalue)
{
	HASH_SEQ_STATUS status;
	pgactiveDatabaseCacheEntry *hentry;

	Assert(pgactiveDatabaseCacheHash != NULL);

	/*
	 * Currently we just reset all entries; it's unlikely anything more
	 * complicated is worthwile.
	 */
	hash_seq_init(&status, pgactiveDatabaseCacheHash);

	while ((hentry = (pgactiveDatabaseCacheEntry *) hash_seq_search(&status)) != NULL)
	{
		hentry->valid = false;
	}

}

static void
pgactive_dbcache_initialize()
{
	HASHCTL		ctl;

	/* Make sure we've initialized CacheMemoryContext. */
	if (CacheMemoryContext == NULL)
		CreateCacheMemoryContext();

	/* Initialize the hash table. */
	MemSet(&ctl, 0, sizeof(ctl));
	ctl.keysize = sizeof(Oid);
	ctl.entrysize = sizeof(pgactiveDatabaseCacheEntry);
	ctl.hash = tag_hash;
	ctl.hcxt = CacheMemoryContext;

	pgactiveDatabaseCacheHash = hash_create("pgactive database cache", 128, &ctl,
											HASH_ELEM | HASH_FUNCTION | HASH_CONTEXT);

	/* Watch for invalidation events. */
	CacheRegisterSyscacheCallback(DATABASEOID, pgactive_dbcache_invalidate_entry, (Datum) 0);
}

void
pgactive_parse_database_options(const char *label, bool *is_active)
{
	JsonbIterator *it;
	JsonbValue	v;
	int			r;
	int			level = 0;
	Jsonb	   *data = NULL;
	bool		parsing_pgactive = false;

	if (label == NULL)
		return;

	data = DatumGetJsonbP(
						  DirectFunctionCall1(jsonb_in, CStringGetDatum(label)));

	if (!JB_ROOT_IS_OBJECT(data))
		elog(ERROR, "root needs to be an object");

	it = JsonbIteratorInit(&data->root);
	while ((r = JsonbIteratorNext(&it, &v, false)) != WJB_DONE)
	{
		if (level == 0 && r != WJB_BEGIN_OBJECT)
			elog(ERROR, "root element needs to be an object");
		else if (level == 0 && it->nElems > 1)
			elog(ERROR, "only 'pgactive' allowed on root level");
		else if (level == 0 && r == WJB_BEGIN_OBJECT)
		{
			level++;
		}
		else if (level == 1 && r == WJB_KEY)
		{
			if (strncmp(v.val.string.val, "pgactive", v.val.string.len) != 0)
				elog(ERROR, "unexpected key: %s",
					 pnstrdup(v.val.string.val, v.val.string.len));
			parsing_pgactive = true;
		}
		else if (level == 1 && r == WJB_VALUE)
		{
			if (!parsing_pgactive)
				elog(ERROR, "in wrong state when parsing key");

			if (v.type != jbvBool)
				elog(ERROR, "unexpected type for key 'pgactive': %u", v.type);

			if (is_active != NULL)
				*is_active = v.val.boolean;
		}
		else if (level == 1 && r != WJB_END_OBJECT)
		{
			elog(ERROR, "unexpected content: %u at level %d", r, level);
		}
		else if (r == WJB_END_OBJECT)
		{
			level--;
			parsing_pgactive = false;
		}
		else
			elog(ERROR, "unexpected content: %u at level %d", r, level);

	}
}

/*
 * Lookup a database cache entry, via its oid.
 *
 * At some future point this probably will need to be externally accessible,
 * but right now we don't need it yet.
 */
static pgactiveDatabaseCacheEntry *
pgactive_dbcache_lookup(Oid dboid, bool missing_ok)
{
	pgactiveDatabaseCacheEntry *entry;
	bool		found;
	ObjectAddress object;
	HeapTuple	dbtuple;
	const char *label;

	if (pgactiveDatabaseCacheHash == NULL)
		pgactive_dbcache_initialize();

	/*
	 * HASH_ENTER returns the existing entry if present or creates a new one.
	 */
	entry = hash_search(pgactiveDatabaseCacheHash, (void *) &dboid,
						HASH_ENTER, &found);

	if (found && entry->valid)
		return entry;

	/* zero out data part of the entry */
	memset(((char *) entry) + offsetof(pgactiveDatabaseCacheEntry, dbname),
		   0,
		   sizeof(pgactiveDatabaseCacheEntry) - offsetof(pgactiveDatabaseCacheEntry, dbname));

	dbtuple = SearchSysCache1(DATABASEOID, ObjectIdGetDatum(dboid));

	if (!HeapTupleIsValid(dbtuple) && !missing_ok)
		elog(ERROR, "cache lookup failed for database %u", dboid);
	else if (!HeapTupleIsValid(dbtuple))
		return NULL;

	entry->dbname = MemoryContextStrdup(CacheMemoryContext,
										NameStr(((Form_pg_database) GETSTRUCT(dbtuple))->datname));

	ReleaseSysCache(dbtuple);

	object.classId = DatabaseRelationId;
	object.objectId = dboid;
	object.objectSubId = 0;

	label = GetSecurityLabel(&object, pgactive_SECLABEL_PROVIDER);
	pgactive_parse_database_options(label, &entry->pgactive_activated);

	entry->valid = true;

	return entry;
}


/*
 * Is the database configured for pgactive?
 */
bool
pgactive_is_pgactive_activated_db(Oid dboid)
{
	pgactiveDatabaseCacheEntry *entry;

	/* won't know until we've forked/execed */
	Assert(IsUnderPostmaster);

	/* potentially need to access syscaches */
	Assert(IsTransactionState());

	entry = pgactive_dbcache_lookup(dboid, false);

	return entry->pgactive_activated;
}
