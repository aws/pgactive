/* -------------------------------------------------------------------------
 *
 * pgactive_nodecache.c
 *		shmem cache for local node entry in pgactive_nodes, holds one entry per
 *		each local pgactive database
 *
 * Copyright (c) 2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_nodecache.c
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include "pgactive.h"

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
static pgactiveNodeId remote_node_id;
static const char *remote_node_name = NULL;

static HTAB *pgactiveNodeCacheHash = NULL;

/*
 * Because PostgreSQL does not have enought relation lookup functions.
 */
static Oid
pgactive_get_relname_relid(const char *nspname, const char *relname)
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
pgactive_nodecache_invalidate(void)
{
	CacheInvalidateRelcacheByRelid(pgactive_get_relname_relid("pgactive", "pgactive_nodes"));
}

/*
 * Invalidate the session local cache.
 */
static void
pgactive_nodecache_invalidate_callback(Datum arg, Oid relid)
{
	if (pgactiveNodeCacheHash == NULL)
		return;

	if (relid == InvalidOid ||
		relid == pgactiveNodesRelid)
	{
		HASH_SEQ_STATUS status;
		pgactiveNodeInfo *entry;

		hash_seq_init(&status, pgactiveNodeCacheHash);

		/* We currently always invalidate everything */
		while ((entry = (pgactiveNodeInfo *) hash_seq_search(&status)) != NULL)
		{
			entry->valid = false;
		}
	}
}

static void
pgactive_nodecache_initialize()
{
	HASHCTL		ctl;

	/* Make sure we've initialized CacheMemoryContext. */
	if (CacheMemoryContext == NULL)
		CreateCacheMemoryContext();

	/* Initialize the hash table. */
	MemSet(&ctl, 0, sizeof(ctl));
	ctl.keysize = sizeof(pgactiveNodeId);
	ctl.entrysize = sizeof(pgactiveNodeInfo);
	ctl.hash = tag_hash;
	ctl.hcxt = CacheMemoryContext;

	pgactiveNodeCacheHash = hash_create("pgactive node cache", 128, &ctl,
										HASH_ELEM | HASH_FUNCTION | HASH_CONTEXT);

	/*
	 * Watch for invalidation events. XXX: This breaks if the table is dropped
	 * and recreated, during the lifetime of this backend.
	 */
	pgactiveNodesRelid = pgactive_get_relname_relid("pgactive", "pgactive_nodes");
	CacheRegisterRelcacheCallback(pgactive_nodecache_invalidate_callback,
								  (Datum) 0);
}

static pgactiveNodeInfo *
pgactive_nodecache_lookup(const pgactiveNodeId * const nodeid,
						  bool missing_ok)
{
	pgactiveNodeInfo *entry,
			   *nodeinfo;
	bool		found;
	MemoryContext saved_ctx;

	/*
	 * We potentially need to access syscaches, but it's not safe to start a
	 * txn here, since we might clobber memory contexts, resource owners, etc
	 * set up elsewhere.
	 */
	Assert(IsTransactionState());

	if (pgactiveNodeCacheHash == NULL)
		pgactive_nodecache_initialize();

	/*
	 * HASH_ENTER returns the existing entry if present or creates a new one.
	 */
	entry = hash_search(pgactiveNodeCacheHash, (void *) nodeid,
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
	memset(((char *) entry) + offsetof(pgactiveNodeInfo, valid),
		   0,
		   sizeof(pgactiveNodeInfo) - offsetof(pgactiveNodeInfo, valid));

	saved_ctx = MemoryContextSwitchTo(TopMemoryContext);
	nodeinfo = pgactive_nodes_get_local_info(nodeid);
	MemoryContextSwitchTo(saved_ctx);

	if (nodeinfo == NULL)
	{
		Assert(IsTransactionState());
		if (!missing_ok)
			elog(ERROR, "could not find node " pgactive_NODEID_FORMAT,
				 pgactive_NODEID_FORMAT_ARGS(*nodeid));
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

	pgactive_pgactive_node_free(nodeinfo);

	Assert(IsTransactionState());
	return entry;
}

/*
 * Look up our node name from the nodecache.
 *
 * A txn must be active.
 *
 * If you need to call this from a context where you're not sure there'll be an
 * open txn, use pgactive_local_node_name_cached().
 */
const char *
pgactive_local_node_name(void)
{
	pgactiveNodeId nodeid;
	pgactiveNodeInfo *node;

	pgactive_make_my_nodeid(&nodeid);
	node = pgactive_nodecache_lookup(&nodeid, true);

	if (node == NULL)
		return "(unknown)";

	return node->name;
}

bool
pgactive_local_node_read_only(void)
{
	pgactiveNodeId nodeid;
	pgactiveNodeInfo *node;

	pgactive_make_my_nodeid(&nodeid);
	node = pgactive_nodecache_lookup(&nodeid, true);

	if (node == NULL)
		return false;

	return node->read_only;
}

char
pgactive_local_node_status(void)
{
	pgactiveNodeId nodeid;
	pgactiveNodeInfo *node;

	pgactive_make_my_nodeid(&nodeid);
	node = pgactive_nodecache_lookup(&nodeid, true);

	if (node == NULL)
		return '\0';

	return node->status;
}

/*
 * Get 16-bit node sequence ID, or
 * -1 if no node or no sequence assigned.
 */
int32
pgactive_local_node_seq_id(void)
{
	pgactiveNodeId nodeid;
	pgactiveNodeInfo *node;

	pgactive_make_my_nodeid(&nodeid);
	node = pgactive_nodecache_lookup(&nodeid, true);

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
pgactive_nodeid_name(const pgactiveNodeId * const node,
					 bool missing_ok)
{
	pgactiveNodeInfo *nodeinfo;
	char	   *node_name;

	nodeinfo = pgactive_nodecache_lookup(node, missing_ok);
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
 * Must be called after background worker setup so pgactiveThisTimeLineID
 * is initialized, while there's an open txn.
 *
 * TODO: If we made the nodecache eager, so it reloaded fully on
 * invalidations, we could get rid of this hack.
 */
void
pgactive_setup_my_cached_node_names()
{
	pgactiveNodeId myid;

	Assert(IsTransactionState());
	pgactive_make_my_nodeid(&myid);

	my_node_name = MemoryContextStrdup(CacheMemoryContext,
									   pgactive_nodeid_name(&myid, false));
}

void
pgactive_setup_cached_remote_name(const pgactiveNodeId * const remote_nodeid)
{
	Assert(IsTransactionState());

	remote_node_name = MemoryContextStrdup(CacheMemoryContext,
										   pgactive_nodeid_name(remote_nodeid, false));

	pgactive_nodeid_cpy(&remote_node_id, remote_nodeid);
}

const char *
pgactive_get_my_cached_node_name()
{
	if (my_node_name != NULL)
		return my_node_name;
	else if (IsTransactionState())
	{
		/* We might get called from a user backend too, within a function */
		return pgactive_local_node_name();
	}
	else
		return "(unknown)";

}

const char *
pgactive_get_my_cached_remote_name(const pgactiveNodeId * const remote_nodeid)
{
	if (remote_node_name != NULL &&
		pgactive_nodeid_eq(&remote_node_id, remote_nodeid))
		return remote_node_name;
	else if (IsTransactionState())
	{
		/* We might get called from a user backend */
		return pgactive_nodeid_name(remote_nodeid, true);
	}
	else
		return "(unknown)";
}
