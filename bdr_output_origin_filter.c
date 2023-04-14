/*-------------------------------------------------------------------------
 *
 * bdr_output_originfilter.c
 * 		pglogical plugin for multi-master replication
 *
 * Copyright (c) 2017, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		  bdr_output_originfilter.c
 *
 * Replication origin filtering in bdr output plugin
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/xact.h"

#include "catalog/namespace.h"

#include "nodes/parsenodes.h"

#include "replication/logical.h"
#include "replication/origin.h"

#include "utils/inval.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/syscache.h"

#include "bdr_output_origin_filter.h"

typedef struct BdrOriginCacheEntry
{
	RepOriginId origin;
	/* Entry is valid and not due to be purged */
	bool is_valid;
	/* If this is a peer BDR node (and not us) */
	bool is_bdr_peer;
} BdrOriginCacheEntry;

static HTAB *BdrOriginCache = NULL;
static int InvalidBdrOriginCacheCnt = 0;

static BdrOriginCacheEntry *bdrorigincache_get_node(RepOriginId origin);
static void bdr_lookup_origin(RepOriginId origin_id, BdrOriginCacheEntry *entry);

/*
 * Invalidation of the origin cache for when an origin is dropped or
 * re-created.
 */
static void
bdrorigincache_invalidation_cb(Datum arg, int cacheid, uint32 origin_id)
 {
	struct BdrOriginCacheEntry *hentry;
	RepOriginId origin = (RepOriginId)origin_id;

	Assert (BdrOriginCache != NULL);
	Assert (cacheid == REPLORIGIDENT);

	/*
	 * We can't immediately delete entries as invalidations can
	 * arrive while we're in the middle of using one. So we must
	 * mark it invalid and purge it later.
	 */
	hentry = (struct BdrOriginCacheEntry *)
		hash_search(BdrOriginCache, &origin, HASH_FIND, NULL);

	if (hentry != NULL)
	{
		hentry->is_valid = false;
		InvalidBdrOriginCacheCnt++;
	}
}

/*
 * Create a cache mapping replication origins to bdr node IDs
 * and node group IDs, so we can make fast decisions about
 * whether or not to forward a given xact.
 */
void
bdrorigincache_init(MemoryContext decoding_context)
{
	InvalidBdrOriginCacheCnt = 0;

	if (BdrOriginCache == NULL)
	{
		HASHCTL	ctl;

		MemSet(&ctl, 0, sizeof(ctl));
		ctl.keysize = sizeof(Oid);
		ctl.entrysize = sizeof(struct BdrOriginCacheEntry);

		/*
		 * Allocate cache under TopMemoryContext. hash_create() does that for
		 * us by default.
		 */
		BdrOriginCache = hash_create("bdr reporigin to node cache",
								   128,
								   &ctl,
								   HASH_ELEM | HASH_BLOBS);

		Assert(BdrOriginCache != NULL);

		/* Assert that hash table has been created under TopMemoryContext. */
		Assert(MemoryContextGetParent(GetMemoryChunkContext(BdrOriginCache)) ==
			   TopMemoryContext);

		CacheRegisterSyscacheCallback(REPLORIGIDENT,
			bdrorigincache_invalidation_cb, (Datum)0);
	}
}

/*
 * Look up an entry, creating it if not found.
 */
static BdrOriginCacheEntry *
bdrorigincache_get_node(RepOriginId origin)
{
	struct BdrOriginCacheEntry *hentry;
	bool found;

	/* Find cached function info, creating if not found */
	hentry = (struct BdrOriginCacheEntry*) hash_search(BdrOriginCache,
										 &origin,
										 HASH_ENTER, &found);

	if (!found || !hentry->is_valid)
		bdr_lookup_origin(origin, hentry);

	Assert(hentry != NULL);
	Assert(hentry->is_valid);

	return hentry;
}


/*
 * Flush the bdr origin cache at the end of a decoding session.
 */
void
bdrorigincache_destroy(void)
{
	HASH_SEQ_STATUS status;
	struct BdrOriginCacheEntry *hentry;

	if (BdrOriginCache != NULL)
	{
		hash_seq_init(&status, BdrOriginCache);

		while ((hentry = (struct BdrOriginCacheEntry*) hash_seq_search(&status)) != NULL)
		{
			if (hash_search(BdrOriginCache,
							(void *) &hentry->origin,
							HASH_REMOVE, NULL) == NULL)
				elog(ERROR, "hash table corrupted");
		}
	}
}

/*
 * We need to filter out transactions from the same node group while retaining
 * transactions forwarded from other peers (pglogical subscriptions, etc) for
 * replication.
 *
 * This leaks memory if called within an existing transaction, but only gets
 * called once per local replication origin entry, so it's probably not worth
 * having a memory context for it.
 */
static void
bdr_lookup_origin(RepOriginId origin_id, BdrOriginCacheEntry *entry)
{
	bool txn_started = false;
	char *origin_name;

	Assert(origin_id != InvalidRepOriginId);
	Assert(origin_id != DoNotReplicateId);

	if (!IsTransactionState())
	{
		StartTransactionCommand();
		txn_started = true;
	}

	if (replorigin_by_oid(origin_id, true, &origin_name))
		entry->is_bdr_peer = strncmp(origin_name, "bdr_", 4) == 0;
	else
		entry->is_bdr_peer = false;

	if (txn_started)
		CommitTransactionCommand();

	entry->is_valid = true;
}

bool
bdr_origin_in_same_nodegroup(RepOriginId origin_id)
{
	BdrOriginCacheEntry *entry;
	Assert(origin_id != InvalidRepOriginId);
	Assert(origin_id != DoNotReplicateId);
	entry = bdrorigincache_get_node(origin_id);
	Assert(entry->is_valid);
	return entry->is_bdr_peer;
}
