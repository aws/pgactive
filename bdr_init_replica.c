/* -------------------------------------------------------------------------
 *
 * bdr_init_replica.c
 *     Populate a new bdr node from the data in an existing node
 *
 * Use dump and restore, then bdr catchup mode, to bring up a new
 * bdr node into a bdr group. Allows a new blank database to be
 * introduced into an existing, already-working bdr group.
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		bdr_init_replica.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "bdr.h"
#include "bdr_internal.h"
#include "bdr_locks.h"

#include "fmgr.h"
#include "funcapi.h"
#include "libpq-fe.h"
#include "miscadmin.h"

#include "libpq/pqformat.h"

#include "access/heapam.h"
#include "access/xact.h"

#include "catalog/pg_type.h"

#include "executor/spi.h"

#include "replication/origin.h"
#include "replication/walreceiver.h"

#include "postmaster/bgworker.h"
#include "postmaster/bgwriter.h"

#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/pg_lsn.h"
#include "utils/snapmgr.h"
#include "pgstat.h"


char	   *bdr_temp_dump_directory = NULL;

static void bdr_execute_command(const char *cmd);
static void bdr_init_exec_dump_restore(BDRNodeInfo * node, char *snapshot);
static void bdr_catchup_to_lsn(remote_node_info * ri, XLogRecPtr target_lsn);

static XLogRecPtr
bdr_get_remote_lsn(PGconn *conn)
{
	XLogRecPtr	lsn;
	PGresult   *res;

	res = PQexec(conn, "SELECT pg_current_wal_insert_lsn()");
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		elog(ERROR, "unable to get remote LSN: status %s: %s",
			 PQresStatus(PQresultStatus(res)), PQresultErrorMessage(res));
	}
	Assert(PQntuples(res) == 1);
	Assert(!PQgetisnull(res, 0, 0));
	lsn = DatumGetLSN(DirectFunctionCall1Coll(pg_lsn_in, InvalidOid,
											  CStringGetDatum(PQgetvalue(res, 0, 0))));
	PQclear(res);
	return lsn;
}

static void
bdr_get_remote_ext_version(PGconn *pgconn, char **default_version,
						   char **installed_version)
{
	PGresult   *res;

	const char *q_bdr_installed =
		"SELECT default_version, installed_version "
		"FROM pg_catalog.pg_available_extensions WHERE name = 'bdr';";

	res = PQexec(pgconn, q_bdr_installed);

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		elog(ERROR, "unable to get remote bdr extension version; query %s failed with %s: %s",
			 q_bdr_installed, PQresStatus(PQresultStatus(res)), PQresultErrorMessage(res));
	}

	if (PQntuples(res) == 1)
	{
		/*
		 * bdr ext is known to Pg, check install state.
		 */
		*default_version = pstrdup(PQgetvalue(res, 0, 0));
		*installed_version = pstrdup(PQgetvalue(res, 0, 0));
	}
	else if (PQntuples(res) == 0)
	{
		/* bdr ext is not known to Pg at all */
		*default_version = NULL;
		*installed_version = NULL;
	}
	else
	{
		Assert(false);			/* Should not get >1 tuples */
	}

	PQclear(res);
}

/*
 * Make sure the bdr extension is installed on the other end. If it's a known
 * extension but not present in the current DB error out and tell the user to
 * activate BDR then try again.
 */
void
bdr_ensure_ext_installed(PGconn *pgconn)
{
	char	   *default_version = NULL;
	char	   *installed_version = NULL;

	bdr_get_remote_ext_version(pgconn, &default_version, &installed_version);

	if (default_version == NULL || strcmp(default_version, "") == 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("remote PostgreSQL install for bdr connection does not have bdr extension installed"),
				 errdetail("No entry with name 'bdr' in pg_available_extensions."),
				 errhint("You need to install the BDR extension on the remote end.")));
	}

	if (installed_version == NULL || strcmp(installed_version, "") == 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("remote database for BDR connection does not have the BDR extension active"),
				 errdetail("installed_version for entry 'bdr' in pg_available_extensions is blank."),
				 errhint("Run 'CREATE EXTENSION BDR;'.")));
	}

	pfree(default_version);
	pfree(installed_version);
}

static void
bdr_init_replica_cleanup_tmpdir(int errcode, Datum tmpdir)
{
	struct stat st;
	const char *dir = DatumGetCString(tmpdir);

	if (stat(dir, &st) == 0)
		if (!rmtree(dir, true))
			elog(WARNING, "failed to clean up BDR dump temporary directory %s on exit/error", dir);
}

/*
 * Function to execute a given commnd.
 *
 * Any sort of failure in command execution is a FATAL error so that
 * postmaster will just start the per-db worker again.
 */
static void
bdr_execute_command(const char *cmd)
{
	int		rc;

	elog(LOG, "BDR executing command \"%s\"", cmd);

	pgstat_report_wait_start(PG_WAIT_EXTENSION);
	rc = system(cmd);
	pgstat_report_wait_end();

	if (rc != 0)
	{
		/*
		 * If either the shell itself, or a called command, died on a signal,
		 * abort the per-db worker.  We do this because system() ignores SIGINT
		 * and SIGQUIT while waiting; so a signal is very likely something that
		 * should have interrupted us too.  Also die if the shell got a hard
		 * "command not found" type of error.  If we overreact it's no big
		 * deal, the postmaster will just start the per-db worker again.
		 */
		if (WIFEXITED(rc))
		{
			ereport(FATAL,
					(errmsg("command failed with exit code %d",
							WEXITSTATUS(rc)),
					 errdetail("The failed command was: %s", cmd)));
		}
		else if (WIFSIGNALED(rc))
		{
#if defined(WIN32)
			ereport(FATAL,
					(errmsg("command was terminated by exception 0x%X",
							WTERMSIG(rc)),
					 errhint("See C include file \"ntstatus.h\" for a description of the hexadecimal value."),
					 errdetail("The failed command was: %s", cmd)));
#else
			ereport(FATAL,
					(errmsg("command was terminated by signal %d: %s",
							WTERMSIG(rc), pg_strsignal(WTERMSIG(rc))),
					 errdetail("The failed command was: %s", cmd)));
#endif
		}
		else
		{
			ereport(FATAL,
					(errmsg("command exited with unrecognized status %d", rc),
					 errdetail("The failed command was: %s", cmd)));
		}
	}
}

/*
 * Copy the contents of a remote node using pg_dump and apply it to the local
 * node using pg_restore. Runs during node join creation to bring up a new
 * logical replica from an existing node. The remote dump is taken from the
 * start position of a slot on the remote end to ensure that we never replay
 * changes included in the dump and never miss changes.
 */
static void
bdr_init_exec_dump_restore(BDRNodeInfo * node, char *snapshot)
{
	char	    tmpdir[MAXPGPATH];
	char		bdr_dump_path[MAXPGPATH];
	char		bdr_restore_path[MAXPGPATH];
	StringInfo origin_dsn = makeStringInfo();
	StringInfo local_dsn = makeStringInfo();
	StringInfo	cmd = makeStringInfo();
	uint32		bin_version;

	if (bdr_find_other_exec(my_exec_path, BDR_DUMP_CMD, &bin_version,
							&bdr_dump_path[0]) < 0)
	{
		elog(ERROR, "BDR node init failed to find " BDR_DUMP_CMD
			 " relative to binary %s",
			 my_exec_path);
	}
	if (bin_version / 10000 != PG_VERSION_NUM / 10000)
	{
		elog(ERROR, "BDR node init found " BDR_DUMP_CMD
			 " with wrong major version %d.%d, expected %d.%d",
			 bin_version / 100 / 100, bin_version / 100 % 100,
			 PG_VERSION_NUM / 100 / 100, PG_VERSION_NUM / 100 % 100);
	}

	if (bdr_find_other_exec(my_exec_path, BDR_RESTORE_CMD, &bin_version,
							&bdr_restore_path[0]) < 0)
	{
		elog(ERROR, "BDR node init failed to find " BDR_RESTORE_CMD
			 " relative to binary %s",
			 my_exec_path);
	}
	if (bin_version / 10000 != PG_VERSION_NUM / 10000)
	{
		elog(ERROR, "BDR node init found " BDR_RESTORE_CMD
			 " with wrong major version %d.%d, expected %d.%d",
			 bin_version / 100 / 100, bin_version / 100 % 100,
			 PG_VERSION_NUM / 100 / 100, PG_VERSION_NUM / 100 % 100);
	}

	appendStringInfo(origin_dsn, "%s %s %s application_name='%s: init dump'",
					 bdr_default_apply_connection_options,
					 bdr_extra_apply_connection_options,
					 node->init_from_dsn,
					 bdr_get_my_cached_node_name());

	/*
	 * Suppress replication of changes applied via pg_restore back to the
	 * local node.
	 *
	 * TODO: This should PQconninfoParse, modify the options keyword or add
	 * it, and reconstruct the string using the functions from pg_dumpall
	 * (also to be used for init_copy). Simply appending the options instead
	 * is a bit dodgy.
	 */
	appendStringInfo(local_dsn, "%s application_name='%s: init restore' "
					 "options='-c bdr.do_not_replicate=on "
					 /* remove for now
					 "-c bdr.permit_unsafe_ddl_commands=on " */
					 "-c bdr.skip_ddl_replication=on "
					 /* remove for now
					 "-c bdr.skip_ddl_locking=on " */
					 "-c session_replication_role=replica'",
					 node->local_dsn,  bdr_get_my_cached_node_name());

	snprintf(tmpdir, sizeof(tmpdir), "%s/postgres-bdr-%s.%d",
			 bdr_temp_dump_directory, snapshot, getpid());

	if (MakePGDirectory(tmpdir) < 0)
	{
		int			save_errno = errno;

		if (save_errno == EEXIST)
		{
			/*
			 * Target is an existing dir that somehow wasn't cleaned up or
			 * something more sinister. We'll just die here, and let the
			 * postmaster relaunch us and retry the whole operation.
			 */
			elog(ERROR, "temporary dump directory %s already exists: %s",
				 tmpdir, strerror(save_errno));
		}
		else
			elog(ERROR, "failed to create temporary dump directory %s: %s",
				 tmpdir, strerror(save_errno));
	}

	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);
	BdrWorkerCtl->in_init_exec_dump_restore = true;
	LWLockRelease(BdrWorkerCtl->lock);

	PG_ENSURE_ERROR_CLEANUP(bdr_init_replica_cleanup_tmpdir,
							CStringGetDatum(tmpdir));
	{
		/* Get contents from remote node with pg_dump */
		appendStringInfo(cmd,
						 "%s -T \"bdr.bdr_nodes\" -T \"bdr.bdr_connections\" "
						 "--bdr-init-node --jobs=%d --snapshot=%s "
						 "--format=directory --file=%s \"%s\"",
						 bdr_dump_path,
						 bdr_init_node_parallel_jobs,
						 snapshot,
						 tmpdir,
						 origin_dsn->data);

		bdr_execute_command(cmd->data);
		resetStringInfo(cmd);

		/*
		 * Restore contents from remote node on to local node with pg_restore.
		 */
		appendStringInfo(cmd,
						 "%s --exit-on-error --jobs=%d --format=directory "
						 "--dbname=\"%s\" %s",
						 bdr_restore_path,
						 bdr_init_node_parallel_jobs,
						 local_dsn->data,
						 tmpdir);

		bdr_execute_command(cmd->data);
	}
	PG_END_ENSURE_ERROR_CLEANUP(bdr_init_replica_cleanup_tmpdir,
								PointerGetDatum(tmpdir));

	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);
	BdrWorkerCtl->in_init_exec_dump_restore = false;
	LWLockRelease(BdrWorkerCtl->lock);

	/* Clean up temporary directory we used for storing pg_dump. */
	bdr_init_replica_cleanup_tmpdir(0, CStringGetDatum(tmpdir));

	pfree(origin_dsn->data);
	pfree(origin_dsn);
	pfree(local_dsn->data);
	pfree(local_dsn);
	pfree(cmd->data);
	pfree(cmd);
}

/*
 * BDR state synchronization.
 */
static void
bdr_sync_nodes(PGconn *remote_conn, BDRNodeInfo * local_node)
{
	PGconn	   *local_conn;

	local_conn = bdr_connect_nonrepl(local_node->local_dsn, "init");

	PG_ENSURE_ERROR_CLEANUP(bdr_cleanup_conn_close,
							PointerGetDatum(&local_conn));
	{
		StringInfoData query;
		PGresult   *res;
		char		sysid_str[33];
		const char *const setup_query =
			"BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;\n"
			"SET LOCAL search_path = bdr, pg_catalog;\n"
			/* remove for now
			"SET LOCAL bdr.permit_unsafe_ddl_commands = on;\n" */
			"SET LOCAL bdr.skip_ddl_replication = on;\n"
			"LOCK TABLE bdr.bdr_nodes IN EXCLUSIVE MODE;\n"
			/* remove for now
			"SET LOCAL bdr.skip_ddl_locking = on;\n" */
			"LOCK TABLE bdr.bdr_connections IN EXCLUSIVE MODE;\n";

		/* Setup the environment. */
		res = PQexec(remote_conn, setup_query);
		if (PQresultStatus(res) != PGRES_COMMAND_OK)
			elog(ERROR, "BEGIN or table locking on remote failed: %s",
				 PQresultErrorMessage(res));
		PQclear(res);

		res = PQexec(local_conn, setup_query);
		if (PQresultStatus(res) != PGRES_COMMAND_OK)
			elog(ERROR, "BEGIN or table locking on local failed: %s",
				 PQresultErrorMessage(res));
		PQclear(res);

		/* Copy remote bdr_nodes entries to the local node. */
		bdr_copytable(remote_conn, local_conn,
					  "COPY (SELECT * FROM bdr.bdr_nodes) TO stdout",
					  "COPY bdr.bdr_nodes FROM stdin");

		/* Copy the local entry to remote node. */
		initStringInfo(&query);
		/* No need to quote as everything is numbers. */
		snprintf(sysid_str, sizeof(sysid_str), UINT64_FORMAT, local_node->id.sysid);
		appendStringInfo(&query,
						 "COPY (SELECT * FROM bdr.bdr_nodes WHERE "
						 "node_sysid = '%s' AND node_timeline = '%u' "
						 "AND node_dboid = '%u') TO stdout",
						 sysid_str, local_node->id.timeline, local_node->id.dboid);

		bdr_copytable(local_conn, remote_conn,
					  query.data, "COPY bdr.bdr_nodes FROM stdin");

		/*
		 * Copy remote connections to the local node.
		 *
		 * Adding local connection to remote node is handled separately
		 * because it triggers the connect-back process on the remote node(s).
		 */
		bdr_copytable(remote_conn, local_conn,
					  "COPY (SELECT * FROM bdr.bdr_connections) TO stdout",
					  "COPY bdr.bdr_connections FROM stdin");

		/* Save changes. */
		res = PQexec(remote_conn, "COMMIT");
		if (PQresultStatus(res) != PGRES_COMMAND_OK)
			elog(ERROR, "COMMIT on remote failed: %s",
				 PQresultErrorMessage(res));
		PQclear(res);

		res = PQexec(local_conn, "COMMIT");
		if (PQresultStatus(res) != PGRES_COMMAND_OK)
			elog(ERROR, "COMMIT on remote failed: %s",
				 PQresultErrorMessage(res));
		PQclear(res);
	}
	PG_END_ENSURE_ERROR_CLEANUP(bdr_cleanup_conn_close,
								PointerGetDatum(&local_conn));
	PQfinish(local_conn);
}

/*
 * Insert the bdr.bdr_nodes and bdr.bdr_connections entries for our node in the
 * remote peer, if they don't already exist.
 */
static void
bdr_insert_remote_conninfo(PGconn *conn, BdrConnectionConfig * myconfig)
{
#define _BDR_JOIN_NODE_PRIVATE 6
	PGresult   *res;
	Oid			types[_BDR_JOIN_NODE_PRIVATE] = {TEXTOID, OIDOID, OIDOID, TEXTOID, INT4OID, TEXTARRAYOID};
	const char *values[_BDR_JOIN_NODE_PRIVATE];
	StringInfoData replicationsets;

	/* Needs to fit max length of UINT64_FORMAT */
	char		sysid_str[33];
	char		tlid_str[33];
	char		mydatabaseid_str[33];
	char		apply_delay[33];

	initStringInfo(&replicationsets);

	stringify_my_node_identity(sysid_str, sizeof(sysid_str),
							   tlid_str, sizeof(tlid_str),
							   mydatabaseid_str, sizeof(mydatabaseid_str));

	values[0] = &sysid_str[0];
	values[1] = &tlid_str[0];
	values[2] = &mydatabaseid_str[0];
	values[3] = myconfig->dsn;

	snprintf(&apply_delay[0], 33, "%d", myconfig->apply_delay);
	values[4] = &apply_delay[0];

	/*
	 * Replication sets are stored as a quoted identifier list. To turn it
	 * into an array literal we can just wrap some brackets around it.
	 */
	appendStringInfo(&replicationsets, "{%s}", myconfig->replication_sets);
	values[5] = replicationsets.data;

	res = PQexecParams(conn,
					   "SELECT bdr._bdr_join_node_private($1,$2,$3,$4,$5,$6);",
					   _BDR_JOIN_NODE_PRIVATE,
					   types, &values[0], NULL, NULL, 0);

	/*
	 * bdr._bdr_join_node_private() must correctly handle unique violations.
	 * Otherwise init that resumes after slot creation, when we're waiting for
	 * inbound slots, will fail.
	 */
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
		elog(ERROR, "unable to update remote bdr.bdr_connections: %s",
			 PQerrorMessage(conn));

#undef _BDR_JOIN_NODE_PRIVATE
}

/*
 * Find all connections other than our own using the copy of
 * bdr.bdr_connections that we acquired from the remote server during
 * apply. Apply workers won't be started yet, we're just making the
 * slots.
 *
 * If the slot already exists from a prior attempt we'll leave it
 * alone. It'll be advanced when we start replaying from it anyway,
 * and it's guaranteed to retain more than the WAL we need.
 */
static void
bdr_init_make_other_slots()
{
	List	   *configs;
	ListCell   *lc;
	MemoryContext old_context;

	Assert(!IsTransactionState());
	StartTransactionCommand();
	old_context = MemoryContextSwitchTo(TopMemoryContext);
	configs = bdr_read_connection_configs();
	MemoryContextSwitchTo(old_context);
	CommitTransactionCommand();

	foreach(lc, configs)
	{
		BdrConnectionConfig *cfg = lfirst(lc);
		PGconn	   *conn;
		NameData	slot_name;
		BDRNodeId	remote,
					myid;
		RepOriginId replication_identifier;
		char	   *snapshot;

		bdr_make_my_nodeid(&myid);

		if (bdr_nodeid_eq(&cfg->remote_node, &myid))
		{
			/* Don't make a slot pointing to ourselves */
			continue;
			bdr_free_connection_config(cfg);
		}

		conn = bdr_establish_connection_and_slot(cfg->dsn, "mkslot", &slot_name,
												 &remote, &replication_identifier, &snapshot);

		/* Ensure the slot points to the node the conn info says it should */
		if (!bdr_nodeid_eq(&cfg->remote_node, &remote))
		{
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("system identification mismatch between connection and slot"),
					 errdetail("Connection for " BDR_NODEID_FORMAT_WITHNAME " resulted in slot on node " BDR_NODEID_FORMAT_WITHNAME " instead of expected node.",
							   BDR_NODEID_FORMAT_WITHNAME_ARGS(cfg->remote_node),
							   BDR_NODEID_FORMAT_WITHNAME_ARGS(remote))));
		}

		/* We don't require the snapshot IDs here */
		if (snapshot != NULL)
			pfree(snapshot);

		/* No replication for now, just close the connection */
		PQfinish(conn);

		elog(DEBUG2, "ensured existence of slot %s on " BDR_NODEID_FORMAT_WITHNAME,
			 NameStr(slot_name), BDR_NODEID_FORMAT_WITHNAME_ARGS(remote));

		bdr_free_connection_config(cfg);
	}

	list_free(configs);
}

/*
 * For each outbound connection in bdr.bdr_connections we should have a local
 * replication slot created by a remote node using our connection info.
 *
 * Wait until all such entries are created and active, then return.
 */
static void
bdr_init_wait_for_slot_creation()
{
	List	   *configs;
	ListCell   *lc;
#if PG_VERSION_NUM < 130000
	ListCell   *next,
			   *prev = NULL;
#endif
	BDRNodeId	myid;

	bdr_make_my_nodeid(&myid);

	elog(INFO, "waiting for all inbound slots to be established");

	/*
	 * Determine the list of expected slot identifiers. These are inbound
	 * slots, so they're our db oid + the remote's bdr ident.
	 */
	StartTransactionCommand();
	configs = bdr_read_connection_configs();

	/* Cleanup the config list from the ones we are not insterested in. */
#if PG_VERSION_NUM >= 130000
	foreach(lc, configs)
#else
	for (lc = list_head(configs); lc; lc = next)
#endif
	{
		BdrConnectionConfig *cfg = lfirst(lc);

		/* We might delete the cell so advance it now. */
#if PG_VERSION_NUM < 130000
		next = lnext(lc);
#endif
		/*
		 * We won't see an inbound slot from our own node.
		 */
		if (bdr_nodeid_eq(&cfg->remote_node, &myid))
		{
#if PG_VERSION_NUM >= 130000
			configs = foreach_delete_current(configs, lc);
#else
			configs = list_delete_cell(configs, lc, prev);
#endif
			break;
		}
		else
		{
#if PG_VERSION_NUM < 130000
			prev = lc;
#endif
		}
	}

	/*
	 * Wait for each slot to reach consistent point.
	 *
	 * This works by checking for BDR_WORKER_WALSENDER in the worker array.
	 * The reason for checking this way is that the worker structure for
	 * BDR_WORKER_WALSENDER is setup from startup_cb which is called after the
	 * consistent point was reached.
	 */
	while (true)
	{
		int			found = 0;
		int			slotoff;

		foreach(lc, configs)
		{
			BdrConnectionConfig *cfg = lfirst(lc);

			if (bdr_nodeid_eq(&cfg->remote_node, &myid))
			{
				/* We won't see an inbound slot from our own node */
				continue;
			}

			LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);
			for (slotoff = 0; slotoff < bdr_max_workers; slotoff++)
			{
				BdrWorker  *w = &BdrWorkerCtl->slots[slotoff];

				if (w->worker_type != BDR_WORKER_WALSENDER)
					continue;

				if (bdr_nodeid_eq(&cfg->remote_node, &w->data.walsnd.remote_node) &&
					w->worker_proc &&
					w->worker_proc->databaseId == MyDatabaseId)
					found++;
			}
			LWLockRelease(BdrWorkerCtl->lock);
		}

		if (found == list_length(configs))
			break;

		elog(DEBUG2, "found %u of %u expected slots, sleeping",
			 (uint32) found, (uint32) list_length(configs));

		pg_usleep(100000);
		CHECK_FOR_INTERRUPTS();
	}

	CommitTransactionCommand();

	elog(INFO, "all inbound slots established");
}

/*
 * Explicitly take the DDL lock on a remote peer.
 *
 * Can run standalone or in an existing tx, doesn't care about tx state.
 *
 * Does nothing if the remote peer doesn't support explicit DDL lock requests.
 *
 * ERRORs if the lock attempt fails. Caller should be prepared to retry
 * the attempt or the whole operations containing it.
 */
static void
bdr_ddl_lock_remote(PGconn *conn, BDRLockType mode)
{
	PGresult   *res;

	/* Currently only supports BDR_LOCK_DDL mode 'cos I'm lazy */
	if (mode != BDR_LOCK_DDL)
		elog(ERROR, "remote DDL locking only supports mode = 'ddl'");

	res = PQexec(conn,
				 "DO LANGUAGE plpgsql $$\n"
				 "BEGIN\n"
				 "	IF EXISTS (SELECT 1 FROM pg_proc WHERE proname = 'bdr_acquire_global_lock' AND pronamespace = (SELECT oid FROM pg_namespace WHERE nspname = 'bdr')) THEN\n"
				 "		PERFORM bdr.bdr_acquire_global_lock('ddl_lock');\n"
				 "	END IF;\n"
				 "END; $$;\n");

	if (PQresultStatus(res) != PGRES_COMMAND_OK)
	{
		PQclear(res);
		elog(ERROR, "failed to acquire global DDL lock on remote peer: %s",
			 PQerrorMessage(conn));
	}

	PQclear(res);
}

/*
 * While holding the global ddl lock on the remote, update bdr.bdr_nodes
 * status to 'r' on the join target. See callsite for more info.
 *
 * This function can leave a tx open and aborted on failure, but the
 * caller is assumed to just close the conn on failure anyway.
 *
 * Note that we set the global sequence ID from here too.
 *
 * Since bdr_init_copy creates nodes in state BDR_NODE_STATUS_CATCHUP,
 * we'll run this for both logically and physically joined nodes.
 */
static void
bdr_nodes_set_remote_status_ready(PGconn *conn)
{
	PGresult   *res;
	char	   *values[3];
	char		local_sysid[32],
				local_timeline[32],
				local_dboid[32];
	int			node_seq_id;

	res = PQexec(conn, "BEGIN ISOLATION LEVEL READ COMMITTED;");
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
	{
		PQclear(res);
		elog(ERROR, "failed to start tx on remote peer: %s", PQerrorMessage(conn));
	}

	bdr_ddl_lock_remote(conn, BDR_LOCK_DDL);

	/* DDL lock renders this somewhat redundant but you can't be too careful */
	res = PQexec(conn, "LOCK TABLE bdr.bdr_nodes IN EXCLUSIVE MODE;");

	stringify_my_node_identity(local_sysid, sizeof(local_sysid),
							   local_timeline, sizeof(local_timeline),
							   local_dboid, sizeof(local_dboid));
	values[0] = &local_sysid[0];
	values[1] = &local_timeline[0];
	values[2] = &local_dboid[0];

	/*
	 * Update our node status to 'r'eady, and grab the lowest free node
	 * node_seq_id in the process.
	 *
	 * It's safe to claim a node_seq_id from a 'k'illed node because we won't
	 * be replaying new changes from it once we see that status and the ID
	 * generator is based on timestamps.
	 */
	res = PQexecParams(conn,
					   "UPDATE bdr.bdr_nodes\n"
					   "SET node_status = " BDR_NODE_STATUS_READY_S ",\n"
					   "    node_seq_id = coalesce(\n"
					   "         -- lowest free ID if one has been released (right anti-join)\n"
					   "         (select min(x)\n"
					   "          from\n"
					   "            (select * from bdr.bdr_nodes where node_status not in (" BDR_NODE_STATUS_KILLED_S ")) n\n"
					   "            right join generate_series(1, (select max(n2.node_seq_id) from bdr.bdr_nodes n2)) s(x)\n"
					   "              on (n.node_seq_id = x)\n"
					   "            where n.node_seq_id is null),\n"
					   "         -- otherwise next-greatest ID\n"
					   "         (select coalesce(max(node_seq_id),0) + 1 from bdr.bdr_nodes where node_status not in (" BDR_NODE_STATUS_KILLED_S ")))\n"
					   "WHERE (node_sysid, node_timeline, node_dboid) = ($1, $2, $3)\n"
					   "RETURNING node_seq_id\n",
					   3, NULL, (const char **) values, NULL, NULL, 0);

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		PQclear(res);
		elog(ERROR, "failed to update my bdr.bdr_nodes entry on remote server: %s",
			 PQerrorMessage(conn));
	}

	if (PQntuples(res) != 1)
	{
		PQclear(res);
		elog(ERROR, "failed to update my bdr.bdr_nodes entry on remote server: affected %d rows instead of expected 1",
			 PQntuples(res));
	}

	Assert(PQnfields(res) == 1);

	if (PQgetisnull(res, 0, 0))
	{
		PQclear(res);
		elog(ERROR, "assigned node sequence ID is unexpectedly null");
	}

	node_seq_id = atoi(PQgetvalue(res, 0, 0));

	elog(DEBUG1, "BDR node finishing join assigned global seq id %d",
		 node_seq_id);

	res = PQexec(conn, "COMMIT;");
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
	{
		PQclear(res);
		elog(ERROR, "failed to start tx on remote peer: %s",
			 PQerrorMessage(conn));
	}
}

/*
 * Idle until our local node status goes 'r'
 */
static void
bdr_wait_for_local_node_ready()
{
	BdrNodeStatus status = BDR_NODE_STATUS_NONE;
	BDRNodeId	myid;

	bdr_make_my_nodeid(&myid);

	while (status != BDR_NODE_STATUS_READY)
	{
		int			rc;

		rc = WaitLatch(&MyProc->procLatch,
					   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
					   1000, PG_WAIT_EXTENSION);

		ResetLatch(&MyProc->procLatch);

		/* emergency bailout if postmaster has died */
		if (rc & WL_POSTMASTER_DEATH)
			proc_exit(1);

		CHECK_FOR_INTERRUPTS();

		StartTransactionCommand();
		SPI_connect();
		PushActiveSnapshot(GetTransactionSnapshot());
		status = bdr_nodes_get_local_status(&myid, false);
		PopActiveSnapshot();
		SPI_finish();
		CommitTransactionCommand();

		if (status == BDR_NODE_STATUS_KILLED)
		{
			ereport(ERROR,
					(errcode(ERRCODE_OPERATOR_INTERVENTION),
					 errmsg("local node has been detached from the BDR group (status=%c)", status)));
		}
	};
}

/*
 * TODO DYNCONF perform_pointless_transaction
 *
 * This is temporary code to be removed when the full detach/join protocol is
 * introduced, at which point WAL messages should handle this. See comments on
 * call site.
 */
static void
perform_pointless_transaction(PGconn *conn, BDRNodeInfo * node)
{
	PGresult   *res;

	res = PQexec(conn, "CREATE TEMP TABLE bdr_init(a int) ON COMMIT DROP");
	Assert(PQresultStatus(res) == PGRES_COMMAND_OK);
	PQclear(res);
}

/*
 * Set a standalone node, i.e one that's not initializing from another peer, to
 * ready state and assign it a node sequence ID.
 */
static void
bdr_init_standalone_node(BDRNodeInfo * local_node)
{
	int			seq_id = 1;
	Relation	rel;

	Assert(local_node->init_from_dsn == NULL);

	StartTransactionCommand();
	rel = table_open(BdrNodesRelid, ExclusiveLock);
	bdr_nodes_set_local_attrs(BDR_NODE_STATUS_READY, BDR_NODE_STATUS_BEGINNING_INIT, &seq_id);
	table_close(rel, ExclusiveLock);
	CommitTransactionCommand();
}

/*
 * Initialize the database, from a remote node if necessary.
 */
void
bdr_init_replica(BDRNodeInfo * local_node)
{
	BdrNodeStatus status;
	PGconn	   *nonrepl_init_conn;
	StringInfoData dsn;
	BdrConnectionConfig *local_conn_config;

	initStringInfo(&dsn);

	status = local_node->status;

	Assert(status != BDR_NODE_STATUS_READY);

	elog(DEBUG2, "initializing database in bdr_init_replica");

	/*
	 * The local SPI transaction we're about to perform must do any writes as
	 * a local transaction, not as a changeset application from a remote node.
	 * That allows rows to be replicated to other nodes. So no
	 * replorigin_session_origin may be set.
	 */
	Assert(replorigin_session_origin == InvalidRepOriginId);

	/*
	 * Before starting workers we must determine if we need to copy initial
	 * state from a remote node. This is necessary unless we are the first
	 * node created or we've already completed init. If we'd already completed
	 * init we would've exited above.
	 */
	if (local_node->init_from_dsn == NULL)
	{
		if (status != BDR_NODE_STATUS_BEGINNING_INIT)
		{
			/*
			 * Even though there's no init_replica worker, the local
			 * bdr.bdr_nodes table has an entry for our (sysid,dbname) and it
			 * isn't status=r (checked above), this should never happen
			 */
			ereport(ERROR,
					(errmsg("bdr.bdr_nodes row with " BDR_NODEID_FORMAT_WITHNAME " exists and has status=%c, but has init_from_dsn set to NULL",
							BDR_LOCALID_FORMAT_WITHNAME_ARGS, status)));
		}

		/*
		 * No connections have init_replica=t, so there's no remote copy to
		 * do, but we still have some work to do to bring up the first / a
		 * standalone node.
		 */
		bdr_init_standalone_node(local_node);

		return;
	}

	local_conn_config = bdr_get_connection_config(&local_node->id, true);

	if (!local_conn_config)
		elog(ERROR, "cannot find local BDR connection configurations");

	elog(DEBUG1, "init_replica init from remote %s",
		 local_node->init_from_dsn);

	nonrepl_init_conn =
		bdr_connect_nonrepl(local_node->init_from_dsn, "init");

	PG_ENSURE_ERROR_CLEANUP(bdr_cleanup_conn_close,
							PointerGetDatum(&nonrepl_init_conn));
	{
		bdr_ensure_ext_installed(nonrepl_init_conn);

		switch (status)
		{
			case BDR_NODE_STATUS_BEGINNING_INIT:
				elog(DEBUG2, "initializing from clean state");
				break;

			case BDR_NODE_STATUS_READY:
				elog(ERROR, "unexpected state");
				break;

			case BDR_NODE_STATUS_CATCHUP:

				/*
				 * We were in catchup mode when we died. We need to resume
				 * catchup mode up to the expected LSN before switching over.
				 *
				 * To do that all we need to do is fall through without doing
				 * any slot re-creation, dump/apply, etc, and pick up where we
				 * do catchup.
				 *
				 * We won't know what the original catchup target point is,
				 * but we can just catch up to whatever xlog position the
				 * server is currently at, it's guaranteed to be later than
				 * the target position.
				 */
				elog(DEBUG2, "dump applied, need to continue catchup");
				break;

			case BDR_NODE_STATUS_CREATING_OUTBOUND_SLOTS:
				elog(DEBUG2, "dump applied and catchup completed, need to continue slot creation");
				break;

			case BDR_NODE_STATUS_COPYING_INITIAL_DATA:

				/*
				 * A previous init attempt seems to have failed. Clean up,
				 * then fall through to start setup again.
				 *
				 * We can't just re-use the slot and replication identifier
				 * that were created last time (if they were), because we have
				 * no way of getting the slot's exported snapshot after
				 * CREATE_REPLICATION_SLOT.
				 *
				 * We could drop and re-create the slot, but...
				 *
				 * We also have no way to undo a failed pg_restore, so if that
				 * phase fails it's necessary to do manual cleanup, dropping
				 * and re-creating the db.
				 *
				 * To avoid that We need to be able to run pg_restore --clean,
				 * and that needs a way to exclude the bdr schema, the bdr
				 * extension, and their dependencies like plpgsql. (TODO patch
				 * pg_restore for that)
				 */
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						 errmsg("previous init failed, manual cleanup is required"),
						 errdetail("Found bdr.bdr_nodes entry for " BDR_NODEID_FORMAT_WITHNAME " with state=i in remote bdr.bdr_nodes.", BDR_LOCALID_FORMAT_WITHNAME_ARGS),
						 errhint("Remove all replication identifiers and slots corresponding to this node from the init target node then drop and recreate this database and try again.")));
				break;

			default:
				elog(ERROR, "unreachable %c", status);	/* Unhandled case */
				break;
		}

		if (status == BDR_NODE_STATUS_BEGINNING_INIT)
		{
			char	   *init_snapshot = NULL;
			PGconn	   *init_repl_conn = NULL;
			NameData	slot_name;
			BDRNodeId	remote;
			RepOriginId repnodeid;

			elog(INFO, "initializing node");

			status = BDR_NODE_STATUS_COPYING_INITIAL_DATA;
			bdr_nodes_set_local_status(status, BDR_NODE_STATUS_BEGINNING_INIT);

			/*
			 * Force the node to read-only while we initialize. This is
			 * persistent, so it'll stay read only through restarts and
			 * retries until we finish init.
			 */
			StartTransactionCommand();
			bdr_set_node_read_only_guts(local_node->name, true, true);
			CommitTransactionCommand();

			/*
			 * Now establish our slot on the target node, so we can replay
			 * changes from that node. It'll be used in catchup mode.
			 */
			init_repl_conn = bdr_establish_connection_and_slot(
															   local_node->init_from_dsn,
															   "init", &slot_name,
															   &remote, &repnodeid, &init_snapshot);

			elog(INFO, "connected to target node " BDR_NODEID_FORMAT_WITHNAME
				 " with snapshot %s",
				 BDR_NODEID_FORMAT_WITHNAME_ARGS(remote), init_snapshot);

			/*
			 * Take the remote dump and apply it. This will give us a local
			 * copy of bdr_connections to work from. It's guaranteed that
			 * everything after this dump will be accessible via the catchup
			 * mode slot created earlier.
			 */
			bdr_init_exec_dump_restore(local_node, init_snapshot);

			/*
			 * TODO DYNCONF copy replication identifier state
			 *
			 * Should copy the target node's
			 * pg_catalog.pg_replication_identifier state for each node to the
			 * local node, using the same snapshot we used to take the dump
			 * from the remote. Doing this ensures that when we create slots
			 * to the target nodes they'll begin replay from a position that's
			 * exactly consistent with what's in the dump.
			 *
			 * We'll still need catchup mode because there's no guarantee our
			 * newly created slots will force all WAL we'd need to be retained
			 * on each node. The target might be behind. So we should catchup
			 * replay until the replication identifier positions received from
			 * catchup are >= the creation positions of the slots we made.
			 *
			 * (We don't need to do this if we instead send a replay
			 * confirmation request and wait for a reply from each node.)
			 */

			PQfinish(init_repl_conn);
			pfree(init_snapshot);

			/*
			 * Copy the state (bdr_nodes and bdr_connections) over from the
			 * init node to our node.
			 */
			elog(DEBUG1, "syncing bdr_nodes and bdr_connections");
			bdr_sync_nodes(nonrepl_init_conn, local_node);

			status = BDR_NODE_STATUS_CATCHUP;
			bdr_nodes_set_local_status(status, BDR_NODE_STATUS_COPYING_INITIAL_DATA);
			elog(DEBUG1, "dump and apply finished, preparing for catchup replay");
		}

		Assert(status != BDR_NODE_STATUS_BEGINNING_INIT);

		if (status == BDR_NODE_STATUS_CATCHUP)
		{
			XLogRecPtr	min_remote_lsn;
			remote_node_info ri;

			/*
			 * Launch outbound connections to all other nodes. It doesn't
			 * matter that their slot horizons are after the dump was taken on
			 * the origin node, so we could never replay all the data we need
			 * if we switched to replaying from these slots now.  We'll be
			 * advancing them in catchup mode until they overtake their
			 * current position before switching to replaying from them
			 * directly.
			 *
			 * Note that while we create slots on the peers, they don't have
			 * bdr_connections or bdr_nodes entries for us yet, so we aren't
			 * counted in DDL locking votes. We aren't replaying from the
			 * peers yet so we won't see DDL lock requests or replies.
			 */
			bdr_init_make_other_slots();

			/*
			 *
			 * There's a small data desync risk here if an extremely laggy
			 * peer who commits a transaction before we create our slot on it,
			 * then the transaction isn't replicated to the join target node
			 * until we exit catchup mode. Acquiring the DDL lock before
			 * exiting catchup mode will fix this, since it forces all tx's
			 * committed before the DDL lock to be replicated to all peers. At
			 * this point we've created our slots so new tx's are guaranteed
			 * to be captured.
			 *
			 * TODO: This doesn't actually have to be a DDL lock. A round of
			 * replay confirmations is sufficient. But the only way we have to
			 * do that right now is a DDL lock.
			 */
			elog(DEBUG3, "forcing all peers to flush pending transactions");
			bdr_ddl_lock_remote(nonrepl_init_conn, BDR_LOCK_DDL);

			/*
			 * Enter catchup mode and wait until we've replayed up to the LSN
			 * the remote was at when we started catchup.
			 */
			elog(DEBUG3, "getting LSN to replay to in catchup mode");
			min_remote_lsn = bdr_get_remote_lsn(nonrepl_init_conn);

			/*
			 * Catchup cannot complete if there isn't at least one remote
			 * transaction to replay. So we perform a dummy transaction on the
			 * target node.
			 *
			 * XXX This is a hack. What we really *should* be doing is asking
			 * the target node to send a catchup confirmation wal message,
			 * then wait until all its current peers (we aren' one yet) reply
			 * with confirmation. Then we should be replaying until we get
			 * confirmation of this from the init target node, rather than
			 * replaying to some specific LSN. The full detach/join protocol
			 * should take care of this.
			 */
			elog(DEBUG3, "forcing a new transaction on the target node");
			perform_pointless_transaction(nonrepl_init_conn, local_node);

			bdr_get_remote_nodeinfo_internal(nonrepl_init_conn, &ri);

			/* Launch the catchup worker and wait for it to finish */
			elog(DEBUG1, "launching catchup mode apply worker");
			bdr_catchup_to_lsn(&ri, min_remote_lsn);

			free_remote_node_info(&ri);

			/*
			 * We're done with catchup. The next phase is inserting our
			 * conninfo, so set status=o
			 */
			status = BDR_NODE_STATUS_CREATING_OUTBOUND_SLOTS;
			bdr_nodes_set_local_status(status, BDR_NODE_STATUS_CATCHUP);
			elog(DEBUG1, "catchup worker finished, requesting slot creation");
		}

		/* To reach here we must be waiting for slot creation */
		Assert(status == BDR_NODE_STATUS_CREATING_OUTBOUND_SLOTS);

		/*
		 * It is now safe to start apply workers, as we've finished catchup.
		 * Doing so ensures that we will replay our own bdr.bdr_nodes changes
		 * from the target node and also makes sure we stay more up-to-date,
		 * reducing slot lag on other nodes.
		 *
		 * We now start seeing DDL lock requests from peers, but they still
		 * don't expect us to reply or really know about us yet.
		 */
		bdr_maintain_db_workers();

		/*
		 * Insert our connection info on the remote end. This will prompt the
		 * other end to connect back to us and make a slot, and will cause the
		 * other nodes to do the same when the new nodes and connections rows
		 * are replicated to them.
		 *
		 * We're still staying out of DDL locking. Our bdr_nodes entry on the
		 * peer is still in 'i' state and won't be counted in DDL locking
		 * quorum votes. To make sure we don't throw off voting we must ensure
		 * that we do not reply to DDL locking requests received from peers
		 * past this point. (TODO XXX FIXME)
		 */
		elog(DEBUG1, "inserting our connection into into remote end");
		bdr_insert_remote_conninfo(nonrepl_init_conn, local_conn_config);

		/*
		 * Wait for all outbound and inbound slot creation to be complete.
		 *
		 * The inbound slots aren't yet required to relay local writes to
		 * remote nodes, but they'll be used to write our catchup confirmation
		 * request WAL message, so we need them to exist.
		 */
		elog(DEBUG1, "waiting for all inbound slots to be created");
		bdr_init_wait_for_slot_creation();

		/*
		 * To make sure that we don't cause issues with any concurrent DDL
		 * locking operation that may be in progress on the BDR group we're
		 * joining we acquire the DDL lock on the target when we update our
		 * nodes entry to 'r'eady state. When peers see our node go ready
		 * they'll start counting it in tallies, so we must have full
		 * bi-directional communication. The new nodes row will be immediately
		 * followed by a DDL lock release message generated when its tx
		 * commits.
		 *
		 * It's fine that during this replay phase some nodes know about us
		 * and some don't. Those that don't yet know about us still have the
		 * local DDL lock held and will reject DDL lock requests from other
		 * peers. Those that do know about us will properly count us when
		 * tallying lock replies or replay confirmations. Nodes that haven't
		 * released their DDL lock won't send us any DDL lock requests or
		 * replay confirmations so we don't have to worry that they don't
		 * count us in their total node count yet.
		 *
		 * If we crash here we'll repeat this phase, but it's all idempotent
		 * so that's fine.
		 *
		 * As a side-effect, while we hold the DDL lock when setting the node
		 * status we'll also assign the lowest free node sequence ID.
		 */
		bdr_nodes_set_remote_status_ready(nonrepl_init_conn);
		status = BDR_NODE_STATUS_READY;

		/*
		 * We now have inbound and outbound slots for all nodes, and we're
		 * caught up to a reasonably recent state from the target node thanks
		 * to the dump and catchup mode operation.
		 */
		bdr_wait_for_local_node_ready();
		StartTransactionCommand();
		bdr_set_node_read_only_guts(local_node->name, false, true);
		CommitTransactionCommand();

		elog(INFO, "finished init_replica, ready to enter normal replication");
	}
	PG_END_ENSURE_ERROR_CLEANUP(bdr_cleanup_conn_close,
								PointerGetDatum(&nonrepl_init_conn));

	Assert(status == BDR_NODE_STATUS_READY);

	PQfinish(nonrepl_init_conn);
}

/*
 * Cleanup function after catchup; makes sure we free the bgworker
 * slot for the catchup worker.
 */
static void
bdr_catchup_to_lsn_cleanup(int code, Datum offset)
{
	uint32		worker_shmem_idx = DatumGetInt32(offset);

	/*
	 * Clear the worker's shared memory struct now we're done with it.
	 *
	 * There's no need to unregister the worker as it was registered with
	 * BGW_NEVER_RESTART.
	 */
	bdr_worker_shmem_free(&BdrWorkerCtl->slots[worker_shmem_idx], NULL);
}

/*
 * Launch a temporary apply worker in catchup mode (forward_changesets=t),
 * set to replay until the passed LSN.
 *
 * This worker will receive and apply all changes the remote server has
 * received since the snapshot we got our dump from was taken, including
 * those from other servers, and will advance the replication identifiers
 * associated with each remote node appropriately.
 *
 * When we finish applying and the worker exits, we'll be caught up with the
 * remote and in a consistent state where all our local replication identifiers
 * are consistent with the actual state of the local DB.
 */
static void
bdr_catchup_to_lsn(remote_node_info * ri, XLogRecPtr target_lsn)
{
	uint32		worker_shmem_idx;
	BdrWorker  *worker;
	BdrApplyWorker *catchup_worker;

	elog(DEBUG1, "registering BDR apply catchup worker for " BDR_NODEID_FORMAT_WITHNAME " to lsn %X/%X",
		 BDR_NODEID_FORMAT_WITHNAME_ARGS(ri->nodeid),
		 LSN_FORMAT_ARGS(target_lsn));

	Assert(bdr_worker_type == BDR_WORKER_PERDB);
	/* Create the shmem entry for the catchup worker */
	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);
	worker = bdr_worker_shmem_alloc(BDR_WORKER_APPLY, &worker_shmem_idx);
	catchup_worker = &worker->data.apply;
	catchup_worker->dboid = MyDatabaseId;
	bdr_nodeid_cpy(&catchup_worker->remote_node, &ri->nodeid);
	catchup_worker->perdb = bdr_worker_slot;
	LWLockRelease(BdrWorkerCtl->lock);

	/*
	 * Launch the catchup worker, ensuring that we free the shmem slot for the
	 * catchup worker even if we hit an error.
	 *
	 * There's a small race between claiming the worker and entering the
	 * ensure cleanup block. The real consequences are pretty much nil, since
	 * this is really just startup code and all we leak is one shmem slot.
	 */
	PG_ENSURE_ERROR_CLEANUP(bdr_catchup_to_lsn_cleanup,
							Int32GetDatum(worker_shmem_idx));
	{
		BgwHandleStatus bgw_status;
		BackgroundWorker bgw = {0};
		BackgroundWorkerHandle *bgw_handle;
		pid_t		bgw_pid;
		pid_t		prev_bgw_pid = 0;
		uint32		worker_arg;

		/* Special parameters for a catchup worker only */
		catchup_worker->replay_stop_lsn = target_lsn;
		catchup_worker->forward_changesets = true;

		/* Configure catchup worker, which is a regular apply worker */
		bgw.bgw_flags = BGWORKER_SHMEM_ACCESS |
			BGWORKER_BACKEND_DATABASE_CONNECTION;
		bgw.bgw_start_time = BgWorkerStart_RecoveryFinished;
		snprintf(bgw.bgw_library_name, BGW_MAXLEN, BDR_LIBRARY_NAME);
		snprintf(bgw.bgw_function_name, BGW_MAXLEN, "bdr_apply_main");
		snprintf(bgw.bgw_name, BGW_MAXLEN, "bdr apply worker for catchup to %X/%X",
				 LSN_FORMAT_ARGS(target_lsn));
		snprintf(bgw.bgw_type, BGW_MAXLEN, "bdr apply worker for catchup");
		bgw.bgw_restart_time = BGW_NEVER_RESTART;
		bgw.bgw_notify_pid = MyProcPid;
		Assert(worker_shmem_idx <= UINT16_MAX);
		worker_arg = (((uint32) BdrWorkerCtl->worker_generation) << 16) | (uint32) worker_shmem_idx;
		bgw.bgw_main_arg = Int32GetDatum(worker_arg);

		/* Launch the catchup worker and wait for it to start */
		RegisterDynamicBackgroundWorker(&bgw, &bgw_handle);
		bgw_status = WaitForBackgroundWorkerStartup(bgw_handle, &bgw_pid);
		prev_bgw_pid = bgw_pid;

		/*
		 * Sleep on our latch until we're woken by SIGUSR1 on bgworker state
		 * change, or by timeout. (We need a timeout because there's a race
		 * between bgworker start and our setting the latch; if it starts and
		 * dies again quickly we'll miss it and sleep forever w/o a timeout).
		 */
		while (bgw_status == BGWH_STARTED && bgw_pid == prev_bgw_pid)
		{
			int			rc;

			rc = WaitLatch(&MyProc->procLatch,
						   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
						   1000L, PG_WAIT_EXTENSION);

			ResetLatch(&MyProc->procLatch);

			/* emergency bailout if postmaster has died */
			if (rc & WL_POSTMASTER_DEATH)
				proc_exit(1);

			CHECK_FOR_INTERRUPTS();

			/* Is our worker still replaying? */
			bgw_status = GetBackgroundWorkerPid(bgw_handle, &bgw_pid);
		}
		switch (bgw_status)
		{
			case BGWH_POSTMASTER_DIED:
				proc_exit(1);
				break;
			case BGWH_STOPPED:
				TerminateBackgroundWorker(bgw_handle);
				break;
			case BGWH_NOT_YET_STARTED:
			case BGWH_STARTED:
				/* Should be unreachable */
				elog(ERROR, "unreachable case, bgw status %d", bgw_status);
				break;
		}
		pfree(bgw_handle);

		/*
		 * Stopped doesn't mean *successful*. The worker might've errored out.
		 * We have no way of getting its exit status, so we have to rely on it
		 * setting something in shmem on successful exit. In this case it will
		 * set replay_stop_lsn to InvalidXLogRecPtr to indicate that replay is
		 * done.
		 */
		if (catchup_worker->replay_stop_lsn != InvalidXLogRecPtr)
		{
			/* Worker must've died before it finished */
			elog(ERROR,
				 "catchup worker exited before catching up to target LSN %X/%X",
				 LSN_FORMAT_ARGS(target_lsn));
		}
		else
			elog(DEBUG1, "catchup worker caught up to target LSN");
	}
	PG_END_ENSURE_ERROR_CLEANUP(bdr_catchup_to_lsn_cleanup,
								Int32GetDatum(worker_shmem_idx));

	bdr_catchup_to_lsn_cleanup(0, Int32GetDatum(worker_shmem_idx));

	/* We're caught up! */
}
