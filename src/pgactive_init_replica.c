/* -------------------------------------------------------------------------
 *
 * pgactive_init_replica.c
 *     Populate a new pgactive node from the data in an existing node
 *
 * Use dump and restore, then pgactive catchup mode, to bring up a new
 * pgactive node into a pgactive group. Allows a new blank database to be
 * introduced into an existing, already-working pgactive group.
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_init_replica.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "pgactive.h"
#include "pgactive_internal.h"
#include "pgactive_locks.h"

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
#include "postmaster/fork_process.h"

#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/pg_lsn.h"
#include "utils/snapmgr.h"
#include "pgstat.h"

char	   *pgactive_temp_dump_directory = NULL;

static void pgactive_execute_command(const char *cmd, char *cmdargv[]);
static void pgactive_get_replication_set_tables(pgactiveNodeInfo * node,
												pgactiveNodeId * remote,
												PGconn *conn,
												List **tables,
												bool *is_include_set,
												bool *is_exclude_set);
static void pgactive_init_exec_dump_restore(pgactiveNodeInfo * node,
											char *snapshot,
											pgactiveNodeId * remote,
											PGconn *conn);
static void pgactive_catchup_to_lsn(remote_node_info * ri, XLogRecPtr target_lsn);

static XLogRecPtr
pgactive_get_remote_lsn(PGconn *conn)
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
pgactive_get_remote_ext_version(PGconn *pgconn, char **default_version,
								char **installed_version)
{
	PGresult   *res;

	const char *q_pgactive_installed =
		"SELECT default_version, installed_version "
		"FROM pg_catalog.pg_available_extensions WHERE name = 'pgactive';";

	res = PQexec(pgconn, q_pgactive_installed);

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		elog(ERROR, "unable to get remote pgactive extension version; query %s failed with %s: %s",
			 q_pgactive_installed, PQresStatus(PQresultStatus(res)), PQresultErrorMessage(res));
	}

	if (PQntuples(res) == 1)
	{
		/*
		 * pgactive ext is known to Pg, check install state.
		 */
		*default_version = pstrdup(PQgetvalue(res, 0, 0));
		*installed_version = pstrdup(PQgetvalue(res, 0, 0));
	}
	else if (PQntuples(res) == 0)
	{
		/* pgactive ext is not known to Pg at all */
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
 * Make sure the pgactive extension is installed on the other end. If it's a known
 * extension but not present in the current DB error out and tell the user to
 * activate pgactive then try again.
 */
void
pgactive_ensure_ext_installed(PGconn *pgconn)
{
	char	   *default_version = NULL;
	char	   *installed_version = NULL;

	pgactive_get_remote_ext_version(pgconn, &default_version, &installed_version);

	if (default_version == NULL || strcmp(default_version, "") == 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("remote PostgreSQL install for pgactive connection does not have pgactive extension installed"),
				 errdetail("No entry with name 'pgactive' in pg_available_extensions."),
				 errhint("You need to install the pgactive extension on the remote end.")));
	}

	if (installed_version == NULL || strcmp(installed_version, "") == 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("remote database for pgactive connection does not have the pgactive extension active"),
				 errdetail("installed_version for entry 'pgactive' in pg_available_extensions is blank."),
				 errhint("Run 'CREATE EXTENSION pgactive;'.")));
	}

	pfree(default_version);
	pfree(installed_version);
}

/*
 * Function to execute a given commnd.
 *
 * Any sort of failure in command execution is a FATAL error so that
 * postmaster will just start the per-db worker again.
 */
static void
pgactive_execute_command(const char *cmd, char *cmdargv[])
{
	pid_t		pid;
	int			exitstatus;

#ifdef WIN32

	/*
	 * TODO: on Windows we should be using CreateProcessEx instead of fork()
	 * and exec(). We should add an abstraction for this to port/ eventually,
	 * so this code doesn't have to care about the platform.
	 */
	ereport(ERROR,
			(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			 errmsg("init_replica isn't supported on Windows yet")));
#endif

	pid = fork_process();

	if (pid == 0)				/* child */
	{
		if (execv(cmd, cmdargv) < 0)
		{
			ereport(LOG,
					(errmsg("could not execute command \"%s\": %m",
							cmd)));
			/* We're already in the child process here, can't return */
			exit(1);
		}
	}

	if (pid < 0)
	{
		/* in parent, fork failed */
		ereport(ERROR,
				(errmsg("could not fork new process to execute command \"%s\" for init_replica: %m",
						cmd)));
	}

	/* in parent, successful fork */
	ereport(LOG,
			(errmsg("waiting for process %d to execute command \"%s\" for init_replica",
					(int) pid, cmd)));

	while (1)
	{
		pid_t		res;

		res = waitpid(pid, &exitstatus, WNOHANG);

		if (res == pid)
			break;
		else if (res == -1 && errno != EINTR)
			elog(FATAL, "error in waitpid() while waiting for process %d",
				 pid);

		(void) pgactiveWaitLatch(&MyProc->procLatch,
								 WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
								 1000L, PG_WAIT_EXTENSION);
		ResetLatch(&MyProc->procLatch);
		CHECK_FOR_INTERRUPTS();
	}

	if (exitstatus != 0)
	{
		StringInfoData buf;

		initStringInfo(&buf);

		if (WIFEXITED(exitstatus))
			appendStringInfo(&buf, "process %d to execute command \"%s\" for init_replica exited with exit code %d",
							 pid, cmd, WEXITSTATUS(exitstatus));
		else if (WIFSIGNALED(exitstatus))
			appendStringInfo(&buf, "process %d to execute command \"%s\" for init_replica exited due to signal %d",
							 pid, cmd, WEXITSTATUS(exitstatus));
		else
			appendStringInfo(&buf, "process %d to execute command \"%s\" for init_replica exited for an unknown reason with exit code %d",
							 pid, cmd, exitstatus);

		/* Set error message info */
		LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);
		memcpy(pgactiveWorkerCtl->errormsg_buf, buf.data, buf.len);
		pgactiveWorkerCtl->errormsg_buf[buf.len] = '\0';
		LWLockRelease(pgactiveWorkerCtl->lock);

		elog(FATAL, "%s", buf.data);
	}

	ereport(LOG,
			(errmsg("successfully executed command \"%s\" for init_replica",
					cmd)));
}

static void
pgactive_get_replication_set_tables(pgactiveNodeInfo * node,
									pgactiveNodeId * remote,
									PGconn *conn,
									List **tables,
									bool *is_include_set,
									bool *is_exclude_set)
{
	StringInfo	cmd = makeStringInfo();
	PGresult   *res;
	int			i;
	char	   *remote_node_name;

	*tables = NIL;
	*is_include_set = false;
	*is_exclude_set = false;

	appendStringInfo(cmd,
					 "SELECT node_name FROM pgactive.pgactive_nodes WHERE "
					 "node_sysid='" UINT64_FORMAT "' AND node_timeline=%u AND node_dboid=%d;",
					 remote->sysid, remote->timeline, remote->dboid);

	res = PQexec(conn, cmd->data);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
		elog(ERROR, "could not get remote node name: %s",
			 PQerrorMessage(conn));

	if (PQntuples(res) == 0)
		goto done;

	remote_node_name = pstrdup(PQgetvalue(res, 0, 0));

	PQclear(res);
	resetStringInfo(cmd);

	appendStringInfo(cmd,
					 "SELECT unnest(rsets) FROM pgactive.pgactive_get_connection_replication_sets('%s') AS rsets;",
					 remote_node_name);

	res = PQexec(conn, cmd->data);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
		elog(ERROR, "could not get connection replication sets: %s",
			 PQerrorMessage(conn));

	if (PQntuples(res) == 0)
		goto done;

	for (i = 0; i < PQntuples(res); i++)
	{
		char	   *rset;

		rset = PQgetvalue(res, i, 0);

		if (pg_strcasecmp(rset, pgactive_INCLUDE_REPLICATION_SET_NAME) == 0)
			*is_include_set = true;
		else if (pg_strcasecmp(rset, pgactive_EXCLUDE_REPLICATION_SET_NAME) == 0)
			*is_exclude_set = true;
	}

	pfree(remote_node_name);

	if (*is_include_set == true && *is_exclude_set == true)
	{
		/* Both include set and exclude set can't co-exist for a given node */
		elog(ERROR, "cannot have both include and exclude replication sets defined for node " pgactive_NODEID_FORMAT,
			 pgactive_NODEID_FORMAT_ARGS(*remote));
	}
	else if (*is_include_set == false && *is_exclude_set == false)
	{
		/* Exit when no include set or exclude set exist for a given node */
		goto done;
	}

	PQclear(res);
	resetStringInfo(cmd);

	if (*is_include_set == true)
		appendStringInfo(cmd,
						 "SELECT pgactive.pgactive_get_replication_set_tables(ARRAY['%s']) AS relnames;",
						 pgactive_INCLUDE_REPLICATION_SET_NAME);
	else if (*is_exclude_set == true)
		appendStringInfo(cmd,
						 "SELECT pgactive.pgactive_get_replication_set_tables(ARRAY['%s']) AS relnames;",
						 pgactive_EXCLUDE_REPLICATION_SET_NAME);

	res = PQexec(conn, cmd->data);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
		elog(ERROR, "could not get replication set tables: %s",
			 PQerrorMessage(conn));

	if (PQntuples(res) == 0)
		goto done;

	for (i = 0; i < PQntuples(res); i++)
	{
		char	   *table;

		table = PQgetvalue(res, i, 0);
		*tables = lappend(*tables, pstrdup(table));
	}

done:
	PQclear(res);
	pfree(cmd->data);
	pfree(cmd);
}

/*
 * Copy the contents of a remote node using pg_dump and apply it to the local
 * node using pg_restore. Runs during node join creation to bring up a new
 * logical replica from an existing node. The remote dump is taken from the
 * start position of a slot on the remote end to ensure that we never replay
 * changes included in the dump and never miss changes.
 */
static void
pgactive_init_exec_dump_restore(pgactiveNodeInfo * node,
								char *snapshot,
								pgactiveNodeId * remote,
								PGconn *conn)
{
	char		tmpdir[MAXPGPATH];
	char		pgactive_dump_path[MAXPGPATH];
	char		pgactive_restore_path[MAXPGPATH];
	StringInfo	origin_dsn = makeStringInfo();
	StringInfo	local_dsn = makeStringInfo();
	uint32		bin_version;
	char	   *o_servername;
	char	   *l_servername;
	char	   *cmdargv[20];
	int			cmdargc;
	char		arg_jobs[12];
	char		arg_tmp1[MAXPGPATH];
	char		arg_tmp2[MAXPGPATH];
	List	   *tables = NIL;
	ListCell   *lc;
	bool		is_include_set = false;
	bool		is_exclude_set = false;
	List	   *table_args = NIL;

	if (pgactive_find_other_exec(my_exec_path, pgactive_DUMP_CMD, &bin_version,
								 &pgactive_dump_path[0]) < 0)
	{
		elog(ERROR, "pgactive node init failed to find " pgactive_DUMP_CMD
			 " relative to binary %s",
			 my_exec_path);
	}
	if (bin_version / 10000 != PG_VERSION_NUM / 10000)
	{
		elog(ERROR, "pgactive node init found " pgactive_DUMP_CMD
			 " with wrong major version %d.%d, expected %d.%d",
			 bin_version / 100 / 100, bin_version / 100 % 100,
			 PG_VERSION_NUM / 100 / 100, PG_VERSION_NUM / 100 % 100);
	}

	if (pgactive_find_other_exec(my_exec_path, pgactive_RESTORE_CMD, &bin_version,
								 &pgactive_restore_path[0]) < 0)
	{
		elog(ERROR, "pgactive node init failed to find " pgactive_RESTORE_CMD
			 " relative to binary %s",
			 my_exec_path);
	}
	if (bin_version / 10000 != PG_VERSION_NUM / 10000)
	{
		elog(ERROR, "pgactive node init found " pgactive_RESTORE_CMD
			 " with wrong major version %d.%d, expected %d.%d",
			 bin_version / 100 / 100, bin_version / 100 % 100,
			 PG_VERSION_NUM / 100 / 100, PG_VERSION_NUM / 100 % 100);
	}

	o_servername = get_connect_string(node->init_from_dsn);
	appendStringInfo(origin_dsn, "%s %s %s application_name='%s: init dump'",
					 pgactive_default_apply_connection_options,
					 pgactive_extra_apply_connection_options,
					 (o_servername == NULL ? node->init_from_dsn : o_servername),
					 pgactive_get_my_cached_node_name());

	/*
	 * Suppress replication of changes applied via pg_restore back to the
	 * local node.
	 *
	 * TODO: This should PQconninfoParse, modify the options keyword or add
	 * it, and reconstruct the string using the functions from pg_dumpall
	 * (also to be used for init_copy). Simply appending the options instead
	 * is a bit dodgy.
	 */
	l_servername = get_connect_string(node->local_dsn);
	appendStringInfo(local_dsn, "%s application_name='%s: init restore' "
					 "options='-c pgactive.do_not_replicate=on "

	/*
	 * remove for now "-c pgactive.permit_unsafe_ddl_commands=on "
	 */
					 "-c pgactive.skip_ddl_replication=on "

	/*
	 * remove for now "-c pgactive.skip_ddl_locking=on "
	 */
					 "-c session_replication_role=replica'",
					 (l_servername == NULL ? node->local_dsn : l_servername), pgactive_get_my_cached_node_name());

	snprintf(tmpdir, sizeof(tmpdir), "%s/%s-" UINT64_FORMAT "-%s.%d",
			 pgactive_temp_dump_directory, TEMP_DUMP_DIR_PREFIX,
			 GetSystemIdentifier(), snapshot, getpid());

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

	PG_ENSURE_ERROR_CLEANUP(destroy_temp_dump_dir,
							CStringGetDatum(tmpdir));
	{
		/* Get contents from remote node with pg_dump */
		snprintf(arg_jobs, sizeof(arg_jobs), "--jobs=%d", pgactive_init_node_parallel_jobs);
		snprintf(arg_tmp1, sizeof(arg_tmp1), "--snapshot=%s", snapshot);
		snprintf(arg_tmp2, sizeof(arg_tmp2), "--file=%s", tmpdir);

		cmdargc = 0;
		cmdargv[cmdargc++] = pgactive_dump_path;
		cmdargv[cmdargc++] = "--exclude-table=pgactive.pgactive_nodes";
		cmdargv[cmdargc++] = "--exclude-table=pgactive.pgactive_connections";
		cmdargv[cmdargc++] = "--pgactive-init-node";
		cmdargv[cmdargc++] = arg_jobs;
		cmdargv[cmdargc++] = arg_tmp1;
		cmdargv[cmdargc++] = "--format=directory";
		cmdargv[cmdargc++] = arg_tmp2;
		cmdargv[cmdargc++] = origin_dsn->data;

		if (pgactive_get_data_only_node_init(MyDatabaseId))
			cmdargv[cmdargc++] = "--data-only";

		pgactive_get_replication_set_tables(node, remote, conn, &tables,
											&is_include_set, &is_exclude_set);

		if (is_include_set || is_exclude_set)
		{
			foreach(lc, tables)
			{
				char	   *table = (char *) lfirst(lc);
				char		table_arg[2 * NAMEDATALEN]; /* For option name +
														 * table name */
				char	   *ptr;

				if (is_include_set)
					snprintf(table_arg, (2 * NAMEDATALEN), "--table=%s", table);
				else if (is_exclude_set)
					snprintf(table_arg, (2 * NAMEDATALEN), "--exclude-table=%s", table);

				ptr = pstrdup(table_arg);
				table_args = lappend(table_args, ptr);
				cmdargv[cmdargc++] = ptr;
			}
		}

		cmdargv[cmdargc++] = NULL;

		pgactive_execute_command(pgactive_dump_path, cmdargv);

		list_free_deep(tables);
		tables = NIL;
		list_free_deep(table_args);
		table_args = NIL;

		/*
		 * We don't need this flag anymore after the dump finishes, so reset
		 * it
		 */
		pgactive_set_data_only_node_init(MyDatabaseId, false);

		/*
		 * Restore contents from remote node on to local node with pg_restore.
		 */

		snprintf(arg_jobs, sizeof(arg_jobs), "--jobs=%d", pgactive_init_node_parallel_jobs);
		snprintf(arg_tmp1, sizeof(arg_tmp1), "--dbname=%s", local_dsn->data);

		cmdargc = 0;
		cmdargv[cmdargc++] = pgactive_restore_path;
		cmdargv[cmdargc++] = "--exit-on-error";
		cmdargv[cmdargc++] = arg_jobs;
		cmdargv[cmdargc++] = "--format=directory";
		cmdargv[cmdargc++] = arg_tmp1;
		cmdargv[cmdargc++] = tmpdir;
		cmdargv[cmdargc++] = NULL;

		pgactive_execute_command(pgactive_restore_path, cmdargv);
	}
	PG_END_ENSURE_ERROR_CLEANUP(destroy_temp_dump_dir,
								PointerGetDatum(tmpdir));

	/* Destroy temporary directory we used for storing pg_dump. */
	destroy_temp_dump_dir(0, CStringGetDatum(tmpdir));

	pfree(origin_dsn->data);
	pfree(origin_dsn);
	pfree(local_dsn->data);
	pfree(local_dsn);
}

/*
 * pgactive state synchronization.
 */
static void
pgactive_sync_nodes(PGconn *remote_conn, pgactiveNodeInfo * local_node)
{
	PGconn	   *local_conn;

	local_conn = pgactive_connect_nonrepl(local_node->local_dsn, "init", true);

	PG_ENSURE_ERROR_CLEANUP(pgactive_cleanup_conn_close,
							PointerGetDatum(&local_conn));
	{
		StringInfoData query;
		PGresult   *res;
		char		sysid_str[33];
		const char *const setup_query =
			"BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;\n"
			"SET LOCAL search_path = pgactive, pg_catalog;\n"

		/*
		 * remove for now "SET LOCAL pgactive.permit_unsafe_ddl_commands =
		 * on;\n"
		 */
			"SET LOCAL pgactive.skip_ddl_replication = on;\n"
			"LOCK TABLE pgactive.pgactive_nodes IN EXCLUSIVE MODE;\n"

		/*
		 * remove for now "SET LOCAL pgactive.skip_ddl_locking = on;\n"
		 */
			"LOCK TABLE pgactive.pgactive_connections IN EXCLUSIVE MODE;\n";

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

		/* Copy remote pgactive_nodes entries to the local node. */
		pgactive_copytable(remote_conn, local_conn,
						   "COPY (SELECT * FROM pgactive.pgactive_nodes) TO stdout",
						   "COPY pgactive.pgactive_nodes FROM stdin");

		/* Copy the local entry to remote node. */
		initStringInfo(&query);
		/* No need to quote as everything is numbers. */
		snprintf(sysid_str, sizeof(sysid_str), UINT64_FORMAT, local_node->id.sysid);
		appendStringInfo(&query,
						 "COPY (SELECT * FROM pgactive.pgactive_nodes WHERE "
						 "node_sysid = '%s' AND node_timeline = '%u' "
						 "AND node_dboid = '%u') TO stdout",
						 sysid_str, local_node->id.timeline, local_node->id.dboid);

		pgactive_copytable(local_conn, remote_conn,
						   query.data, "COPY pgactive.pgactive_nodes FROM stdin");

		pfree(query.data);

		/*
		 * Copy remote connections to the local node.
		 *
		 * Adding local connection to remote node is handled separately
		 * because it triggers the connect-back process on the remote node(s).
		 */
		pgactive_copytable(remote_conn, local_conn,
						   "COPY (SELECT * FROM pgactive.pgactive_connections) TO stdout",
						   "COPY pgactive.pgactive_connections FROM stdin");

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
	PG_END_ENSURE_ERROR_CLEANUP(pgactive_cleanup_conn_close,
								PointerGetDatum(&local_conn));
	PQfinish(local_conn);
}

/*
 * Insert the pgactive.pgactive_nodes and pgactive.pgactive_connections entries for our node in the
 * remote peer, if they don't already exist.
 */
static void
pgactive_insert_remote_conninfo(PGconn *conn, pgactiveConnectionConfig * myconfig)
{
#define _pgactive_JOIN_NODE_PRIVATE 6
	PGresult   *res;
	Oid			types[_pgactive_JOIN_NODE_PRIVATE] = {TEXTOID, OIDOID, OIDOID, TEXTOID, INT4OID, TEXTARRAYOID};
	const char *values[_pgactive_JOIN_NODE_PRIVATE];
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
					   "SELECT pgactive._pgactive_join_node_private($1,$2,$3,$4,$5,$6);",
					   _pgactive_JOIN_NODE_PRIVATE,
					   types, &values[0], NULL, NULL, 0);

	/*
	 * pgactive._pgactive_join_node_private() must correctly handle unique
	 * violations. Otherwise init that resumes after slot creation, when we're
	 * waiting for inbound slots, will fail.
	 */
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
		elog(ERROR, "unable to update remote pgactive.pgactive_connections: %s",
			 PQerrorMessage(conn));

#undef _pgactive_JOIN_NODE_PRIVATE
}

/*
 * Find all connections other than our own using the copy of
 * pgactive.pgactive_connections that we acquired from the remote server during
 * apply. Apply workers won't be started yet, we're just making the
 * slots.
 *
 * If the slot already exists from a prior attempt we'll leave it
 * alone. It'll be advanced when we start replaying from it anyway,
 * and it's guaranteed to retain more than the WAL we need.
 */
static void
pgactive_init_make_other_slots()
{
	List	   *configs;
	ListCell   *lc;
	MemoryContext old_context;

	Assert(!IsTransactionState());
	StartTransactionCommand();
	old_context = MemoryContextSwitchTo(TopMemoryContext);
	configs = pgactive_read_connection_configs();
	MemoryContextSwitchTo(old_context);
	CommitTransactionCommand();

	foreach(lc, configs)
	{
		pgactiveConnectionConfig *cfg = lfirst(lc);
		PGconn	   *conn;
		NameData	slot_name;
		pgactiveNodeId remote,
					myid;

		pgactive_make_my_nodeid(&myid);

		if (pgactive_nodeid_eq(&cfg->remote_node, &myid))
		{
			/* Don't make a slot pointing to ourselves */
			continue;
			pgactive_free_connection_config(cfg);
		}

		conn = pgactive_establish_connection_and_slot(cfg->dsn,
													  "mkslot",
													  &slot_name,
													  &remote,
													  NULL,
													  NULL);

		/* Ensure the slot points to the node the conn info says it should */
		if (!pgactive_nodeid_eq(&cfg->remote_node, &remote))
		{
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("system identification mismatch between connection and slot"),
					 errdetail("Connection for " pgactive_NODEID_FORMAT_WITHNAME " resulted in slot on node " pgactive_NODEID_FORMAT_WITHNAME " instead of expected node.",
							   pgactive_NODEID_FORMAT_WITHNAME_ARGS(cfg->remote_node),
							   pgactive_NODEID_FORMAT_WITHNAME_ARGS(remote))));
		}

		/* No replication for now, just close the connection */
		PQfinish(conn);

		elog(DEBUG2, "ensured existence of slot %s on " pgactive_NODEID_FORMAT_WITHNAME,
			 NameStr(slot_name), pgactive_NODEID_FORMAT_WITHNAME_ARGS(remote));

		pgactive_free_connection_config(cfg);
	}

	list_free(configs);
}

/*
 * For each outbound connection in pgactive.pgactive_connections we should have a local
 * replication slot created by a remote node using our connection info.
 *
 * Wait until all such entries are created and active, then return.
 */
static void
pgactive_init_wait_for_slot_creation()
{
	List	   *configs;
	ListCell   *lc;
#if PG_VERSION_NUM < 130000
	ListCell   *next,
			   *prev = NULL;
#endif
	pgactiveNodeId myid;

	pgactive_make_my_nodeid(&myid);

	elog(INFO, "waiting for all inbound slots to be established");

	/*
	 * Determine the list of expected slot identifiers. These are inbound
	 * slots, so they're our db oid + the remote's pgactive ident.
	 */
	StartTransactionCommand();
	configs = pgactive_read_connection_configs();

	/* Cleanup the config list from the ones we are not insterested in. */
#if PG_VERSION_NUM >= 130000
	foreach(lc, configs)
#else
	for (lc = list_head(configs); lc; lc = next)
#endif
	{
		pgactiveConnectionConfig *cfg = lfirst(lc);

		/* We might delete the cell so advance it now. */
#if PG_VERSION_NUM < 130000
		next = lnext(lc);
#endif

		/*
		 * We won't see an inbound slot from our own node.
		 */
		if (pgactive_nodeid_eq(&cfg->remote_node, &myid))
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
	 * This works by checking for pgactive_WORKER_WALSENDER in the worker
	 * array. The reason for checking this way is that the worker structure
	 * for pgactive_WORKER_WALSENDER is setup from startup_cb which is called
	 * after the consistent point was reached.
	 */
	while (true)
	{
		int			found = 0;
		int			slotoff;

		foreach(lc, configs)
		{
			pgactiveConnectionConfig *cfg = lfirst(lc);

			if (pgactive_nodeid_eq(&cfg->remote_node, &myid))
			{
				/* We won't see an inbound slot from our own node */
				continue;
			}

			LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);
			for (slotoff = 0; slotoff < pgactive_max_workers; slotoff++)
			{
				pgactiveWorker *w = &pgactiveWorkerCtl->slots[slotoff];

				if (w->worker_type != pgactive_WORKER_WALSENDER)
					continue;

				if (pgactive_nodeid_eq(&cfg->remote_node, &w->data.walsnd.remote_node) &&
					w->worker_proc &&
					w->worker_proc->databaseId == MyDatabaseId)
					found++;
			}
			LWLockRelease(pgactiveWorkerCtl->lock);
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
pgactive_ddl_lock_remote(PGconn *conn, pgactiveLockType mode)
{
	PGresult   *res;

	/* Currently only supports pgactive_LOCK_DDL mode 'cos I'm lazy */
	if (mode != pgactive_LOCK_DDL)
		elog(ERROR, "remote DDL locking only supports mode = 'ddl'");

	res = PQexec(conn,
				 "DO LANGUAGE plpgsql $$\n"
				 "BEGIN\n"
				 "	IF EXISTS (SELECT 1 FROM pg_proc WHERE proname = 'pgactive_acquire_global_lock' AND pronamespace = (SELECT oid FROM pg_namespace WHERE nspname = 'pgactive')) THEN\n"
				 "		PERFORM pgactive.pgactive_acquire_global_lock('ddl_lock');\n"
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
 * While holding the global ddl lock on the remote, update pgactive.pgactive_nodes
 * status to 'r' on the join target. See callsite for more info.
 *
 * This function can leave a tx open and aborted on failure, but the
 * caller is assumed to just close the conn on failure anyway.
 *
 * Note that we set the global sequence ID from here too.
 *
 * Since pgactive_init_copy creates nodes in state pgactive_NODE_STATUS_CATCHUP,
 * we'll run this for both logically and physically joined nodes.
 */
static void
pgactive_nodes_set_remote_status_ready(PGconn *conn)
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

	pgactive_ddl_lock_remote(conn, pgactive_LOCK_DDL);

	/* DDL lock renders this somewhat redundant but you can't be too careful */
	res = PQexec(conn, "LOCK TABLE pgactive.pgactive_nodes IN EXCLUSIVE MODE;");

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
					   "UPDATE pgactive.pgactive_nodes\n"
					   "SET node_status = " pgactive_NODE_STATUS_READY_S ",\n"
					   "    node_seq_id = coalesce(\n"
					   "         -- lowest free ID if one has been released (right anti-join)\n"
					   "         (select min(x)\n"
					   "          from\n"
					   "            (select * from pgactive.pgactive_nodes where node_status not in (" pgactive_NODE_STATUS_KILLED_S ")) n\n"
					   "            right join generate_series(1, (select max(n2.node_seq_id) from pgactive.pgactive_nodes n2)) s(x)\n"
					   "              on (n.node_seq_id = x)\n"
					   "            where n.node_seq_id is null),\n"
					   "         -- otherwise next-greatest ID\n"
					   "         (select coalesce(max(node_seq_id),0) + 1 from pgactive.pgactive_nodes where node_status not in (" pgactive_NODE_STATUS_KILLED_S ")))\n"
					   "WHERE (node_sysid, node_timeline, node_dboid) = ($1, $2, $3)\n"
					   "RETURNING node_seq_id\n",
					   3, NULL, (const char **) values, NULL, NULL, 0);

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		PQclear(res);
		elog(ERROR, "failed to update my pgactive.pgactive_nodes entry on remote server: %s",
			 PQerrorMessage(conn));
	}

	if (PQntuples(res) != 1)
	{
		PQclear(res);
		elog(ERROR, "failed to update my pgactive.pgactive_nodes entry on remote server: affected %d rows instead of expected 1",
			 PQntuples(res));
	}

	Assert(PQnfields(res) == 1);

	if (PQgetisnull(res, 0, 0))
	{
		PQclear(res);
		elog(ERROR, "assigned node sequence ID is unexpectedly null");
	}

	node_seq_id = atoi(PQgetvalue(res, 0, 0));

	elog(DEBUG1, "pgactive node finishing join assigned global seq id %d",
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
pgactive_wait_for_local_node_ready()
{
	pgactiveNodeStatus status = pgactive_NODE_STATUS_NONE;
	pgactiveNodeId myid;

	pgactive_make_my_nodeid(&myid);

	while (status != pgactive_NODE_STATUS_READY)
	{
		(void) pgactiveWaitLatch(&MyProc->procLatch,
								 WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
								 1000L, PG_WAIT_EXTENSION);
		ResetLatch(&MyProc->procLatch);
		CHECK_FOR_INTERRUPTS();

		StartTransactionCommand();
		SPI_connect();
		PushActiveSnapshot(GetTransactionSnapshot());
		status = pgactive_nodes_get_local_status(&myid, false);
		PopActiveSnapshot();
		SPI_finish();
		CommitTransactionCommand();

		if (status == pgactive_NODE_STATUS_KILLED)
		{
			ereport(ERROR,
					(errcode(ERRCODE_OPERATOR_INTERVENTION),
					 errmsg("local node has been detached from the pgactive group (status=%c)", status)));
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
perform_pointless_transaction(PGconn *conn, pgactiveNodeInfo * node)
{
	PGresult   *res;

	res = PQexec(conn, "CREATE TEMP TABLE pgactive_init(a int) ON COMMIT DROP");
	Assert(PQresultStatus(res) == PGRES_COMMAND_OK);
	PQclear(res);
}

/*
 * Set a standalone node, i.e one that's not initializing from another peer, to
 * ready state and assign it a node sequence ID.
 */
static void
pgactive_init_standalone_node(pgactiveNodeInfo * local_node)
{
	int			seq_id = 1;
	Relation	rel;

	Assert(local_node->init_from_dsn == NULL);

	StartTransactionCommand();
	rel = table_open(pgactiveNodesRelid, ExclusiveLock);
	pgactive_nodes_set_local_attrs(pgactive_NODE_STATUS_READY, pgactive_NODE_STATUS_BEGINNING_INIT, &seq_id);
	table_close(rel, ExclusiveLock);
	CommitTransactionCommand();
}

/*
 * Initialize the database, from a remote node if necessary.
 */
void
pgactive_init_replica(pgactiveNodeInfo * local_node)
{
	pgactiveNodeStatus status;
	PGconn	   *nonrepl_init_conn;
	pgactiveConnectionConfig *local_conn_config;

	status = local_node->status;

	Assert(status != pgactive_NODE_STATUS_READY);

	elog(DEBUG2, "initializing database in pgactive_init_replica");

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
		if (status != pgactive_NODE_STATUS_BEGINNING_INIT)
		{
			/*
			 * Even though there's no init_replica worker, the local
			 * pgactive.pgactive_nodes table has an entry for our
			 * (sysid,dbname) and it isn't status=r (checked above), this
			 * should never happen
			 */
			ereport(ERROR,
					(errmsg("pgactive.pgactive_nodes row with " pgactive_NODEID_FORMAT_WITHNAME " exists and has status=%c, but has init_from_dsn set to NULL",
							pgactive_LOCALID_FORMAT_WITHNAME_ARGS, status)));
		}

		/*
		 * No connections have init_replica=t, so there's no remote copy to
		 * do, but we still have some work to do to bring up the first / a
		 * standalone node.
		 */
		pgactive_init_standalone_node(local_node);

		return;
	}

	local_conn_config = pgactive_get_connection_config(&local_node->id, true);

	if (!local_conn_config)
		elog(ERROR, "cannot find local pgactive connection configurations");

	elog(DEBUG1, "init_replica init from remote %s",
		 local_node->init_from_dsn);

	nonrepl_init_conn =
		pgactive_connect_nonrepl(local_node->init_from_dsn, "init", true);

	PG_ENSURE_ERROR_CLEANUP(pgactive_cleanup_conn_close,
							PointerGetDatum(&nonrepl_init_conn));
	{
		pgactive_ensure_ext_installed(nonrepl_init_conn);

		switch (status)
		{
			case pgactive_NODE_STATUS_BEGINNING_INIT:
				elog(DEBUG2, "initializing from clean state");
				break;

			case pgactive_NODE_STATUS_READY:
				elog(ERROR, "unexpected state");
				break;

			case pgactive_NODE_STATUS_CATCHUP:

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

			case pgactive_NODE_STATUS_CREATING_OUTBOUND_SLOTS:
				elog(DEBUG2, "dump applied and catchup completed, need to continue slot creation");
				break;

			case pgactive_NODE_STATUS_COPYING_INITIAL_DATA:

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
				 * and that needs a way to exclude the pgactive schema, the
				 * pgactive extension, and their dependencies like plpgsql.
				 * (TODO patch pg_restore for that)
				 */
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						 errmsg("previous init failed, manual cleanup is required"),
						 errdetail("Found pgactive.pgactive_nodes entry for " pgactive_NODEID_FORMAT_WITHNAME " with state=i in remote pgactive.pgactive_nodes.", pgactive_LOCALID_FORMAT_WITHNAME_ARGS),
						 errhint("Remove all replication identifiers and slots corresponding to this node from the init target node then drop and recreate this database and try again.")));
				break;

			default:
				elog(ERROR, "unreachable %c", status);	/* Unhandled case */
				break;
		}

		/* Reset previous error message info */
		LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);
		pgactiveWorkerCtl->errormsg_buf[0] = '\0';
		LWLockRelease(pgactiveWorkerCtl->lock);

		if (status == pgactive_NODE_STATUS_BEGINNING_INIT)
		{
			char		init_snapshot[NAMEDATALEN] = {0};
			PGconn	   *init_repl_conn = NULL;
			NameData	slot_name;
			pgactiveNodeId remote;

			elog(INFO, "initializing node");

			status = pgactive_NODE_STATUS_COPYING_INITIAL_DATA;
			pgactive_nodes_set_local_status(status, pgactive_NODE_STATUS_BEGINNING_INIT);

			/*
			 * Force the node to read-only while we initialize. This is
			 * persistent, so it'll stay read only through restarts and
			 * retries until we finish init.
			 */
			StartTransactionCommand();
			pgactive_set_node_read_only_guts(local_node->name, true, true);
			CommitTransactionCommand();

			/*
			 * Now establish our slot on the target node, so we can replay
			 * changes from that node. It'll be used in catchup mode.
			 */
			init_repl_conn =
				pgactive_establish_connection_and_slot(local_node->init_from_dsn,
													   "init",
													   &slot_name,
													   &remote,
													   NULL,
													   init_snapshot);

			elog(INFO, "connected to target node " pgactive_NODEID_FORMAT_WITHNAME
				 " with snapshot %s",
				 pgactive_NODEID_FORMAT_WITHNAME_ARGS(remote), init_snapshot);

			/*
			 * Take the remote dump and apply it. This will give us a local
			 * copy of pgactive_connections to work from. It's guaranteed that
			 * everything after this dump will be accessible via the catchup
			 * mode slot created earlier.
			 */
			pgactive_init_exec_dump_restore(local_node, init_snapshot,
											&remote, nonrepl_init_conn);

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

			/*
			 * Copy the state (pgactive_nodes and pgactive_connections) over
			 * from the init node to our node.
			 */
			elog(DEBUG1, "syncing pgactive_nodes and pgactive_connections");
			pgactive_sync_nodes(nonrepl_init_conn, local_node);

			status = pgactive_NODE_STATUS_CATCHUP;
			pgactive_nodes_set_local_status(status, pgactive_NODE_STATUS_COPYING_INITIAL_DATA);
			elog(DEBUG1, "dump and apply finished, preparing for catchup replay");
		}

		Assert(status != pgactive_NODE_STATUS_BEGINNING_INIT);

		if (status == pgactive_NODE_STATUS_CATCHUP)
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
			 * pgactive_connections or pgactive_nodes entries for us yet, so
			 * we aren't counted in DDL locking votes. We aren't replaying
			 * from the peers yet so we won't see DDL lock requests or
			 * replies.
			 */
			pgactive_init_make_other_slots();

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
			pgactive_ddl_lock_remote(nonrepl_init_conn, pgactive_LOCK_DDL);

			/*
			 * Enter catchup mode and wait until we've replayed up to the LSN
			 * the remote was at when we started catchup.
			 */
			elog(DEBUG3, "getting LSN to replay to in catchup mode");
			min_remote_lsn = pgactive_get_remote_lsn(nonrepl_init_conn);

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

			pgactive_get_remote_nodeinfo_internal(nonrepl_init_conn, &ri);

			/* Launch the catchup worker and wait for it to finish */
			elog(DEBUG1, "launching catchup mode apply worker");
			pgactive_catchup_to_lsn(&ri, min_remote_lsn);

			free_remote_node_info(&ri);

			/*
			 * We're done with catchup. The next phase is inserting our
			 * conninfo, so set status=o
			 */
			status = pgactive_NODE_STATUS_CREATING_OUTBOUND_SLOTS;
			pgactive_nodes_set_local_status(status, pgactive_NODE_STATUS_CATCHUP);
			elog(DEBUG1, "catchup worker finished, requesting slot creation");
		}

		/* To reach here we must be waiting for slot creation */
		Assert(status == pgactive_NODE_STATUS_CREATING_OUTBOUND_SLOTS);

		/*
		 * It is now safe to start apply workers, as we've finished catchup.
		 * Doing so ensures that we will replay our own
		 * pgactive.pgactive_nodes changes from the target node and also makes
		 * sure we stay more up-to-date, reducing slot lag on other nodes.
		 *
		 * We now start seeing DDL lock requests from peers, but they still
		 * don't expect us to reply or really know about us yet.
		 */
		pgactive_maintain_db_workers();

		/*
		 * Insert our connection info on the remote end. This will prompt the
		 * other end to connect back to us and make a slot, and will cause the
		 * other nodes to do the same when the new nodes and connections rows
		 * are replicated to them.
		 *
		 * We're still staying out of DDL locking. Our pgactive_nodes entry on
		 * the peer is still in 'i' state and won't be counted in DDL locking
		 * quorum votes. To make sure we don't throw off voting we must ensure
		 * that we do not reply to DDL locking requests received from peers
		 * past this point. (TODO XXX FIXME)
		 */
		elog(DEBUG1, "inserting our connection into into remote end");
		pgactive_insert_remote_conninfo(nonrepl_init_conn, local_conn_config);

		/*
		 * Wait for all outbound and inbound slot creation to be complete.
		 *
		 * The inbound slots aren't yet required to relay local writes to
		 * remote nodes, but they'll be used to write our catchup confirmation
		 * request WAL message, so we need them to exist.
		 */
		elog(DEBUG1, "waiting for all inbound slots to be created");
		pgactive_init_wait_for_slot_creation();

		/*
		 * To make sure that we don't cause issues with any concurrent DDL
		 * locking operation that may be in progress on the pgactive group
		 * we're joining we acquire the DDL lock on the target when we update
		 * our nodes entry to 'r'eady state. When peers see our node go ready
		 * they'll start counting it in tallies, so we must have full
		 * active-active communication. The new nodes row will be immediately
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
		pgactive_nodes_set_remote_status_ready(nonrepl_init_conn);
		status = pgactive_NODE_STATUS_READY;

		/*
		 * We now have inbound and outbound slots for all nodes, and we're
		 * caught up to a reasonably recent state from the target node thanks
		 * to the dump and catchup mode operation.
		 */
		pgactive_wait_for_local_node_ready();
		StartTransactionCommand();
		pgactive_set_node_read_only_guts(local_node->name, false, true);
		CommitTransactionCommand();

		elog(INFO, "finished init_replica, ready to enter normal replication");
	}
	PG_END_ENSURE_ERROR_CLEANUP(pgactive_cleanup_conn_close,
								PointerGetDatum(&nonrepl_init_conn));

	Assert(status == pgactive_NODE_STATUS_READY);

	PQfinish(nonrepl_init_conn);
}

/*
 * Cleanup function after catchup; makes sure we free the bgworker slot for the
 * catchup worker.
 */
static void
pgactive_catchup_to_lsn_cleanup(int code, Datum offset)
{
	uint32		worker_shmem_idx = DatumGetInt32(offset);

	/*
	 * Clear the worker's shared memory struct now we're done with it.
	 *
	 * There's no need to unregister the worker as it was registered with
	 * BGW_NEVER_RESTART.
	 */
	pgactive_worker_shmem_free(&pgactiveWorkerCtl->slots[worker_shmem_idx],
							   NULL, true);
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
pgactive_catchup_to_lsn(remote_node_info * ri, XLogRecPtr target_lsn)
{
	uint32		worker_shmem_idx;
	pgactiveWorker *worker;
	pgactiveApplyWorker *catchup_worker;

	elog(DEBUG1, "registering pgactive apply catchup worker for " pgactive_NODEID_FORMAT_WITHNAME " to lsn %X/%X",
		 pgactive_NODEID_FORMAT_WITHNAME_ARGS(ri->nodeid),
		 LSN_FORMAT_ARGS(target_lsn));

	Assert(pgactive_worker_type == pgactive_WORKER_PERDB);
	/* Create the shmem entry for the catchup worker */
	LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);
	worker = pgactive_worker_shmem_alloc(pgactive_WORKER_APPLY, &worker_shmem_idx);
	catchup_worker = &worker->data.apply;
	catchup_worker->dboid = MyDatabaseId;
	pgactive_nodeid_cpy(&catchup_worker->remote_node, &ri->nodeid);
	catchup_worker->perdb = pgactive_worker_slot;
	LWLockRelease(pgactiveWorkerCtl->lock);

	/*
	 * Launch the catchup worker, ensuring that we free the shmem slot for the
	 * catchup worker even if we hit an error.
	 *
	 * There's a small race between claiming the worker and entering the
	 * ensure cleanup block. The real consequences are pretty much nil, since
	 * this is really just startup code and all we leak is one shmem slot.
	 */
	PG_ENSURE_ERROR_CLEANUP(pgactive_catchup_to_lsn_cleanup,
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
		snprintf(bgw.bgw_library_name, BGW_MAXLEN, pgactive_LIBRARY_NAME);
		snprintf(bgw.bgw_function_name, BGW_MAXLEN, "pgactive_apply_main");
		snprintf(bgw.bgw_name, BGW_MAXLEN, "pgactive apply worker for catchup to %X/%X",
				 LSN_FORMAT_ARGS(target_lsn));
		snprintf(bgw.bgw_type, BGW_MAXLEN, "pgactive apply worker for catchup");
		bgw.bgw_restart_time = BGW_NEVER_RESTART;
		bgw.bgw_notify_pid = MyProcPid;
		Assert(worker_shmem_idx <= UINT16_MAX);
		worker_arg = (((uint32) pgactiveWorkerCtl->worker_generation) << 16) | (uint32) worker_shmem_idx;
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
			(void) pgactiveWaitLatch(&MyProc->procLatch,
									 WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
									 1000L, PG_WAIT_EXTENSION);
			ResetLatch(&MyProc->procLatch);
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
	PG_END_ENSURE_ERROR_CLEANUP(pgactive_catchup_to_lsn_cleanup,
								Int32GetDatum(worker_shmem_idx));

	pgactive_catchup_to_lsn_cleanup(0, Int32GetDatum(worker_shmem_idx));

	/* We're caught up! */
}
