/* -------------------------------------------------------------------------
 *
 * bdr_nodecache.c
 *		shmem cache for local node entry in bdr_nodes, holds one entry per
 *		each local bdr database
 *
 * Copyright (c) 2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		bdr_nodecache.c
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include "bdr.h"
#include "bdr_locks.h"

#include "access/heapam.h"
#include "access/xact.h"
#include "catalog/namespace.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "utils/catcache.h"
#include "utils/inval.h"
#include "utils/memutils.h"
#include "utils/lsyscache.h"

/*
 * Caches for our name and (if we're an apply worker or walsender) our peer
 * node's name, to bypass the usual nodecache machinery and provide quick, safe
 * access when not in a txn.
 */
static const char *my_node_name = NULL;

/*
 * To make sure cached name calls are for the correct node id and don't produce
 * confusing results, check node id each call.
 */
static BDRNodeId remote_node_id;
static const char *remote_node_name = NULL;

static HTAB *BDRNodeCacheHash = NULL;

/*
 * Because PostgreSQL does not have enought relation lookup functions.
 */
static Oid
bdr_get_relname_relid(const char *nspname, const char *relname)
{
	Oid			nspid;
	Oid			relid;

	nspid = get_namespace_oid(nspname, false);
	relid = get_relname_relid(relname, nspid);

	if (!relid)
		elog(ERROR, "cache lookup failed for relation %s.%s",
			 nspname, relname);

	return relid;
}

/*
 * Send cache invalidation singal to all backends.
 */
void
bdr_nodecache_invalidate(void)
{
	CacheInvalidateRelcacheByRelid(bdr_get_relname_relid("bdr", "bdr_nodes"));
}

/*
 * Invalidate the session local cache.
 */
static void
bdr_nodecache_invalidate_callback(Datum arg, Oid relid)
{
	if (BDRNodeCacheHash == NULL)
		return;

	if (relid == InvalidOid ||
		relid == BdrNodesRelid)
	{
		HASH_SEQ_STATUS status;
		BDRNodeInfo *entry;

		hash_seq_init(&status, BDRNodeCacheHash);

		/* We currently always invalidate everything */
		while ((entry = (BDRNodeInfo *) hash_seq_search(&status)) != NULL)
		{
			entry->valid = false;
		}
	}
}

static void
bdr_nodecache_initialize()
{
	HASHCTL		ctl;

	/* Make sure we've initialized CacheMemoryContext. */
	if (CacheMemoryContext == NULL)
		CreateCacheMemoryContext();

	/* Initialize the hash table. */
	MemSet(&ctl, 0, sizeof(ctl));
	ctl.keysize = sizeof(BDRNodeId);
	ctl.entrysize = sizeof(BDRNodeInfo);
	ctl.hash = tag_hash;
	ctl.hcxt = CacheMemoryContext;

	BDRNodeCacheHash = hash_create("BDR node cache", 128, &ctl,
								   HASH_ELEM | HASH_FUNCTION | HASH_CONTEXT);

	/*
	 * Watch for invalidation events. XXX: This breaks if the table is dropped
	 * and recreated, during the lifetime of this backend.
	 */
	BdrNodesRelid = bdr_get_relname_relid("bdr", "bdr_nodes");
	CacheRegisterRelcacheCallback(bdr_nodecache_invalidate_callback,
								  (Datum) 0);
}

static BDRNodeInfo *
bdr_nodecache_lookup(const BDRNodeId * const nodeid,
					 bool missing_ok,
					 bool only_cache_lookup)
{
	BDRNodeInfo *entry,
			   *nodeinfo;
	bool		found;
	MemoryContext saved_ctx;

	/*
	 * We potentially need to access syscaches, but it's not safe to start a
	 * txn here, since we might clobber memory contexts, resource owners, etc
	 * set up elsewhere.
	 */
	Assert(IsTransactionState());

	if (BDRNodeCacheHash == NULL)
		bdr_nodecache_initialize();

	/*
	 * HASH_ENTER returns the existing entry if present or creates a new one.
	 */
	entry = hash_search(BDRNodeCacheHash, (void *) nodeid,
						HASH_ENTER, &found);

	if (found)
	{
		if (entry->valid)
		{
			Assert(IsTransactionState());
			return entry;
		}
		else
		{
			/*
			 * Entry exists but is invalid. Release any memory it holds in
			 * CacheMemoryContext before we zero the entry for re-use.
			 */
			if (entry->local_dsn != NULL)
				pfree(entry->local_dsn);
			if (entry->init_from_dsn != NULL)
				pfree(entry->init_from_dsn);
			if (entry->name != NULL)
				pfree(entry->name);
		}
	}

	/* zero out data part of the entry */
	memset(((char *) entry) + offsetof(BDRNodeInfo, valid),
		   0,
		   sizeof(BDRNodeInfo) - offsetof(BDRNodeInfo, valid));

	/*
	 * If asked to look up only in the cache, do not go further to get the
	 * info from the table upon cache miss.
	 */
	if (only_cache_lookup)
		return NULL;

	saved_ctx = MemoryContextSwitchTo(TopMemoryContext);
	nodeinfo = bdr_nodes_get_local_info(nodeid);
	MemoryContextSwitchTo(saved_ctx);

	if (nodeinfo == NULL)
	{
		Assert(IsTransactionState());
		if (!missing_ok)
			elog(ERROR, "could not find node " BDR_NODEID_FORMAT,
				 BDR_NODEID_FORMAT_ARGS(*nodeid));
		else
			return NULL;
	}

	entry->status = nodeinfo->status;
	if (nodeinfo->local_dsn)
		entry->local_dsn = MemoryContextStrdup(CacheMemoryContext,
											   nodeinfo->local_dsn);
	if (nodeinfo->init_from_dsn)
		entry->init_from_dsn = MemoryContextStrdup(CacheMemoryContext,
												   nodeinfo->init_from_dsn);
	entry->read_only = nodeinfo->read_only;

	if (nodeinfo->name)
		entry->name = MemoryContextStrdup(CacheMemoryContext,
										  nodeinfo->name);

	entry->seq_id = nodeinfo->seq_id;

	entry->valid = true;

	bdr_bdr_node_free(nodeinfo);

	Assert(IsTransactionState());
	return entry;
}

/*
 * Look up our node name from the nodecache.
 *
 * A txn must be active.
 *
 * If you need to call this from a context where you're not sure there'll be an
 * open txn, use bdr_local_node_name_cached().
 */
const char *
bdr_local_node_name(bool only_cache_lookup)
{
	BDRNodeId	nodeid;
	BDRNodeInfo *node;

	bdr_make_my_nodeid(&nodeid);
	node = bdr_nodecache_lookup(&nodeid, true, only_cache_lookup);

	if (node == NULL)
		return "(unknown)";

	return node->name;
}

bool
bdr_local_node_read_only(void)
{
	BDRNodeId	nodeid;
	BDRNodeInfo *node;

	bdr_make_my_nodeid(&nodeid);
	node = bdr_nodecache_lookup(&nodeid, true, false);

	if (node == NULL)
		return false;

	return node->read_only;
}

char
bdr_local_node_status(void)
{
	BDRNodeId	nodeid;
	BDRNodeInfo *node;

	bdr_make_my_nodeid(&nodeid);
	node = bdr_nodecache_lookup(&nodeid, true, false);

	if (node == NULL)
		return '\0';

	return node->status;
}

/*
 * Get 16-bit node sequence ID, or
 * -1 if no node or no sequence assigned.
 */
int32
bdr_local_node_seq_id(void)
{
	BDRNodeId	nodeid;
	BDRNodeInfo *node;

	bdr_make_my_nodeid(&nodeid);
	node = bdr_nodecache_lookup(&nodeid, true, false);

	if (node == NULL)
		return -1;

	return node->seq_id;
}

/*
 * Look up the specified node in the nodecache and return a guaranteed
 * non-null pointer. If no node name found, use (none) or if missing_ok = f,
 * abort.
 *
 * Return value is owned by the cache and must not be free'd.
 */
const char *
bdr_nodeid_name(const BDRNodeId * const node,
				bool missing_ok,
				bool only_cache_lookup)
{
	BDRNodeInfo *nodeinfo;
	char	   *node_name;

	nodeinfo = bdr_nodecache_lookup(node, missing_ok, only_cache_lookup);
	node_name = (nodeinfo == NULL || nodeinfo->name == NULL ?
				 "(unknown)" : nodeinfo->name);

	return node_name;
}

/*
 * The full nodecache requires a transaction to be open. Since we
 * often want to output our own node name and that of our peer node,
 * we cache them at worker startup.
 *
 * This cache doesn't get invalidated if node names change, but since our
 * application_name doesn't either, users should expect to have to restart
 * workers anyway. The node name doesn't act as a key to anything so
 * not invalidating it on change isn't a big deal; about all it can do
 * is affect synchronous_standby_names .
 *
 * Must be called after background worker setup so BDRThisTimeLineID
 * is initialized, while there's an open txn.
 *
 * TODO: If we made the nodecache eager, so it reloaded fully on
 * invalidations, we could get rid of this hack.
 */
void
bdr_setup_my_cached_node_names()
{
	BDRNodeId	myid;

	Assert(IsTransactionState());
	bdr_make_my_nodeid(&myid);

	my_node_name = MemoryContextStrdup(CacheMemoryContext,
									   bdr_nodeid_name(&myid, false, false));
}

void
bdr_setup_cached_remote_name(const BDRNodeId * const remote_nodeid)
{
	Assert(IsTransactionState());

	remote_node_name = MemoryContextStrdup(CacheMemoryContext,
										   bdr_nodeid_name(remote_nodeid, false, false));

	bdr_nodeid_cpy(&remote_node_id, remote_nodeid);
}

/*
 * A deadlock can occur when look up for a node name leads to reading from
 * bdr.bdr_nodes table (node cache miss) while holding bdr_locks shared memory
 * lock. The deadlock was observed in one of the TAP test
 * 042_concurrency_physical.pl, and it looked like the following:
 *
 * 1. A per-db worker while holding bdr_locks shared memory lock from
 * bdr_locks_node_detached() tried to read a node name (via
 * BDR_NODEID_FORMAT_WITHNAME_ARGS) to print in log message. This node name
 * read from node cache lead to reading from bdr.bdr_nodes table in
 * bdr_nodes_get_local_info() which requires an exclusive lock on the table.
 *
 * 2. A backend process related to the connection opened by
 * bdr_nodes_set_remote_status_ready() was trying to commit a transaction while
 * holding the exclusive lock on bdr.bdr_nodes table. This led to
 * bdr_lock_holder_xact_callback() requiring bdr_locks shared memory lock.
 *
 * In short, the per-db worker held bdr_locks shared memory lock, and waiting
 * to acquire exclusive lock on bdr.bdr_nodes table. The backend process held
 * exclusive lock on bdr.bdr_nodes table, and waiting to acquire bdr_locks
 * shared memory lock. This led to deadlock.
 *
 * A simple fix here is to disallow reading node name from bdr.bdr_nodes table
 * when node cache miss happens while holding bdr_locks shared memory lock. In
 * this case, "(unknown)" is returned as node name. This fix is simple because
 * the node name read functions bdr_get_my_cached_node_name() and
 * bdr_get_my_cached_remote_name() are mostly called to print node names in log
 * messages. What may happen is that the log messages will have a valid node id
 * with node name as "(unknown)", the valid node id will help distiguish the
 * log messages for every node. See the code around only_cache_lookup variable
 * in below functions.
 */
const char *
bdr_get_my_cached_node_name()
{
	if (my_node_name != NULL)
		return my_node_name;
	else if (IsTransactionState())
	{
		bool		only_cache_lookup;

		only_cache_lookup = IsBDRLocksShmemLockHeldByMe();

		/* We might get called from a user backend too, within a function */
		return bdr_local_node_name(only_cache_lookup);
	}
	else
		return "(unknown)";

}

const char *
bdr_get_my_cached_remote_name(const BDRNodeId * const remote_nodeid)
{
	if (remote_node_name != NULL &&
		bdr_nodeid_eq(&remote_node_id, remote_nodeid))
		return remote_node_name;
	else if (IsTransactionState())
	{
		bool		only_cache_lookup;

		only_cache_lookup = IsBDRLocksShmemLockHeldByMe();

		/* We might get called from a user backend */
		return bdr_nodeid_name(remote_nodeid, true, only_cache_lookup);
	}
	else
		return "(unknown)";
}
