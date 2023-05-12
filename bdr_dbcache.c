/* -------------------------------------------------------------------------
 *
 * bdr_dbcache.c
 *		coherent, per database, cache for BDR.
 *
 * Copyright (c) 2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		bdr_dbcache.c
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include "bdr.h"
#include "bdr_label.h"

#include "miscadmin.h"

#include "access/xact.h"

#include "catalog/pg_database.h"

#include "commands/seclabel.h"

#include "utils/catcache.h"
#include "utils/inval.h"
#if PG_VERSION_NUM >= 130000
#include "common/jsonapi.h"
#else
#include "utils/jsonapi.h"
#endif
#include "utils/json.h"
#include "utils/jsonb.h"
#include "utils/syscache.h"
#include "utils/builtins.h"

/* Cache entry. */
typedef struct BDRDatabaseCacheEntry
{
	Oid			oid;			/* cache key, needs to be first */
	const char *dbname;
	bool		valid;
	bool		bdr_activated;
}			BDRDatabaseCacheEntry;

static HTAB *BDRDatabaseCacheHash = NULL;

static BDRDatabaseCacheEntry * bdr_dbcache_lookup(Oid dboid, bool missing_ok);

static void
bdr_dbcache_invalidate_entry(Datum arg, int cacheid, uint32 hashvalue)
{
	HASH_SEQ_STATUS status;
	BDRDatabaseCacheEntry *hentry;

	Assert(BDRDatabaseCacheHash != NULL);

	/*
	 * Currently we just reset all entries; it's unlikely anything more
	 * complicated is worthwile.
	 */
	hash_seq_init(&status, BDRDatabaseCacheHash);

	while ((hentry = (BDRDatabaseCacheEntry *) hash_seq_search(&status)) != NULL)
	{
		hentry->valid = false;
	}

}

static void
bdr_dbcache_initialize()
{
	HASHCTL		ctl;

	/* Make sure we've initialized CacheMemoryContext. */
	if (CacheMemoryContext == NULL)
		CreateCacheMemoryContext();

	/* Initialize the hash table. */
	MemSet(&ctl, 0, sizeof(ctl));
	ctl.keysize = sizeof(Oid);
	ctl.entrysize = sizeof(BDRDatabaseCacheEntry);
	ctl.hash = tag_hash;
	ctl.hcxt = CacheMemoryContext;

	BDRDatabaseCacheHash = hash_create("BDR database cache", 128, &ctl,
									   HASH_ELEM | HASH_FUNCTION | HASH_CONTEXT);

	/* Watch for invalidation events. */
	CacheRegisterSyscacheCallback(DATABASEOID, bdr_dbcache_invalidate_entry, (Datum) 0);
}

void
bdr_parse_database_options(const char *label, bool *is_active)
{
	JsonbIterator *it;
	JsonbValue	v;
	int			r;
	int			level = 0;
	Jsonb	   *data = NULL;
	bool		parsing_bdr = false;

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
			elog(ERROR, "only 'bdr' allowed on root level");
		else if (level == 0 && r == WJB_BEGIN_OBJECT)
		{
			level++;
		}
		else if (level == 1 && r == WJB_KEY)
		{
			if (strncmp(v.val.string.val, "bdr", v.val.string.len) != 0)
				elog(ERROR, "unexpected key: %s",
					 pnstrdup(v.val.string.val, v.val.string.len));
			parsing_bdr = true;
		}
		else if (level == 1 && r == WJB_VALUE)
		{
			if (!parsing_bdr)
				elog(ERROR, "in wrong state when parsing key");

			if (v.type != jbvBool)
				elog(ERROR, "unexpected type for key 'bdr': %u", v.type);

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
			parsing_bdr = false;
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
static BDRDatabaseCacheEntry *
bdr_dbcache_lookup(Oid dboid, bool missing_ok)
{
	BDRDatabaseCacheEntry *entry;
	bool		found;
	ObjectAddress object;
	HeapTuple	dbtuple;
	const char *label;

	if (BDRDatabaseCacheHash == NULL)
		bdr_dbcache_initialize();

	/*
	 * HASH_ENTER returns the existing entry if present or creates a new one.
	 */
	entry = hash_search(BDRDatabaseCacheHash, (void *) &dboid,
						HASH_ENTER, &found);

	if (found && entry->valid)
		return entry;

	/* zero out data part of the entry */
	memset(((char *) entry) + offsetof(BDRDatabaseCacheEntry, dbname),
		   0,
		   sizeof(BDRDatabaseCacheEntry) - offsetof(BDRDatabaseCacheEntry, dbname));


	/* lookup db entry and error out when the db doesn't exist && !missin_ok */
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

	label = GetSecurityLabel(&object, "bdr");
	bdr_parse_database_options(label, &entry->bdr_activated);

	entry->valid = true;

	return entry;
}


/*
 * Is the database configured for bdr?
 */
bool
bdr_is_bdr_activated_db(Oid dboid)
{
	BDRDatabaseCacheEntry *entry;

	/* won't know until we've forked/execed */
	Assert(IsUnderPostmaster);

	/* potentially need to access syscaches */
	Assert(IsTransactionState());

	entry = bdr_dbcache_lookup(dboid, false);

	return entry->bdr_activated;
}
