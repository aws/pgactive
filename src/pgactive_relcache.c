/* -------------------------------------------------------------------------
 *
 * pgactive_relcache.c
 *		pgactive relation caching
 *
 * Caching relation specific information
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_relcache.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pgactive.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/xact.h"

#include "catalog/pg_class.h"

#include "commands/seclabel.h"

#include "utils/builtins.h"
#include "utils/catcache.h"
#include "utils/fmgroids.h"
#include "utils/inval.h"
#include "utils/jsonb.h"
#include "utils/rel.h"

static HTAB *pgactiveRelcacheHash = NULL;

static void
pgactiveRelcacheHashInvalidateEntry(pgactiveRelation * entry)
{
	int			i;

	if (entry->conflict_handlers)
		pfree(entry->conflict_handlers);

	if (entry->num_replication_sets > 0)
	{
		for (i = 0; i < entry->num_replication_sets; i++)
			pfree(entry->replication_sets[i]);

		pfree(entry->replication_sets);
	}
}

void
pgactiveRelcacheHashInvalidateCallback(Datum arg, Oid relid)
{
	HASH_SEQ_STATUS status;
	pgactiveRelation *entry;

	/*
	 * We sometimes explicitly invalidate the entire pgactive relcache -
	 * independent of actual system caused invalidations. Without that this
	 * situation could not happen as the normall inval callback only gets
	 * registered after creating the hash.
	 */
	if (pgactiveRelcacheHash == NULL)
		return;

	/*
	 * If relid is InvalidOid, signalling a complete reset, we have to remove
	 * all entries, otherwise just invalidate the specific relation's entry.
	 */
	if (relid == InvalidOid)
	{
		hash_seq_init(&status, pgactiveRelcacheHash);

		while ((entry = (pgactiveRelation *) hash_seq_search(&status)) != NULL)
		{
			entry->valid = false;
		}
	}
	else
	{
		if ((entry = hash_search(pgactiveRelcacheHash, &relid,
								 HASH_FIND, NULL)) != NULL)
		{
			entry->valid = false;
		}
	}
}

static void
pgactive_relcache_initialize()
{
	HASHCTL		ctl;

	/* Make sure we've initialized CacheMemoryContext. */
	if (CacheMemoryContext == NULL)
		CreateCacheMemoryContext();

	/* Initialize the hash table. */
	MemSet(&ctl, 0, sizeof(ctl));
	ctl.keysize = sizeof(Oid);
	ctl.entrysize = sizeof(pgactiveRelation);
	ctl.hash = tag_hash;
	ctl.hcxt = CacheMemoryContext;

	pgactiveRelcacheHash = hash_create("pgactive relation cache", 128, &ctl,
									   HASH_ELEM | HASH_FUNCTION | HASH_CONTEXT);

	/* Watch for invalidation events. */
	CacheRegisterRelcacheCallback(pgactiveRelcacheHashInvalidateCallback,
								  (Datum) 0);
}

void
pgactive_validate_replication_set_name(const char *name,
									   bool allow_implicit)
{
	const char *cp;

	if (strlen(name) == 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_NAME),
				 errmsg("replication set name \"%s\" is too short",
						name)));
	}

	if (strlen(name) >= NAMEDATALEN)
	{
		ereport(ERROR,
				(errcode(ERRCODE_NAME_TOO_LONG),
				 errmsg("replication set name \"%s\" is too long",
						name)));
	}

	for (cp = name; *cp; cp++)
	{
		if (!((*cp >= 'a' && *cp <= 'z')
			  || (*cp >= '0' && *cp <= '9')
			  || (*cp == '_')
			  || (*cp == '-')))
		{
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_NAME),
					 errmsg("replication set name \"%s\" contains invalid character",
							name),
					 errhint("Replication set names may only contain letters, numbers, and the underscore character.")));
		}
	}

	if (!allow_implicit && (
							strcmp(name, "default") == 0 ||
							strcmp(name, "all") == 0
							))
	{
		ereport(ERROR,
				(errcode(ERRCODE_NAME_TOO_LONG),
				 errmsg("replication set name \"%s\" is reserved",
						name),
				 errhint("To reset a relation's replication sets to defaults, use pgactive.pgactive_set_table_replication_sets('relation_name', NULL).")));
	}
}

void
pgactive_parse_relation_options(const char *label, pgactiveRelation * rel)
{
	JsonbIterator *it;
	JsonbValue	v;
	int			r;
	bool		parsing_sets = false;
	int			level = 0;
	Jsonb	   *data = NULL;

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
			elog(ERROR, "only 'sets' allowed on root level");
		else if (level == 1 && r == WJB_KEY)
		{
			if (strncmp(v.val.string.val, "sets", v.val.string.len) != 0)
				elog(ERROR, "unexpected key: %s",
					 pnstrdup(v.val.string.val, v.val.string.len));
			parsing_sets = true;

			if (rel != NULL)
				rel->num_replication_sets = 0;
		}
		else if (r == WJB_BEGIN_ARRAY || r == WJB_BEGIN_OBJECT)
		{
			if (parsing_sets && rel != NULL)
			{
				rel->replication_sets =
					MemoryContextAlloc(CacheMemoryContext,
									   sizeof(char *) * it->nElems);
			}
			level++;
		}
		else if (r == WJB_END_ARRAY || r == WJB_END_OBJECT)
		{
			level--;
			parsing_sets = false;
		}
		else if (parsing_sets)
		{
			char	   *setname;
			MemoryContext oldcontext;

			if (r != WJB_ELEM)
				elog(ERROR, "unexpected element type %u", r);
			if (level != 2)
				elog(ERROR, "unexpected level for set %d", level);

			oldcontext = MemoryContextSwitchTo(CacheMemoryContext);

			setname = pnstrdup(v.val.string.val, v.val.string.len);
			pgactive_validate_replication_set_name(setname, false);

			if (rel != NULL)
			{
				rel->replication_sets[rel->num_replication_sets++] = setname;
			}
			else
				pfree(setname);

			MemoryContextSwitchTo(oldcontext);
		}
		else
			elog(ERROR, "unexpected content: %u at level %d", r, level);
	}

	if (rel != NULL && rel->num_replication_sets > 0)
	{
		qsort(rel->replication_sets, rel->num_replication_sets,
			  sizeof(char *), pg_qsort_strcmp);
	}

}

pgactiveRelation *
pgactive_table_open(Oid reloid, LOCKMODE lockmode)
{
	pgactiveRelation *entry;
	bool		found;
	Relation	rel;
	ObjectAddress object;
	const char *label;

	rel = table_open(reloid, lockmode);

	if (pgactiveRelcacheHash == NULL)
		pgactive_relcache_initialize();

	/*
	 * HASH_ENTER returns the existing entry if present or creates a new one.
	 */
	entry = hash_search(pgactiveRelcacheHash, (void *) &reloid,
						HASH_ENTER, &found);

	/* possibly a new relcache.c relcache entry */
	entry->rel = rel;

	if (found && entry->valid)
		return entry;
	else if (found)
		pgactiveRelcacheHashInvalidateEntry(entry);

	/* zero out data part of the entry */
	memset(((char *) entry) + offsetof(pgactiveRelation, conflict_handlers),
		   0,
		   sizeof(pgactiveRelation) - offsetof(pgactiveRelation, conflict_handlers));

	entry->reloid = reloid;
	entry->num_replication_sets = -1;

	object.classId = RelationRelationId;
	object.objectId = reloid;
	object.objectSubId = 0;

	label = GetSecurityLabel(&object, pgactive_SECLABEL_PROVIDER);
	pgactive_parse_relation_options(label, entry);

	entry->valid = true;

	return entry;
}

void
pgactive_table_close(pgactiveRelation * rel, LOCKMODE lockmode)
{
	table_close(rel->rel, lockmode);
	rel->rel = NULL;
}


static bool
relation_in_replication_set(pgactiveRelation * r, const char *setname)
{
	/* "all" set contains, surprise, all relations */
	if (strcmp(setname, "all") == 0)
		return true;

	/*
	 * "default" set contains all relations without a replication set
	 * configuration
	 */
	if (strcmp(setname, "default") == 0 && r->num_replication_sets == -1)
		return true;

	/* if no set is configured, it's not in there */
	if (r->num_replication_sets <= 0)
		return false;

	/* look whether the relation is the named set */
	if (bsearch(&setname,
				r->replication_sets, r->num_replication_sets, sizeof(char *),
				pg_qsort_strcmp))
		return true;

	return false;
}

static HeapTuple
replset_lookup(Relation rel, const char *cname)
{
	ScanKey		key;
	NameData	name;
	SysScanDesc scan;
	HeapTuple	tuple = NULL;

	namestrcpy(&name, cname);

	key = (ScanKey) palloc(sizeof(ScanKeyData) * 1);

	ScanKeyInit(&key[0],
				1,
				BTEqualStrategyNumber, F_NAMEEQ,
				NameGetDatum(&name)
		);

	/* FIXME: should use index */
	scan = systable_beginscan(rel, 0, true, NULL, 1, key);

	while ((tuple = systable_getnext(scan)) != NULL)
	{
		tuple = heap_copytuple(tuple);
		break;
	}

	systable_endscan(scan);
	return tuple;
}

/*
 * Compute whether modifications to this relation should be replicated or not
 * and cache the result in the relation descriptor.
 *
 * NB: This can only sensibly used from inside logical decoding as we require
 * a constant set of 'to be replicated' sets to be passed in - which happens
 * to be what we need for logical decoding. As there really isn't another need
 * for this functionality so far...
 * Another reason restricting this to backends in decoding is that we
 * currently can't invalidate the cache correctly otherwise.
 */
void
pgactive_heap_compute_replication_settings(pgactiveRelation * r,
										   int conf_num_replication_sets,
										   char **conf_replication_sets)
{
	int			i;

	Assert(MyReplicationSlot);	/* in decoding */

	Assert(!r->computed_repl_valid);

	/* Implicit "replicate everything" configuration */
	if (conf_num_replication_sets == -1)
	{
		r->computed_repl_insert = true;
		r->computed_repl_update = true;
		r->computed_repl_delete = true;

		r->computed_repl_valid = true;
		return;
	}

	/*
	 * Build the union of all replicated actions across all configured
	 * replication sets.
	 */
	for (i = 0; i < conf_num_replication_sets; i++)
	{
		Relation	repl_sets;
		HeapTuple	tuple;
		const char *setname;

		setname = conf_replication_sets[i];

		if (!relation_in_replication_set(r, setname))
			continue;

		repl_sets = table_open(pgactiveReplicationSetConfigRelid, AccessShareLock);
		tuple = replset_lookup(repl_sets, setname);

		if (tuple != NULL)
		{
			bool		isnull;
			TupleDesc	desc = RelationGetDescr(repl_sets);

			if (DatumGetBool(fastgetattr(tuple, 2, desc, &isnull)))
				r->computed_repl_insert = true;

			if (DatumGetBool(fastgetattr(tuple, 3, desc, &isnull)))
				r->computed_repl_update = true;

			if (DatumGetBool(fastgetattr(tuple, 4, desc, &isnull)))
				r->computed_repl_delete = true;

			pfree(tuple);
		}
		else
		{
			r->computed_repl_insert = true;
			r->computed_repl_update = true;
			r->computed_repl_delete = true;
		}

		table_close(repl_sets, AccessShareLock);

		/* no need to look any further, we replicate everything */
		if (r->computed_repl_insert &&
			r->computed_repl_update &&
			r->computed_repl_delete)
			break;
	}

	r->computed_repl_valid = true;
}
