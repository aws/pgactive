/*-------------------------------------------------------------------------
 *
 * pgactive_output.c
 *		  pgactive output plugin
 *
 * Copyright (c) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		  pgactive_output.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "pgactive.h"
#include "pgactive_internal.h"
#include "miscadmin.h"

#include "access/sysattr.h"
#if PG_VERSION_NUM >= 130000
#include "access/detoast.h"
#else
#include "access/tuptoaster.h"
#endif
#include "access/xact.h"

#include "catalog/catversion.h"
#include "catalog/index.h"

#include "catalog/namespace.h"
#include "catalog/pg_class.h"
#include "catalog/pg_database.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_type.h"

#include "commands/dbcommands.h"

#include "executor/spi.h"

#include "libpq/pqformat.h"

#include "mb/pg_wchar.h"

#include "nodes/parsenodes.h"

#include "replication/logical.h"
#include "replication/output_plugin.h"
#include "replication/origin.h"
#include "replication/slot.h"
#include "replication/walsender_private.h"

#include "storage/fd.h"
#include "storage/proc.h"

#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "utils/timestamp.h"
#include "utils/typcache.h"
#include "utils/varlena.h"

extern void _PG_output_plugin_init(OutputPluginCallbacks *cb);

typedef struct
{
	MemoryContext context;

	pgactiveNodeId	remote_node;

	bool		allow_binary_protocol;
	bool		allow_sendrecv_protocol;
	bool		int_datetime_mismatch;
	bool		forward_changesets;

	uint32		client_pg_version;
	uint32		client_pg_catversion;
	uint32		client_pgactive_version;
	char	   *client_pgactive_variant;
	uint32		client_min_pgactive_version;
	size_t		client_sizeof_int;
	size_t		client_sizeof_long;
	size_t		client_sizeof_datum;
	size_t		client_maxalign;
	bool		client_bigendian;
	bool		client_float4_byval;
	bool		client_float8_byval;
	bool		client_int_datetime;
	char	   *client_db_encoding;
	Oid			pgactive_schema_oid;
	Oid			pgactive_conflict_handlers_reloid;
	Oid			pgactive_locks_reloid;
	Oid			pgactive_conflict_history_reloid;

	int			num_replication_sets;
	char	  **replication_sets;
}			pgactiveOutputData;

static pgactiveWalsenderWorker * pgactive_walsender_worker = NULL;

/* These must be available to pg_dlsym() */
static void pg_decode_startup(LogicalDecodingContext *ctx, OutputPluginOptions *opt,
							  bool is_init);
static void pg_decode_shutdown(LogicalDecodingContext *ctx);
static void pg_decode_begin_txn(LogicalDecodingContext *ctx,
								ReorderBufferTXN *txn);
static void pg_decode_commit_txn(LogicalDecodingContext *ctx,
								 ReorderBufferTXN *txn, XLogRecPtr commit_lsn);
static void pg_decode_change(LogicalDecodingContext *ctx,
							 ReorderBufferTXN *txn, Relation rel,
							 ReorderBufferChange *change);

static void pg_decode_message(LogicalDecodingContext *ctx,
							  ReorderBufferTXN *txn,
							  XLogRecPtr message_lsn,
							  bool transactional,
							  const char *prefix,
							  Size sz,
							  const char *message);

/* private prototypes */
static void write_rel(StringInfo out, Relation rel);
static void write_tuple(pgactiveOutputData * data, StringInfo out, Relation rel,
						HeapTuple tuple);

static void pglReorderBufferCleanSerializedTXNs(const char *slotname);

/* specify output plugin callbacks */
void
_PG_output_plugin_init(OutputPluginCallbacks *cb)
{
	AssertVariableIsOfType(&_PG_output_plugin_init, LogicalOutputPluginInit);

	cb->startup_cb = pg_decode_startup;
	cb->begin_cb = pg_decode_begin_txn;
	cb->change_cb = pg_decode_change;
	cb->commit_cb = pg_decode_commit_txn;
	cb->message_cb = pg_decode_message;
	cb->shutdown_cb = pg_decode_shutdown;

	Assert(ThisTimeLineID > 0);
}

/* Ensure a pgactive_parse_... arg is non-null */
static void
pgactive_parse_notnull(DefElem *elem, const char *paramtype)
{
	if (elem->arg == NULL || strVal(elem->arg) == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("%s parameter \"%s\" had no value",
						paramtype, elem->defname)));
}


static void
pgactive_parse_uint32(DefElem *elem, uint32 *res)
{
	pgactive_parse_notnull(elem, "uint32");
	errno = 0;
	*res = strtoul(strVal(elem->arg), NULL, 0);

	if (errno != 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("could not parse uint32 value \"%s\" for parameter \"%s\": %m",
						strVal(elem->arg), elem->defname)));
}

static void
pgactive_parse_size_t(DefElem *elem, size_t *res)
{
	pgactive_parse_notnull(elem, "size_t");
	errno = 0;
	*res = strtoull(strVal(elem->arg), NULL, 0);

	if (errno != 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("could not parse size_t value \"%s\" for parameter \"%s\": %m",
						strVal(elem->arg), elem->defname)));
}

static void
pgactive_parse_bool(DefElem *elem, bool *res)
{
	pgactive_parse_notnull(elem, "bool");
	if (!parse_bool(strVal(elem->arg), res))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("could not parse boolean value \"%s\" for parameter \"%s\": %m",
						strVal(elem->arg), elem->defname)));
}

static void
pgactive_parse_identifier_list_arr(DefElem *elem, char ***list, int *len)
{
	List	   *namelist;
	ListCell   *c;

	pgactive_parse_notnull(elem, "list");

	if (!SplitIdentifierString(pstrdup(strVal(elem->arg)),
							   ',', &namelist))
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("could not identifier list value \"%s\" for parameter \"%s\": %m",
						strVal(elem->arg), elem->defname)));
	}

	*len = 0;
	*list = palloc(list_length(namelist) * sizeof(char *));

	foreach(c, namelist)
	{
		(*list)[(*len)++] = pstrdup(lfirst(c));
	}
	list_free(namelist);
}

static void
pgactive_parse_str(DefElem *elem, char **res)
{
	pgactive_parse_notnull(elem, "string");
	*res = pstrdup(strVal(elem->arg));
}

static void
pgactive_req_param(const char *param)
{
	ereport(ERROR,
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			 errmsg("missing value for for parameter \"%s\"",
					param)));
}

/*
 * Check pgactive.pgactive_nodes entry in local DB and if status != r
 * and we're trying to begin logical replay, raise an error.
 *
 * Also prevents slot creation if the pgactive extension isn't installed in the
 * local node.
 *
 * If this function returns it's safe to begin replay.
 */
static void
pgactive_ensure_node_ready(pgactiveOutputData * data)
{
	int			spi_ret;
	char		our_status;
	pgactiveNodeStatus remote_status;
	NameData	dbname;
	char	   *tmp_dbname;

	/* We need dbname valid outside this transaction, so copy it */
	tmp_dbname = get_database_name(MyDatabaseId);
	snprintf(NameStr(dbname), NAMEDATALEN, "%s", tmp_dbname);
	pfree(tmp_dbname);

	/*
	 * Refuse to begin replication if the local node isn't yet ready to send
	 * data. Check the status in pgactive.pgactive_nodes.
	 */
	spi_ret = SPI_connect();
	if (spi_ret != SPI_OK_CONNECT)
		elog(ERROR, "local SPI connect failed; shouldn't happen");
	PushActiveSnapshot(GetTransactionSnapshot());

	our_status = pgactive_local_node_status();

	{
		pgactiveNodeInfo *remote_nodeinfo;

		remote_nodeinfo = pgactive_nodes_get_local_info(&data->remote_node);
		remote_status = remote_nodeinfo == NULL ? '\0' : remote_nodeinfo->status;
		pgactive_pgactive_node_free(remote_nodeinfo);
	}

	SPI_finish();
	PopActiveSnapshot();

	if (remote_status == pgactive_NODE_STATUS_KILLED)
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("pgactive output plugin: slot usage rejected, remote node is killed")));
	}

	/*
	 * Complain if node isn't ready,
	 */
	switch (our_status)
	{
		case pgactive_NODE_STATUS_READY:
		case pgactive_NODE_STATUS_CREATING_OUTBOUND_SLOTS:
			break;				/* node ready or creating outbound slots */
		case pgactive_NODE_STATUS_NONE:
		case pgactive_NODE_STATUS_BEGINNING_INIT:
			/* This isn't a pgactive node yet. */
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("pgactive output plugin: slot creation rejected, pgactive.pgactive_nodes entry for local node " pgactive_NODEID_FORMAT " does not exist",
							pgactive_LOCALID_FORMAT_ARGS),
					 errdetail("pgactive is not active on this database."),
					 errhint("Add pgactive to shared_preload_libraries and check logs for pgactive startup errors.")));
			break;
		case pgactive_NODE_STATUS_CATCHUP:

			/*
			 * When in catchup mode we write rows with their true origin, so
			 * it's safe to create and use a slot now. Just to be careful the
			 * join code will refuse to use an upstream that isn't in 'r'eady
			 * state.
			 *
			 * Locally originated changes will still be replayed to peers (but
			 * we should set readonly mode to prevent them entirely).
			 */
			break;
		case pgactive_NODE_STATUS_COPYING_INITIAL_DATA:

			/*
			 * We used to refuse to create a slot before/during apply of base
			 * backup. Now we have pgactive.do_not_replicate set DoNotReplicateId
			 * when restoring so it's safe to do so since we can't replicate
			 * the backup to peers anymore.
			 *
			 * Locally originated changes will still be replayed to peers (but
			 * we should set readonly mode to prevent them entirely).
			 */
			break;
		case pgactive_NODE_STATUS_KILLED:
			elog(ERROR, "node is exiting");
			break;

		default:
			elog(ERROR, "unhandled case status=%c", our_status);
			break;
	}
}


/* initialize this plugin */
static void
pg_decode_startup(LogicalDecodingContext *ctx, OutputPluginOptions *opt, bool is_init)
{
	ListCell   *option;
	pgactiveOutputData *data;
	Oid			schema_oid;
	bool		tx_started = false;
	Oid			local_dboid;

	data = palloc0(sizeof(pgactiveOutputData));
	data->context = AllocSetContextCreate(TopMemoryContext,
										  "pgactive conversion context",
										  ALLOCSET_DEFAULT_MINSIZE,
										  ALLOCSET_DEFAULT_INITSIZE,
										  ALLOCSET_DEFAULT_MAXSIZE);

	ctx->output_plugin_private = data;

	opt->output_type = OUTPUT_PLUGIN_BINARY_OUTPUT;

	data->pgactive_conflict_history_reloid = InvalidOid;
	data->pgactive_conflict_handlers_reloid = InvalidOid;
	data->pgactive_locks_reloid = InvalidOid;
	data->pgactive_schema_oid = InvalidOid;
	data->num_replication_sets = -1;

	/* parse where the connection has to be from */
	pgactive_parse_slot_name(NameStr(MyReplicationSlot->data.name),
						&data->remote_node, &local_dboid);

	/* parse options passed in by the client */

	foreach(option, ctx->output_plugin_options)
	{
		DefElem    *elem = lfirst(option);

		Assert(elem->arg == NULL || IsA(elem->arg, String));

		if (strcmp(elem->defname, "pg_version") == 0)
			pgactive_parse_uint32(elem, &data->client_pg_version);
		else if (strcmp(elem->defname, "pg_catversion") == 0)
			pgactive_parse_uint32(elem, &data->client_pg_catversion);
		else if (strcmp(elem->defname, "pgactive_version") == 0)
			pgactive_parse_uint32(elem, &data->client_pgactive_version);
		else if (strcmp(elem->defname, "pgactive_variant") == 0)
			pgactive_parse_str(elem, &data->client_pgactive_variant);
		else if (strcmp(elem->defname, "min_pgactive_version") == 0)
			pgactive_parse_uint32(elem, &data->client_min_pgactive_version);
		else if (strcmp(elem->defname, "sizeof_int") == 0)
			pgactive_parse_size_t(elem, &data->client_sizeof_int);
		else if (strcmp(elem->defname, "sizeof_long") == 0)
			pgactive_parse_size_t(elem, &data->client_sizeof_long);
		else if (strcmp(elem->defname, "sizeof_datum") == 0)
			pgactive_parse_size_t(elem, &data->client_sizeof_datum);
		else if (strcmp(elem->defname, "maxalign") == 0)
			pgactive_parse_size_t(elem, &data->client_maxalign);
		else if (strcmp(elem->defname, "bigendian") == 0)
			pgactive_parse_bool(elem, &data->client_bigendian);
		else if (strcmp(elem->defname, "float4_byval") == 0)
			pgactive_parse_bool(elem, &data->client_float4_byval);
		else if (strcmp(elem->defname, "float8_byval") == 0)
			pgactive_parse_bool(elem, &data->client_float8_byval);
		else if (strcmp(elem->defname, "integer_datetimes") == 0)
			pgactive_parse_bool(elem, &data->client_int_datetime);
		else if (strcmp(elem->defname, "db_encoding") == 0)
			data->client_db_encoding = pstrdup(strVal(elem->arg));
		else if (strcmp(elem->defname, "forward_changesets") == 0)
			pgactive_parse_bool(elem, &data->forward_changesets);
		else if (strcmp(elem->defname, "replication_sets") == 0)
		{
			int			i;

			/* parse list */
			pgactive_parse_identifier_list_arr(elem,
										  &data->replication_sets,
										  &data->num_replication_sets);

			Assert(data->num_replication_sets >= 0);

			/* validate elements */
			for (i = 0; i < data->num_replication_sets; i++)
				pgactive_validate_replication_set_name(data->replication_sets[i],
												  true);

			/* make it bsearch()able */
			qsort(data->replication_sets, data->num_replication_sets,
				  sizeof(char *), pg_qsort_strcmp);
		}
		else if (strcmp(elem->defname, "interactive") == 0)
		{
			/*
			 * Set defaults for interactive mode
			 *
			 * This is used for examining the replication queue from SQL.
			 */
			data->client_pg_version = PG_VERSION_NUM;
			data->client_pg_catversion = CATALOG_VERSION_NO;
			data->client_pgactive_version = pgactive_VERSION_NUM;
			data->client_pgactive_variant = pgactive_VARIANT;
			data->client_min_pgactive_version = pgactive_VERSION_NUM;
			data->client_sizeof_int = sizeof(int);
			data->client_sizeof_long = sizeof(long);
			data->client_sizeof_datum = sizeof(Datum);
			data->client_maxalign = MAXIMUM_ALIGNOF;
			data->client_bigendian = pgactive_get_bigendian();
			data->client_float4_byval = pgactive_get_float4byval();
			data->client_float8_byval = pgactive_get_float8byval();
			data->client_int_datetime = pgactive_get_integer_timestamps();
			data->client_db_encoding = pstrdup(GetDatabaseEncodingName());
		}
		else
		{
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("option \"%s\" = \"%s\" is unknown",
							elem->defname,
							elem->arg ? strVal(elem->arg) : "(null)")));
		}
	}

	if (!is_init)
	{
		/*
		 * There's a potential corruption bug in PostgreSQL 10.1, 9.6.6,
		 * 9.5.10 and 9.4.15 that can cause reorder buffers to accumulate
		 * duplicated transactions. See
		 * https://www.postgresql.org/message-id/CAMsr+YHdX=XECbZshDZ2CZNWGTyw-taYBnzqVfx4JzM4ExP5xg@mail.gmail.com
		 *
		 * We can defend against this by doing our own cleanup of any
		 * serialized txns in the reorder buffer on startup.
		 */
		pglReorderBufferCleanSerializedTXNs(NameStr(MyReplicationSlot->data.name));
	}

	/*
	 * Ensure that the pgactive extension is installed on this database.
	 *
	 * We must prevent slot creation before the pgactive extension is created,
	 * otherwise the event trigger for DDL replication will record the
	 * extension's creation in pgactive.pgactive_queued_commands and the slot position
	 * will be before then, causing CREATE EXTENSION to be replayed. Since the
	 * other end already has the pgactive extension (obviously) this will cause
	 * replay to fail.
	 *
	 * TODO: Should really test for the extension its self, but this is faster
	 * and easier...
	 */
	if (!IsTransactionState())
	{
		tx_started = true;
		StartTransactionCommand();
	}

	/* pgactive extension must be installed. */
	if (get_namespace_oid("pgactive", true) == InvalidOid)
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("pgactive extension does not exist on " pgactive_NODEID_FORMAT,
						pgactive_LOCALID_FORMAT_ARGS),
				 errdetail("Cannot create a pgactive slot without the pgactive extension installed.")));
	}

	/* no options are passed in during initialization, so don't complain there */
	if (!is_init)
	{
		if (data->client_pg_version == 0)
			pgactive_req_param("pg_version");
		if (data->client_pg_catversion == 0)
			pgactive_req_param("pg_catversion");
		if (data->client_pgactive_version == 0)
			pgactive_req_param("pgactive_version");
		if (data->client_min_pgactive_version == 0)
			pgactive_req_param("min_pgactive_version");
		if (data->client_sizeof_int == 0)
			pgactive_req_param("sizeof_int");
		if (data->client_sizeof_long == 0)
			pgactive_req_param("sizeof_long");
		if (data->client_sizeof_datum == 0)
			pgactive_req_param("sizeof_datum");
		if (data->client_maxalign == 0)
			pgactive_req_param("maxalign");
		/* XXX: can't check for boolean values this way */
		if (data->client_db_encoding == NULL)
			pgactive_req_param("db_encoding");

		/* check incompatibilities we cannot work around */
		if (strcmp(data->client_db_encoding, GetDatabaseEncodingName()) != 0)
			elog(ERROR, "mismatching encodings are not yet supported");

		if (data->client_min_pgactive_version > pgactive_VERSION_NUM)
			elog(ERROR, "incompatible pgactive client and server versions, server too old");
		if (data->client_pgactive_version < pgactive_MIN_REMOTE_VERSION_NUM)
			elog(ERROR, "incompatible pgactive client and server versions, client too old");

		data->allow_binary_protocol = true;
		data->allow_sendrecv_protocol = true;

		/*
		 * Now use the passed in information to determine how to encode the
		 * data sent by the output plugin. We don't make datatype specific
		 * decisions here, just generic decisions about using binary and/or
		 * send/recv protocols.
		 */

		/*
		 * Don't use the binary protocol if there are fundamental arch
		 * differences.
		 */
		if (data->client_sizeof_int != sizeof(int) ||
			data->client_sizeof_long != sizeof(long) ||
			data->client_sizeof_datum != sizeof(Datum))
		{
			data->allow_binary_protocol = false;
			elog(LOG, "disabling binary protocol because of sizeof differences");
		}
		else if (data->client_bigendian != pgactive_get_bigendian())
		{
			data->allow_binary_protocol = false;
			elog(LOG, "disabling binary protocol because of endianess difference");
		}

		/*
		 * We also can't use the binary protocol if there are critical
		 * differences in compile time settings.
		 */
		if (data->client_float4_byval != pgactive_get_float4byval() ||
			data->client_float8_byval != pgactive_get_float8byval())
			data->allow_binary_protocol = false;

		if (data->client_int_datetime != pgactive_get_integer_timestamps())
			data->int_datetime_mismatch = true;
		else
			data->int_datetime_mismatch = false;


		/*
		 * Don't use the send/recv protocol if there are version differences.
		 * There currently isn't any guarantee for cross version compatibility
		 * of the send/recv representations. But there actually *is* a compat.
		 * guarantee for architecture differences...
		 *
		 * XXX: We could easily do better by doing per datatype considerations
		 * if there are known incompatibilities.
		 */
		if (data->client_pg_version / 100 != PG_VERSION_NUM / 100)
			data->allow_sendrecv_protocol = false;

		pgactive_maintain_schema(false);

		data->pgactive_schema_oid = get_namespace_oid("pgactive", true);
		schema_oid = data->pgactive_schema_oid;

		if (schema_oid != InvalidOid)
		{
			data->pgactive_conflict_handlers_reloid =
				get_relname_relid("pgactive_conflict_handlers", schema_oid);

			if (data->pgactive_conflict_handlers_reloid == InvalidOid)
				elog(ERROR, "cache lookup for relation pgactive.pgactive_conflict_handlers failed");
			else
				elog(DEBUG1, "pgactive.pgactive_conflict_handlers OID set to %u",
					 data->pgactive_conflict_handlers_reloid);

			data->pgactive_conflict_history_reloid =
				get_relname_relid("pgactive_conflict_history", schema_oid);

			if (data->pgactive_conflict_history_reloid == InvalidOid)
				elog(ERROR, "cache lookup for relation pgactive.pgactive_conflict_history failed");

			data->pgactive_locks_reloid =
				get_relname_relid("pgactive_global_locks", schema_oid);

			if (data->pgactive_locks_reloid == InvalidOid)
				elog(ERROR, "cache lookup for relation pgactive.pgactive_global_locks failed");
		}
		else
			elog(WARNING, "cache lookup for schema pgactive failed");

		/*
		 * Make sure it's safe to begin playing changes to the remote end.
		 * This'll ERROR out if we're not ready. Note that this does NOT
		 * prevent slot creation, only START_REPLICATION from the slot.
		 */
		pgactive_ensure_node_ready(data);
	}

	if (tx_started)
		CommitTransactionCommand();

	/*
	 * Everything looks ok. Acquire a shmem slot to represent us running.
	 */
	{
		uint32		worker_idx;

		LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);

		if (pgactiveWorkerCtl->worker_management_paused)
		{
			LWLockRelease(pgactiveWorkerCtl->lock);
			elog(ERROR, "pgactive worker management is currently paused, walsender exiting; retry later.");
		}

		pgactive_worker_shmem_alloc(pgactive_WORKER_WALSENDER, &worker_idx);
		pgactive_worker_shmem_acquire(pgactive_WORKER_WALSENDER, worker_idx, true);
		pgactive_worker_slot->worker_pid = MyProcPid;
		pgactive_worker_slot->worker_proc = MyProc;
		/* can be null if sql interface is used */
		pgactive_worker_slot->data.walsnd.walsender = MyWalSnd;
		pgactive_worker_slot->data.walsnd.slot = MyReplicationSlot;
		pgactive_nodeid_cpy(&pgactive_worker_slot->data.walsnd.remote_node, &data->remote_node);
		pgactive_worker_slot->data.walsnd.last_sent_xact_id = InvalidTransactionId;
		pgactive_worker_slot->data.walsnd.last_sent_xact_committs = 0;
		pgactive_worker_slot->data.walsnd.last_sent_xact_at = 0;
		pgactive_walsender_worker = &pgactive_worker_slot->data.walsnd;

		LWLockRelease(pgactiveWorkerCtl->lock);
	}
}

static void
pg_decode_shutdown(LogicalDecodingContext *ctx)
{
	/* release and free slot */
	pgactive_worker_shmem_release();
}

/*
 * Only changesets generated on the local node should be replicated
 * to the client unless we're in changeset forwarding mode.
 */
static inline bool
should_forward_changeset(LogicalDecodingContext *ctx,
						 RepOriginId origin_id)
{
	pgactiveOutputData *const data = ctx->output_plugin_private;

	if (origin_id == InvalidRepOriginId || data->forward_changesets)
		return true;
	else if (origin_id == DoNotReplicateId)
		return false;

	/*
	 * We do not let the pgactive output plugin replicate changes that came from
	 * pgactive peers to avoid replication loops. We used to check the replication
	 * origin name to determine whether the changes came from peer pgactive nodes
	 * or non-pgactive nodes. The commit 76a88a0ba23b874 introduced a shared hash
	 * table to track all pgactive replication origin names. We used this hash
	 * table to filter out changes from peer pgactive nodes and to forward changes
	 * from non-pgactive nodes. However, we determined a severe performance
	 * bottleneck with the hash table lookup. A simple use-case that revealed
	 * this bottleneck is - on a 2 node pgactive group, bulk loaded data on node 1,
	 * upon applying the changes received from node 1, the logical walsender
	 * on node 2 corresponding to node 1 was performing a hash table lookup
	 * for every decoded change. Due to this, frequent look up the walsender
	 * was consuming ~100% CPU and any simple DML on node 2 was taking days to
	 * reach node 1.
	 *
	 * To avoid the performance bottleneck, we do two things - 1) We disallow
	 * a pgactive node pulling in changes from any non-pgactive/external logical
	 * replication solutions. 2) We removed the shared hash table completely.
	 * With these things, a pgactive node doesn't have to look for any replication
	 * origin names to determine non-pgactive changes. Because, every change that
	 * comes with a valid origin_id is essentially from pgactive peers, and can
	 * safely be filtered out i.e. not forward further to avoid replication
	 * loops.
	 */
	return false;
}

static inline bool
should_forward_change(LogicalDecodingContext *ctx, pgactiveOutputData * data,
					  pgactiveRelation * r, enum ReorderBufferChangeType change)
{
	/* internal pgactive relations that may not be replicated */
	if (RelationGetRelid(r->rel) == data->pgactive_conflict_handlers_reloid ||
		RelationGetRelid(r->rel) == data->pgactive_locks_reloid ||
		RelationGetRelid(r->rel) == data->pgactive_conflict_history_reloid)
		return false;

	/*
	 * Quite ugly, but there's no neat way right now: Flush replication set
	 * configuration from pgactive's relcache.
	 */
	if (RelationGetRelid(r->rel) == pgactiveReplicationSetConfigRelid)
		pgactiveRelcacheHashInvalidateCallback(0, InvalidOid);

	/* always replicate other stuff in the pgactive schema */
	if (r->rel->rd_rel->relnamespace == data->pgactive_schema_oid)
		return true;

	if (!r->computed_repl_valid)
		pgactive_heap_compute_replication_settings(r,
											  data->num_replication_sets,
											  data->replication_sets);

	/* Check whether the current action is configured to be replicated */
	switch (change)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			return r->computed_repl_insert;
		case REORDER_BUFFER_CHANGE_UPDATE:
			return r->computed_repl_update;
		case REORDER_BUFFER_CHANGE_DELETE:
			return r->computed_repl_delete;
		default:
			elog(ERROR, "should be unreachable");
	}
}

/*
 * BEGIN callback
 *
 * If you change this you must also change the corresponding code in
 * pgactive_apply.c . Make sure that any flags are in sync.
 */
void
pg_decode_begin_txn(LogicalDecodingContext *ctx, ReorderBufferTXN *txn)
{
	pgactiveOutputData *data = ctx->output_plugin_private;
	int			flags = 0;
	TimestampTz committime;

	AssertVariableIsOfType(&pg_decode_begin_txn, LogicalDecodeBeginCB);

	if (!should_forward_changeset(ctx, txn->origin_id))
		return;

	OutputPluginPrepareWrite(ctx, true);
	pq_sendbyte(ctx->out, 'B'); /* BEGIN */


	/*
	 * Are we forwarding changesets from other nodes? If so, we must include
	 * the origin node ID and LSN in BEGIN records.
	 */
	if (data->forward_changesets)
		flags |= pgactive_OUTPUT_TRANSACTION_HAS_ORIGIN;

	/* send the flags field its self */
	pq_sendint(ctx->out, flags, 4);

	/* fixed fields */

	/*
	 * pgactive 1.0 sent the commit start lsn here, but that has issues with
	 * progress tracking; see pgactive_apply for details. Instead send LSN of end
	 * of commit + 1 so that's what gets recorded in replication origins.
	 */
	pq_sendint64(ctx->out, txn->end_lsn);
#if PG_VERSION_NUM >= 150000
	pq_sendint64(ctx->out, txn->xact_time.commit_time);
	committime = txn->xact_time.commit_time;
#else
	pq_sendint64(ctx->out, txn->commit_time);
	committime = txn->commit_time;
#endif
	pq_sendint(ctx->out, txn->xid, 4);

	/* and optional data selected above */
	if (flags & pgactive_OUTPUT_TRANSACTION_HAS_ORIGIN)
	{
		/*
		 * The RepOriginId in txn->origin_id is our local identifier for the
		 * origin node, but it's not valid outside our node. It must be
		 * converted into the (sysid, tlid, dboid) that uniquely identifies
		 * the node globally so that can be sent.
		 */
		pgactiveNodeId	origin;

		pgactive_fetch_sysid_via_node_id(txn->origin_id, &origin);

		pgactive_send_nodeid(ctx->out, &origin, false);
		pq_sendint64(ctx->out, txn->origin_lsn);
	}

	OutputPluginWrite(ctx, true);

	/* Save last sent transaction info */
	pgactive_walsender_worker->last_sent_xact_id = txn->xid;
	pgactive_walsender_worker->last_sent_xact_committs = committime;
	pgactive_walsender_worker->last_sent_xact_at = GetCurrentTimestamp();

	return;
}

/*
 * COMMIT callback
 *
 * Send the LSN at the time of the commit, the commit time, and the end LSN.
 *
 * The presence of additional records is controlled by a flag field, with
 * records that're present appearing strictly in the order they're listed
 * here. There is no sub-record header or other structure beyond the flags
 * field.
 *
 * If you change this, you'll need to change process_remote_commit(...)
 * too. Make sure to keep any flags in sync.
 */
void
pg_decode_commit_txn(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
					 XLogRecPtr commit_lsn)
{
	int			flags = 0;

	if (!should_forward_changeset(ctx, txn->origin_id))
		return;

	OutputPluginPrepareWrite(ctx, true);
	pq_sendbyte(ctx->out, 'C'); /* sending COMMIT */

	/* send the flags field its self */
	pq_sendint(ctx->out, flags, 4);

	/* Send fixed fields */
	Assert(commit_lsn == txn->final_lsn);	/* why do we pass this to the CB
											 * separately? */
	pq_sendint64(ctx->out, commit_lsn);

	/*
	 * end_lsn is end of commit + 1, which is what's used in replorigin and
	 * feedback msgs
	 */
	Assert(txn->end_lsn != InvalidXLogRecPtr);
	pq_sendint64(ctx->out, txn->end_lsn);
#if PG_VERSION_NUM >= 150000
	pq_sendint64(ctx->out, txn->xact_time.commit_time);
#else
	pq_sendint64(ctx->out, txn->commit_time);
#endif

	OutputPluginWrite(ctx, true);
}

void
pg_decode_change(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
				 Relation relation, ReorderBufferChange *change)
{
	pgactiveOutputData *data;
	MemoryContext old;
	pgactiveRelation *pgactive_relation;

#ifdef USE_ASSERT_CHECKING

	/*
	 * NB: We take a lock to avoid assertion failure in relation_open(). We
	 * don't take any lock in non-assert builds. Well, this might sound like a
	 * hack. But, acquiring lock for every decoded change might prove costly
	 * on production builds. In the worst case, it may happen that somebody
	 * can add the relation to a replication set while we are reading it here
	 * without any lock, and our should_forward_change() check can miss it.
	 * That is less of a concern than acquiring lock for every decoded change.
	 */
	pgactive_relation = pgactive_table_open(RelationGetRelid(relation), AccessShareLock);
#else
	pgactive_relation = pgactive_table_open(RelationGetRelid(relation), NoLock);
#endif

	data = ctx->output_plugin_private;

	/* Avoid leaking memory by using and resetting our own context */
	old = MemoryContextSwitchTo(data->context);

	if (!should_forward_changeset(ctx, txn->origin_id))
		goto skip;

	if (!should_forward_change(ctx, data, pgactive_relation, change->action))
		goto skip;

	OutputPluginPrepareWrite(ctx, true);

	switch (change->action)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			pq_sendbyte(ctx->out, 'I'); /* action INSERT */
			write_rel(ctx->out, relation);
			pq_sendbyte(ctx->out, 'N'); /* new tuple follows */
			write_tuple(data, ctx->out, relation, &change->data.tp.newtuple->tuple);
			break;
		case REORDER_BUFFER_CHANGE_UPDATE:
			pq_sendbyte(ctx->out, 'U'); /* action UPDATE */
			write_rel(ctx->out, relation);
			if (change->data.tp.oldtuple != NULL)
			{
				pq_sendbyte(ctx->out, 'K'); /* old key follows */
				write_tuple(data, ctx->out, relation,
							&change->data.tp.oldtuple->tuple);
			}
			pq_sendbyte(ctx->out, 'N'); /* new tuple follows */
			write_tuple(data, ctx->out, relation,
						&change->data.tp.newtuple->tuple);
			break;
		case REORDER_BUFFER_CHANGE_DELETE:
			pq_sendbyte(ctx->out, 'D'); /* action DELETE */
			write_rel(ctx->out, relation);
			if (change->data.tp.oldtuple != NULL)
			{
				pq_sendbyte(ctx->out, 'K'); /* old key follows */
				write_tuple(data, ctx->out, relation,
							&change->data.tp.oldtuple->tuple);
			}
			else
				pq_sendbyte(ctx->out, 'E'); /* empty */
			break;
		default:
			Assert(false);
	}
	OutputPluginWrite(ctx, true);

skip:
	MemoryContextSwitchTo(old);
	MemoryContextReset(data->context);

	pgactive_table_close(pgactive_relation, NoLock);
}

/*
 * Write schema.relation to the output stream.
 */
static void
write_rel(StringInfo out, Relation rel)
{
	const char *nspname;
	int64		nspnamelen;
	const char *relname;
	int64		relnamelen;

	nspname = get_namespace_name(rel->rd_rel->relnamespace);
	if (nspname == NULL)
		elog(ERROR, "cache lookup failed for namespace %u",
			 rel->rd_rel->relnamespace);
	nspnamelen = strlen(nspname) + 1;

	relname = NameStr(rel->rd_rel->relname);
	relnamelen = strlen(relname) + 1;

	pq_sendint(out, nspnamelen, 2); /* schema name length */
	appendBinaryStringInfo(out, nspname, nspnamelen);

	pq_sendint(out, relnamelen, 2); /* table name length */
	appendBinaryStringInfo(out, relname, relnamelen);
}

/*
 * Make the executive decision about which protocol to use.
 */
static void
decide_datum_transfer(pgactiveOutputData * data,
					  Form_pg_attribute att, Form_pg_type typclass,
					  bool *use_binary, bool *use_sendrecv)
{
	/* always disallow fancyness if there's type representation mismatches */
	if (data->int_datetime_mismatch &&
		(att->atttypid == TIMESTAMPOID || att->atttypid == TIMESTAMPTZOID ||
		 att->atttypid == TIMEOID))
	{
		*use_binary = false;
		*use_sendrecv = false;
	}

	/*
	 * Use the binary protocol, if allowed, for builtin & plain datatypes.
	 */
	else if (data->allow_binary_protocol &&
			 typclass->typtype == 'b' &&
			 att->atttypid < FirstNormalObjectId &&
			 typclass->typelem == InvalidOid)
	{
		*use_binary = true;
	}

	/*
	 * Use send/recv, if allowed, if the type is plain or builtin.
	 *
	 * XXX: we can't use send/recv for array or composite types for now due to
	 * the embedded oids.
	 */
	else if (data->allow_sendrecv_protocol &&
			 OidIsValid(typclass->typreceive) &&
			 (att->atttypid < FirstNormalObjectId || typclass->typtype != 'c') &&
			 (att->atttypid < FirstNormalObjectId || typclass->typelem == InvalidOid))
	{
		*use_sendrecv = true;
	}
}

/*
 * Write a tuple to the outputstream, in the most efficient format possible.
 */
static void
write_tuple(pgactiveOutputData * data, StringInfo out, Relation rel,
			HeapTuple tuple)
{
	TupleDesc	desc;
	Datum		values[MaxTupleAttributeNumber];
	bool		isnull[MaxTupleAttributeNumber];
	int			i;

	desc = RelationGetDescr(rel);

	pq_sendbyte(out, 'T');		/* tuple follows */

	pq_sendint(out, desc->natts, 4);	/* number of attributes */

	/* try to allocate enough memory from the get go */
	enlargeStringInfo(out, tuple->t_len +
					  desc->natts * (1 + 4));

	/*
	 * XXX: should this prove to be a relevant bottleneck, it might be
	 * interesting to inline heap_deform_tuple() here, we don't actually need
	 * the information in the form we get from it.
	 */
	heap_deform_tuple(tuple, desc, values, isnull);

	for (i = 0; i < desc->natts; i++)
	{
		HeapTuple	typtup;
		Form_pg_type typclass;

		Form_pg_attribute att = &desc->attrs[i];

		bool		use_binary = false;
		bool		use_sendrecv = false;

		if (isnull[i] || att->attisdropped)
		{
			pq_sendbyte(out, 'n');	/* null column */
			continue;
		}
		else if (att->attlen == -1 && VARATT_IS_EXTERNAL_ONDISK(values[i]))
		{
			pq_sendbyte(out, 'u');	/* unchanged toast column */
			continue;
		}

		typtup = SearchSysCache1(TYPEOID, ObjectIdGetDatum(att->atttypid));
		if (!HeapTupleIsValid(typtup))
			elog(ERROR, "cache lookup failed for type %u", att->atttypid);
		typclass = (Form_pg_type) GETSTRUCT(typtup);

		decide_datum_transfer(data, att, typclass, &use_binary, &use_sendrecv);

		if (use_binary)
		{
			pq_sendbyte(out, 'b');	/* binary data follows */

			/* pass by value */
			if (att->attbyval)
			{
				pq_sendint(out, att->attlen, 4);	/* length */

				enlargeStringInfo(out, att->attlen);
				store_att_byval(out->data + out->len, values[i], att->attlen);
				out->len += att->attlen;
				out->data[out->len] = '\0';
			}
			/* fixed length non-varlena pass-by-reference type */
			else if (att->attlen > 0)
			{
				pq_sendint(out, att->attlen, 4);	/* length */

				appendBinaryStringInfo(out, DatumGetPointer(values[i]),
									   att->attlen);
			}
			/* varlena type */
			else if (att->attlen == -1)
			{
				char	   *data = DatumGetPointer(values[i]);

				/* send indirect datums inline */
				if (VARATT_IS_EXTERNAL_INDIRECT(values[i]))
				{
					struct varatt_indirect redirect;

					VARATT_EXTERNAL_GET_POINTER(redirect, data);
					data = (char *) redirect.pointer;
				}

				Assert(!VARATT_IS_EXTERNAL(data));

				pq_sendint(out, VARSIZE_ANY(data), 4);	/* length */

				appendBinaryStringInfo(out, data,
									   VARSIZE_ANY(data));

			}
			else
				elog(ERROR, "unsupported tuple type");
		}
		else if (use_sendrecv)
		{
			bytea	   *outputbytes;
			int			len;

			pq_sendbyte(out, 's');	/* 'send' data follows */

			outputbytes =
				OidSendFunctionCall(typclass->typsend, values[i]);

			len = VARSIZE(outputbytes) - VARHDRSZ;
			pq_sendint(out, len, 4);	/* length */
			pq_sendbytes(out, VARDATA(outputbytes), len);	/* data */
			pfree(outputbytes);
		}
		else
		{
			char	   *outputstr;
			int			len;

			pq_sendbyte(out, 't');	/* 'text' data follows */

			outputstr =
				OidOutputFunctionCall(typclass->typoutput, values[i]);
			len = strlen(outputstr) + 1;
			pq_sendint(out, len, 4);	/* length */
			appendBinaryStringInfo(out, outputstr, len);	/* data */
			pfree(outputstr);
		}

		ReleaseSysCache(typtup);
	}
}

static void
pg_decode_message(LogicalDecodingContext *ctx,
				  ReorderBufferTXN *txn, XLogRecPtr lsn,
				  bool transactional, const char *prefix,
				  Size sz, const char *message)
{
	if (strcmp(prefix, pgactive_LOGICAL_MSG_PREFIX) == 0)
	{
		OutputPluginPrepareWrite(ctx, true);
		pq_sendbyte(ctx->out, 'M'); /* message follows */
		pq_sendbyte(ctx->out, transactional);
		pq_sendint64(ctx->out, lsn);
		pq_sendint(ctx->out, sz, 4);
		pq_sendbytes(ctx->out, message, sz);
		OutputPluginWrite(ctx, true);
	}
}

/*
 * Clone of ReorderBufferCleanSerializedTXNs; see
 * https://www.postgresql.org/message-id/CAMsr+YHdX=XECbZshDZ2CZNWGTyw-taYBnzqVfx4JzM4ExP5xg@mail.gmail.com
 */
static void
pglReorderBufferCleanSerializedTXNs(const char *slotname)
{
	DIR		   *spill_dir = NULL;
	struct dirent *spill_de;
	struct stat statbuf;
	char		path[MAXPGPATH * 2 + 12];

	snprintf(path, sizeof(path), "pg_replslot/%s", slotname);

	/* we're only handling directories here, skip if it's not our's */
	if (lstat(path, &statbuf) == 0 && !S_ISDIR(statbuf.st_mode))
		return;

	spill_dir = AllocateDir(path);
	while ((spill_de = ReadDir(spill_dir, path)) != NULL)
	{
		if (strcmp(spill_de->d_name, ".") == 0 ||
			strcmp(spill_de->d_name, "..") == 0)
			continue;

		/* only look at names that can be ours */
		if (strncmp(spill_de->d_name, "xid", 3) == 0)
		{
			snprintf(path, sizeof(path), "pg_replslot/%s/%s", slotname,
					 spill_de->d_name);

			if (unlink(path) != 0)
				ereport(PANIC,
						(errcode_for_file_access(),
						 errmsg("could not remove file \"%s\": %m",
								path)));
		}
	}
	FreeDir(spill_dir);
}
