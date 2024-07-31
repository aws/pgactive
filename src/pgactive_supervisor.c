/* -------------------------------------------------------------------------
 *
 * pgactive_supervisor.c
 *		Cluster wide supervisor worker.
 *
 * Copyright (C) 2014-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_supervisor.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pgactive.h"

#include "miscadmin.h"
#include "pgstat.h"

#include "access/relscan.h"
#include "access/skey.h"
#include "access/xact.h"

#include "catalog/objectaddress.h"
#include "catalog/pg_database.h"
#include "catalog/pg_shseclabel.h"

#include "commands/dbcommands.h"
#include "commands/seclabel.h"

#include "libpq/libpq-be.h"

#include "postmaster/bgworker.h"

#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/proc.h"

#include "utils/builtins.h"
#include "utils/elog.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"

static bool destroy_temp_dump_dirs_callback_registered = false;

/*
 * Register a new perdb worker for a database. The worker MUST not already
 * exist.
 *
 * This is called by the supervisor during startup, and by user backends when
 * the first connection is added for a database.
 *
 * Returns true if the worker is started, otherwise false.
 */
static bool
pgactive_register_perdb_worker(Oid dboid)
{
	BackgroundWorkerHandle *bgw_handle;
	BackgroundWorker bgw = {0};
	BgwHandleStatus status;
	pid_t		pid;
	pgactiveWorker *worker;
	pgactivePerdbWorker *perdb;
	unsigned int worker_slot_number;
	uint32		worker_arg;
	char	   *dbname;

	Assert(LWLockHeldByMe(pgactiveWorkerCtl->lock));
	dbname = get_database_name(dboid);

	elog(LOG, "registering pgactive per-db worker for database \"%s\" with OID %u",
		 dbname, dboid);

	worker = pgactive_worker_shmem_alloc(pgactive_WORKER_PERDB,
										 &worker_slot_number);

	perdb = &worker->data.perdb;
	perdb->c_dboid = dboid;
	/* Node count is set when apply workers are registered */
	perdb->nnodes = -1;

	/*
	 * The rest of the perdb worker's shmem segment - proclatch and nnodes -
	 * gets set up by the worker during startup.
	 */

	/* Configure per-db worker */
	bgw.bgw_flags = BGWORKER_SHMEM_ACCESS |
		BGWORKER_BACKEND_DATABASE_CONNECTION;
	snprintf(bgw.bgw_library_name, BGW_MAXLEN, pgactive_LIBRARY_NAME);
	snprintf(bgw.bgw_function_name, BGW_MAXLEN, "pgactive_perdb_worker_main");
	snprintf(bgw.bgw_name, BGW_MAXLEN, "pgactive per-db worker for %s", dbname);
	snprintf(bgw.bgw_type, BGW_MAXLEN, "pgactive per-db worker");
	bgw.bgw_start_time = BgWorkerStart_RecoveryFinished;
	bgw.bgw_restart_time = 5;

	/* We want supervisor to be notified when the worker is started */
	bgw.bgw_notify_pid = MyProcPid;

	/*
	 * The main arg is composed of two uint16 parts - the worker generation
	 * number (see pgactive_worker_shmem_startup) and the index into
	 * pgactiveWorkerCtl->slots in shared memory.
	 */
	Assert(worker_slot_number <= UINT16_MAX);
	worker_arg = (((uint32) pgactiveWorkerCtl->worker_generation) << 16) | (uint32) worker_slot_number;
	bgw.bgw_main_arg = Int32GetDatum(worker_arg);

	if (!RegisterDynamicBackgroundWorker(&bgw, &bgw_handle))
	{
		/*
		 * If we can't register the per-db worker now, let's free up the
		 * worker slot in pgactive shared memory instead of emitting an error
		 * disturbing all other existing pgactive worker processes. Upon
		 * seeing the WARNING, users can act accordingly. The pgactive
		 * supervisor will anyway try registering the per-db worker in its
		 * next scan cycle.
		 */
		pgactive_worker_shmem_free(worker, NULL, false);

		ereport(WARNING,
				(errcode(ERRCODE_CONFIGURATION_LIMIT_EXCEEDED),
				 errmsg("could not register pgactive per-db worker for database \"%s\" with OID %u",
						dbname, dboid),
				 errhint("Consider increasing configuration parameter \"max_worker_processes\".")));

		return false;
	}

	elog(LOG, "successfully registered pgactive per-db worker for database \"%s\" with OID %u",
		 dbname, dboid);

	/*
	 * Here, supervisor must ensure the per-db worker registered above is
	 * started by postmaster and updated database oid in its shared memory
	 * slot. This is to avoid a race condition.
	 *
	 * Steps that can otherwise lead to the race condition are:
	 *
	 * 1. Supervisor registers per-db worker while holding
	 * pgactiveWorkerCtl->lock in pgactive_supervisor_rescan_dbs().
	 *
	 * 2. Started per-db worker needs pgactiveWorkerCtl->lock to update
	 * database oid in its shared memory slot and thus adds itself to lock's
	 * wait queue. Unless per-db worker updates database oid, supervisor
	 * cannot consider it started in find_perdb_worker_slot().
	 *
	 * 3. Supervisor releases the lock, but a waiter other than per-db worker
	 * acquires the lock. Meanwhile, the supervisor adds itself to the lock's
	 * wait queue, thanks to SetLatch() in pgactive_perdb_xact_callback().
	 *
	 * 4. Supervisor acquires the lock again before the first per-db worker
	 * and fails to find the first per-db worker in find_perdb_worker_slot()
	 * as it hasn't yet got a chance to update database oid in the shared
	 * memory slot. This makes supervisor register another per-db worker for
	 * the same pgactive-enabled database causing multiple per-db workers (and
	 * so multiple apply workers - each per-db worker starts an apply worker)
	 * to coexist. These multiple per-db workers don't let nodes joining the
	 * pgactive group to come out from catchup state to ready state.
	 *
	 * We fix this race condition by making supervisor register per-db worker,
	 * wait until postmaster starts it, give it a chance to update database
	 * oid in its shared memory slot and continue to scan for other
	 * pgactive-enabled databases. An assert-enabled function
	 * check_for_multiple_perdb_workers() helps to validate the fix.
	 */
	status = WaitForBackgroundWorkerStartup(bgw_handle, &pid);
	if (status != BGWH_STARTED)
	{
		/*
		 * If we can't register the per-db worker now, let's free up the
		 * worker slot in pgactive shared memory instead of emitting an error
		 * disturbing all other existing pgactive worker processes. Upon
		 * seeing the WARNING, users can act accordingly. The pgactive
		 * supervisor will anyway try registering the per-db worker in its
		 * next scan cycle.
		 */
		pgactive_worker_shmem_free(worker, NULL, false);

		ereport(WARNING,
				(errcode(ERRCODE_INSUFFICIENT_RESOURCES),
				 errmsg("could not start pgactive per-db worker for %s",
						dbname),
				 errhint("More details may be available in the server log.")));

		return false;
	}

	/*
	 * Wait for per-db worker to register itself in the worker's shared memory
	 * slot.
	 */
	for (;;)
	{
		LWLockRelease(pgactiveWorkerCtl->lock);

		(void) pgactiveWaitLatch(&MyProc->procLatch,
								 WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
								 100L, PG_WAIT_EXTENSION);
		ResetLatch(&MyProc->procLatch);
		CHECK_FOR_INTERRUPTS();

		if (ConfigReloadPending)
		{
			ConfigReloadPending = false;
			ProcessConfigFile(PGC_SIGHUP);
			/* set log_min_messages */
			SetConfigOption("log_min_messages", pgactive_error_severity(pgactive_log_min_messages),
							PGC_POSTMASTER, PGC_S_OVERRIDE);
		}

		LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);

		if (perdb->proclatch != NULL && perdb->p_dboid == dboid)
		{
			LWLockRelease(pgactiveWorkerCtl->lock);
			break;
		}
	}

	Assert(!LWLockHeldByMe(pgactiveWorkerCtl->lock));

	/* Re-acquire lock for the caller */
	LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);

	Assert(perdb->c_dboid == perdb->p_dboid);
	elog(LOG, "successfully started pgactive per-db worker for database \"%s\" with OID %u",
		 dbname, perdb->p_dboid);
	pfree(dbname);

	return true;
}

/*
 * Check for pgactive-enabled DBs and start per-db workers for any that currently
 * lack them.
 *
 * TODO DYNCONF: Handle removal of pgactive from DBs
 */
static void
pgactive_supervisor_rescan_dbs()
{
	Relation	secrel;
	ScanKeyData skey[2];
	SysScanDesc scan;
	HeapTuple	secTuple;
	int			n_new_workers = 0,
				pgactive_dbs = 0;

	elog(DEBUG1, "supervisor scanning for pgactive-enabled databases");

	pgstat_report_activity(STATE_RUNNING, "scanning backends");

	StartTransactionCommand();

	/*
	 * Scan pg_shseclabel looking for entries for pg_database with the
	 * pgactive label provider. We'll find all labels for the pgactive
	 * provider, irrespective of value.
	 *
	 * The only index present isn't much use for this scan and using it makes
	 * us set up more keys, so do a heap scan.
	 *
	 * The lock taken on pg_shseclabel must be strong enough to conflict with
	 * the lock taken be pgactive.pgactive_connection_add(...) to ensure that
	 * any transactions adding new labels have committed and cleaned up before
	 * we read it. Otherwise a race between the supervisor latch being set in
	 * a commit hook and the tuples actually becoming visible is possible.
	 */
	secrel = table_open(SharedSecLabelRelationId, RowShareLock);

	ScanKeyInit(&skey[0],
				Anum_pg_shseclabel_classoid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(DatabaseRelationId));

	ScanKeyInit(&skey[1],
				Anum_pg_shseclabel_provider,
				BTEqualStrategyNumber, F_TEXTEQ,
				CStringGetTextDatum(pgactive_SECLABEL_PROVIDER));

	scan = systable_beginscan(secrel, InvalidOid, false, NULL, 2, &skey[0]);

	/*
	 * We need to scan the shmem segment that tracks pgactive workers and
	 * possibly modify it, so lock it.
	 *
	 * We have to take an exclusive lock in case we need to modify it,
	 * otherwise we'd be faced with a lock upgrade.
	 */
	LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);

	/*
	 * Now examine each label and if there's no worker for the labled DB
	 * already, start one.
	 */
	while (HeapTupleIsValid(secTuple = systable_getnext(scan)))
	{
		FormData_pg_shseclabel *sec;
		int			slotno;

		sec = (FormData_pg_shseclabel *) GETSTRUCT(secTuple);

		if (!pgactive_is_pgactive_activated_db(sec->objoid))
			continue;

		/*
		 * While we are here, there's no problem even if the database is
		 * renamed. This is because we use OID based bg worker API (i.e.,
		 * every bg worker is mapped with database OID, not with database
		 * name), and database renaming doesn't change the OID.
		 */
		elog(DEBUG1, "found pgactive-enabled database with OID %u", sec->objoid);

		pgactive_dbs++;

		/*
		 * Check if we have a per-db worker for this db oid already and if we
		 * don't, start one.
		 *
		 * This is O(n^2) for n pgactive-enabled DBs; to be more scalable we
		 * could accumulate and sort the oids, then do a single scan of the
		 * shmem segment. But really, if you have that many DBs this cost is
		 * nothing.
		 */
		slotno = find_perdb_worker_slot(sec->objoid, NULL);

		if (slotno == pgactive_PER_DB_WORKER_SLOT_NOT_FOUND)
		{
			/*
			 * No perdb worker exists for this DB, try to start one. If we
			 * can't start now, try again in the next scan cycle.
			 */
			if (pgactive_register_perdb_worker(sec->objoid))
				n_new_workers++;

			Assert(LWLockHeldByMe(pgactiveWorkerCtl->lock));
		}
		else if (slotno >= pgactive_PER_DB_WORKER_SLOT_FOUND)
			elog(DEBUG1, "per-db worker for database with OID %u already exists, not registering",
				 sec->objoid);
		else if (slotno == pgactive_UNREGISTERED_PER_DB_WORKER_SLOT_FOUND)
			elog(DEBUG1, "per-db worker for database with OID %u was previously unregistered, not registering",
				 sec->objoid);
	}

	elog(DEBUG1, "found %i pgactive-labeled DBs; registered %i new per-db workers",
		 pgactive_dbs, n_new_workers);

	LWLockRelease(pgactiveWorkerCtl->lock);

	systable_endscan(scan);
	table_close(secrel, RowShareLock);

	CommitTransactionCommand();

	elog(DEBUG1, "finished scanning for pgactive-enabled databases");

	pgstat_report_activity(STATE_IDLE, NULL);
}

/*
 * Create the database the supervisor remains connected
 * to, a DB with no user connections permitted.
 *
 * This is a workaround for the inability to use pg_shseclabel
 * without a DB connection; see comments in pgactive_supervisor_worker_main
 */
static void
pgactive_supervisor_createdb()
{
	Oid			dboid;
	ParseState *pstate;

	StartTransactionCommand();

	/* If the DB already exists, no need to create it */
	dboid = get_database_oid(pgactive_SUPERVISOR_DBNAME, true);

	if (dboid == InvalidOid)
	{
		CreatedbStmt stmt;
		DefElem		de_template;
		DefElem		de_connlimit;

		de_template.defname = "template";
		de_template.type = T_String;
		de_template.arg = (Node *) makeString("template1");

		de_connlimit.defname = "connection_limit";
		de_template.type = T_Integer;
		de_connlimit.arg = (Node *) makeInteger(1);

		stmt.dbname = pgactive_SUPERVISOR_DBNAME;
		stmt.options = list_make2(&de_template, &de_connlimit);

		pstate = make_parsestate(NULL);

		dboid = createdb(pstate, &stmt);

		if (dboid == InvalidOid)
			elog(ERROR, "failed to create " pgactive_SUPERVISOR_DBNAME " DB");

		/* TODO DYNCONF: Add a comment to the db, and/or a dummy table */

		elog(LOG, "created database " pgactive_SUPERVISOR_DBNAME " (oid=%i) during pgactive startup", dboid);
	}
	else
	{
		elog(DEBUG3, "database " pgactive_SUPERVISOR_DBNAME " (oid=%i) already exists, not creating", dboid);
	}

	CommitTransactionCommand();

	Assert(dboid != InvalidOid);
}

Oid
pgactive_get_supervisordb_oid(bool missingok)
{
	Oid			dboid;

	dboid = get_database_oid(pgactive_SUPERVISOR_DBNAME, true);

	if (dboid == InvalidOid && !missingok)
	{
		/*
		 * We'll get relaunched soon, so just die rather than having a
		 * wait-and-test loop here
		 */
		elog(LOG, "exiting because pgactive supervisor database " pgactive_SUPERVISOR_DBNAME " does not yet exist");
		proc_exit(1);
	}

	return dboid;
}

#ifdef USE_ASSERT_CHECKING
/*
 * Verify that each pgactive-enabled database has exactly one per-db worker.
 * Presence of more than one per-db worker is indicative of a race condition we
 * try to prevent in pgactive_register_perdb_worker().
 */
static void
check_for_multiple_perdb_workers(void)
{
	int			i;
	bool		exists = false;
	List	   *perdb_w = NIL;

	LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);

	for (i = 0; i < pgactive_max_workers; i++)
	{
		pgactiveWorker *w = &pgactiveWorkerCtl->slots[i];

		/* unused slot */
		if (w->worker_type == pgactive_WORKER_EMPTY_SLOT)
			continue;

		/* unconnected slot */
		if (w->worker_proc == NULL)
			continue;

		if (w->worker_type == pgactive_WORKER_PERDB)
		{
			pgactivePerdbWorker *pw = &w->data.perdb;
			Oid			dboid = pw->p_dboid;

			if (!OidIsValid(dboid))
				continue;

			if (!list_member_oid(perdb_w, dboid))
				perdb_w = lappend_oid(perdb_w, dboid);
			else
			{
				ereport(LOG,
						(errmsg("more than one per-db worker exists for database %d",
								dboid),
						 errdetail("One of the workers' PID is %d.",
								   w->worker_pid)));
				exists = true;
			}
		}
	}

	LWLockRelease(pgactiveWorkerCtl->lock);

	if (exists)
		elog(PANIC, "cannot have more than one per-db worker for a single pgactive-enabled database");

	list_free(perdb_w);
}
#endif

/*
 * The pgactive supervisor is a static bgworker that serves as the supervisor
 * for all pgactive workers. It exists so that pgactive can be enabled and disabled
 * dynamically for databases.
 *
 * It is responsible for identifying pgactive-enabled databases at startup and
 * launching their dynamic per-db workers. It should do as little else as
 * possible, as it'll run when pgactive is in shared_preload_libraries whether
 * or not it's otherwise actually in use.
 *
 * The supervisor worker has no access to any database.
 */
void
pgactive_supervisor_worker_main(Datum main_arg)
{
	Assert(DatumGetInt32(main_arg) == 0);
#if PG_VERSION_NUM < 170000
	Assert(IsBackgroundWorker);
#else
	Assert(AmBackgroundWorkerProcess());
#endif

	pqsignal(SIGHUP, SignalHandlerForConfigReload);
	pqsignal(SIGTERM, die);
	BackgroundWorkerUnblockSignals();

	/*
	 * bgworkers aren't started until after recovery, even in hot standby. But
	 * lets make this clear anyway; we can't safely start in recovery because
	 * we'd possibly connect to peer slots already used by our upstream.
	 */
	if (RecoveryInProgress())
	{
		elog(INFO, "pgactive refusing to start during recovery");
		proc_exit(0);
	}

	MyProcPort = (Port *) calloc(1, sizeof(Port));

	/*
	 * Destroy leftover temporary dump directories (if any) from previous run.
	 * Also, register a proc_exit callback so that things get destroyed for
	 * clean exits.
	 */
	destroy_temp_dump_dirs(0, 0);
	if (!destroy_temp_dump_dirs_callback_registered)
	{
		on_proc_exit(destroy_temp_dump_dirs, 0);
		destroy_temp_dump_dirs_callback_registered = true;
	}

	/*
	 * Unfortunately we currently can't access shared catalogs like
	 * pg_shseclabel (where we store information about which database use
	 * pgactive) without being connected to a database. Only shared & nailed
	 * catalogs can be accessed before being connected to a database - and
	 * pg_shseclabel is not one of those.
	 *
	 * Instead we have a database pgactive_SUPERVISOR_DBNAME that's supposed
	 * to be empty which we just use to read pg_shseclabel. Not pretty, but it
	 * works. (The need for this goes away in 9.5 with the new oid-based
	 * alternative bgworker api).
	 *
	 * Without copying significant parts of InitPostgres() we can't even read
	 * pg_database without connecting to a database.  As we can't connect to
	 * "no database", we must connect to one that always exists, like
	 * template1, then use it to create a dummy database to operate in.
	 *
	 * Once created we set a shmem flag and restart so we know we can connect
	 * to the newly created database.
	 */
	if (!pgactiveWorkerCtl->is_supervisor_restart)
	{
		BackgroundWorkerInitializeConnection("template1", NULL, 0);
		pgactive_supervisor_createdb();

		pgactiveWorkerCtl->is_supervisor_restart = true;

		elog(LOG, "pgactive supervisor restarting to connect to '%s' DB for shared catalog access",
			 pgactive_SUPERVISOR_DBNAME);
		proc_exit(1);
	}

	BackgroundWorkerInitializeConnection(pgactive_SUPERVISOR_DBNAME, NULL, 0);
	Assert(ThisTimeLineID > 0);

	MyProcPort->database_name = pgactive_SUPERVISOR_DBNAME;

	LWLockAcquire(pgactiveWorkerCtl->lock, LW_EXCLUSIVE);
	pgactiveWorkerCtl->supervisor_latch = &MyProc->procLatch;
	LWLockRelease(pgactiveWorkerCtl->lock);

	elog(LOG, "pgactive supervisor restarted and connected to DB " pgactive_SUPERVISOR_DBNAME);

	SetConfigOption("application_name", "pgactive:supervisor", PGC_USERSET, PGC_S_SESSION);

	/* mark as idle, before starting to loop */
	pgstat_report_activity(STATE_IDLE, NULL);

	pgactive_supervisor_rescan_dbs();

	while (!ProcDiePending)
	{
		int			rc;
		long		timeout = 180000L;

		if (ConfigReloadPending)
		{
			ConfigReloadPending = false;
			ProcessConfigFile(PGC_SIGHUP);
			/* set log_min_messages */
			SetConfigOption("log_min_messages", pgactive_error_severity(pgactive_log_min_messages),
							PGC_POSTMASTER, PGC_S_OVERRIDE);
		}

#ifdef USE_ASSERT_CHECKING

		/*
		 * In assert-enabled build, supervisor needs to frequently call
		 * check_for_multiple_perdb_workers(), so keep a lower value for
		 * timeout.
		 */
		timeout = 10000L;
#endif

		/*
		 * After startup the supervisor doesn't currently have anything to do,
		 * so it can just go to sleep on its latch. It could exit after
		 * running startup, but we're expecting to need it to do other things
		 * down the track, so might as well keep it alive...
		 */
		rc = pgactiveWaitLatch(&MyProc->procLatch,
							   WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
							   timeout, PG_WAIT_EXTENSION);
		ResetLatch(&MyProc->procLatch);
		CHECK_FOR_INTERRUPTS();

		if (rc & WL_LATCH_SET)
		{
			/*
			 * We've been asked to launch new perdb workers if there are any
			 * changes to security labels.
			 */
			pgactive_supervisor_rescan_dbs();
		}

#ifdef USE_ASSERT_CHECKING
		check_for_multiple_perdb_workers();
#endif
	}

	proc_exit(0);
}

/*
 * Register the pgactive supervisor bgworker, which will start all the
 * per-db workers.
 *
 * Called in postmaster context from _PG_init.
 *
 * The supervisor is guaranteed to be assigned the first shmem slot in our
 * workers shmem array. This is vital because at this point shemem isn't
 * allocated yet, so all we can do is tell the supervisor worker its shmem slot
 * number then actually populate that slot when the postmaster runs our shmem
 * init callback later.
 */
void
pgactive_supervisor_register()
{
	BackgroundWorker bgw = {0};

	Assert(IsPostmasterEnvironment && !IsUnderPostmaster);

	/*
	 * Configure superviosur worker. It basically accesses shared relations,
	 * but does not connect to any specific database. We still have to flag it
	 * as using a connection in the bgworker API.
	 */
	bgw.bgw_flags = BGWORKER_SHMEM_ACCESS |
		BGWORKER_BACKEND_DATABASE_CONNECTION;
	bgw.bgw_start_time = BgWorkerStart_RecoveryFinished;
	snprintf(bgw.bgw_library_name, BGW_MAXLEN, pgactive_LIBRARY_NAME);
	snprintf(bgw.bgw_function_name, BGW_MAXLEN, "pgactive_supervisor_worker_main");
	snprintf(bgw.bgw_name, BGW_MAXLEN, "pgactive supervisor");
	snprintf(bgw.bgw_type, BGW_MAXLEN, "pgactive supervisor");
	bgw.bgw_restart_time = 1;

	RegisterBackgroundWorker(&bgw);
}
