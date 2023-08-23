/* -------------------------------------------------------------------------
 *
 * bdr_supervisor.c
 *		Cluster wide supervisor worker.
 *
 * Copyright (C) 2014-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		bdr_supervisor.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "bdr.h"

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
#include "storage/ipc.h"

#include "utils/builtins.h"
#include "utils/elog.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"

/*
 * Register a new perdb worker for a database. The worker MUST not already
 * exist.
 *
 * This is called by the supervisor during startup, and by user backends when
 * the first connection is added for a database.
 */
static void
bdr_register_perdb_worker(Oid dboid)
{
	BackgroundWorkerHandle *bgw_handle;
	BackgroundWorker bgw = {0};
	BgwHandleStatus status;
	pid_t		pid;
	BdrWorker  *worker;
	BdrPerdbWorker *perdb;
	unsigned int worker_slot_number;
	uint32		worker_arg;
	char	   *dbname;

	Assert(LWLockHeldByMe(BdrWorkerCtl->lock));
	dbname = get_database_name(dboid);

	elog(DEBUG2, "registering per-db worker for database \"%s\" with OID %u",
		 dbname, dboid);

	worker = bdr_worker_shmem_alloc(
									BDR_WORKER_PERDB,
									&worker_slot_number
		);

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
	snprintf(bgw.bgw_library_name, BGW_MAXLEN, BDR_LIBRARY_NAME);
	snprintf(bgw.bgw_function_name, BGW_MAXLEN, "bdr_perdb_worker_main");
	snprintf(bgw.bgw_name, BGW_MAXLEN, "bdr per-db worker for %s", dbname);
	snprintf(bgw.bgw_type, BGW_MAXLEN, "bdr per-db worker");
	bgw.bgw_start_time = BgWorkerStart_RecoveryFinished;
	bgw.bgw_restart_time = 5;

	/* We want supervisor to be notified when the worker is started */
	bgw.bgw_notify_pid = MyProcPid;

	/*
	 * The main arg is composed of two uint16 parts - the worker generation
	 * number (see bdr_worker_shmem_startup) and the index into
	 * BdrWorkerCtl->slots in shared memory.
	 */
	Assert(worker_slot_number <= UINT16_MAX);
	worker_arg = (((uint32) BdrWorkerCtl->worker_generation) << 16) | (uint32) worker_slot_number;
	bgw.bgw_main_arg = Int32GetDatum(worker_arg);

	if (!RegisterDynamicBackgroundWorker(&bgw, &bgw_handle))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_RESOURCES),
				 errmsg("registering BDR per-db dynamic background worker failed"),
				 errhint("Consider increasing configuration parameter \"max_worker_processes\".")));

	elog(DEBUG2, "successfully registered BDR per-db worker for database \"%s\"", dbname);

	/*
	 * Here, supervisor must ensure the per-db worker registered above is
	 * started by postmaster and updated database oid in its shared memory
	 * slot. This is to avoid a race condition.
	 *
	 * Steps that can otherwise lead to the race condition are:
	 *
	 * 1. Supervisor registers per-db worker while holding BdrWorkerCtl->lock
	 * in bdr_supervisor_rescan_dbs().
	 *
	 * 2. Started per-db worker needs BdrWorkerCtl->lock to update database
	 * oid in its shared memory slot and thus adds itself to lock's wait
	 * queue. Unless per-db worker updates database oid, supervisor cannot
	 * consider it started in find_perdb_worker_slot().
	 *
	 * 3. Supervisor releases the lock, but a waiter other than per-db worker
	 * acquires the lock. Meanwhile, the supervisor adds itself to the lock's
	 * wait queue, thanks to SetLatch() in bdr_perdb_xact_callback().
	 *
	 * 4. Supervisor acquires the lock again before the first per-db worker
	 * and fails to find the first per-db worker in find_perdb_worker_slot()
	 * as it hasn't yet got a chance to update database oid in the shared
	 * memory slot. This makes supervisor register another per-db worker for
	 * the same BDR-enabled database causing multiple per-db workers (and so
	 * multiple apply workers - each per-db worker starts an apply worker) to
	 * coexist. These multiple per-db workers don't let nodes joining the BDR
	 * group to come out from catchup state to ready state.
	 *
	 * We fix this race condition by making supervisor register per-db worker,
	 * wait until postmaster starts it, give it a chance to update database
	 * oid in its shared memory slot and continue to scan for other
	 * BDR-enabled databases. An assert-enabled function
	 * check_for_multiple_perdb_workers() helps to validate the fix.
	 */
	status = WaitForBackgroundWorkerStartup(bgw_handle, &pid);
	if (status != BGWH_STARTED)
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_RESOURCES),
				 errmsg("could not start per-db worker for %s", dbname),
				 errhint("More details may be available in the server log.")));

	/*
	 * Wait for per-db worker to register itself in the worker's shared memory
	 * slot.
	 */
	for (;;)
	{
		int			rc;

		LWLockRelease(BdrWorkerCtl->lock);

		rc = WaitLatch(&MyProc->procLatch,
					   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
					   100L, PG_WAIT_EXTENSION);

		ResetLatch(&MyProc->procLatch);

		/* emergency bailout if postmaster has died */
		if (rc & WL_POSTMASTER_DEATH)
			proc_exit(1);

		if (got_SIGHUP)
		{
			got_SIGHUP = false;
			ProcessConfigFile(PGC_SIGHUP);
			/* set log_min_messages */
			SetConfigOption("log_min_messages", bdr_error_severity(bdr_log_min_messages),
							PGC_POSTMASTER, PGC_S_OVERRIDE);
		}

		CHECK_FOR_INTERRUPTS();

		LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);

		if (perdb->proclatch != NULL && perdb->p_dboid == dboid)
		{
			LWLockRelease(BdrWorkerCtl->lock);
			break;
		}
	}

	Assert(!LWLockHeldByMe(BdrWorkerCtl->lock));

	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);

	Assert(perdb->c_dboid == perdb->p_dboid);
	elog(DEBUG2, "successfully started BDR per-db worker for database \"%s\", perdb->proclatch %p, perdb->p_dboid %d",
		 dbname, perdb->proclatch, perdb->p_dboid);
	pfree(dbname);
}

/*
 * Check for BDR-enabled DBs and start per-db workers for any that currently
 * lack them.
 *
 * TODO DYNCONF: Handle removal of BDR from DBs
 */
static void
bdr_supervisor_rescan_dbs()
{
	Relation	secrel;
	ScanKeyData skey[2];
	SysScanDesc scan;
	HeapTuple	secTuple;
	int			n_new_workers = 0,
				bdr_dbs = 0;

	elog(DEBUG1, "supervisor scanning for BDR-enabled databases");

	pgstat_report_activity(STATE_RUNNING, "scanning backends");

	StartTransactionCommand();

	/*
	 * Scan pg_shseclabel looking for entries for pg_database with the bdr
	 * label provider. We'll find all labels for the BDR provider,
	 * irrespective of value.
	 *
	 * The only index present isn't much use for this scan and using it makes
	 * us set up more keys, so do a heap scan.
	 *
	 * The lock taken on pg_shseclabel must be strong enough to conflict with
	 * the lock taken be bdr.bdr_connection_add(...) to ensure that any
	 * transactions adding new labels have committed and cleaned up before we
	 * read it. Otherwise a race between the supervisor latch being set in a
	 * commit hook and the tuples actually becoming visible is possible.
	 */
	secrel = table_open(SharedSecLabelRelationId, RowShareLock);

	ScanKeyInit(&skey[0],
				Anum_pg_shseclabel_classoid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(DatabaseRelationId));

	ScanKeyInit(&skey[1],
				Anum_pg_shseclabel_provider,
				BTEqualStrategyNumber, F_TEXTEQ,
				CStringGetTextDatum(BDR_SECLABEL_PROVIDER));

	scan = systable_beginscan(secrel, InvalidOid, false, NULL, 2, &skey[0]);

	/*
	 * We need to scan the shmem segment that tracks BDR workers and possibly
	 * modify it, so lock it.
	 *
	 * We have to take an exclusive lock in case we need to modify it,
	 * otherwise we'd be faced with a lock upgrade.
	 */
	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);

	/*
	 * Now examine each label and if there's no worker for the labled DB
	 * already, start one.
	 */
	while (HeapTupleIsValid(secTuple = systable_getnext(scan)))
	{
		FormData_pg_shseclabel *sec;

		sec = (FormData_pg_shseclabel *) GETSTRUCT(secTuple);

		if (!bdr_is_bdr_activated_db(sec->objoid))
			continue;

		/*
		 * While we are here, there's no problem even if the database is
		 * renamed. This is because we use OID based bg worker API (i.e.,
		 * every bg worker is mapped with database OID, not with database
		 * name), and database renaming doesn't change the OID.
		 */
		elog(DEBUG1, "found BDR-enabled database with OID %u", sec->objoid);

		bdr_dbs++;

		/*
		 * Check if we have a per-db worker for this db oid already and if we
		 * don't, start one.
		 *
		 * This is O(n^2) for n BDR-enabled DBs; to be more scalable we could
		 * accumulate and sort the oids, then do a single scan of the shmem
		 * segment. But really, if you have that many DBs this cost is
		 * nothing.
		 */
		if (find_perdb_worker_slot(sec->objoid, NULL) == -1)
		{
			/* No perdb worker exists for this DB, make one */
			bdr_register_perdb_worker(sec->objoid);
			Assert(LWLockHeldByMe(BdrWorkerCtl->lock));
			n_new_workers++;
		}
		else
			elog(DEBUG2, "per-db worker for database with OID %u already exists, not registering",
				 sec->objoid);
	}

	elog(DEBUG2, "found %i BDR-labeled DBs; registered %i new per-db workers",
		 bdr_dbs, n_new_workers);

	LWLockRelease(BdrWorkerCtl->lock);

	systable_endscan(scan);
	table_close(secrel, RowShareLock);

	CommitTransactionCommand();

	elog(DEBUG2, "finished scanning for BDR-enabled databases");

	pgstat_report_activity(STATE_IDLE, NULL);
}

/*
 * Create the database the supervisor remains connected
 * to, a DB with no user connections permitted.
 *
 * This is a workaorund for the inability to use pg_shseclabel
 * without a DB connection; see comments in bdr_supervisor_main
 */
static void
bdr_supervisor_createdb()
{
	Oid			dboid;
	ParseState *pstate;

	StartTransactionCommand();

	/* If the DB already exists, no need to create it */
	dboid = get_database_oid(BDR_SUPERVISOR_DBNAME, true);

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

		stmt.dbname = BDR_SUPERVISOR_DBNAME;
		stmt.options = list_make2(&de_template, &de_connlimit);

		pstate = make_parsestate(NULL);

		dboid = createdb(pstate, &stmt);

		if (dboid == InvalidOid)
			elog(ERROR, "failed to create " BDR_SUPERVISOR_DBNAME " DB");

		/* TODO DYNCONF: Add a comment to the db, and/or a dummy table */

		elog(LOG, "created database " BDR_SUPERVISOR_DBNAME " (oid=%i) during BDR startup", dboid);
	}
	else
	{
		elog(DEBUG3, "database " BDR_SUPERVISOR_DBNAME " (oid=%i) already exists, not creating", dboid);
	}

	CommitTransactionCommand();

	Assert(dboid != InvalidOid);
}

Oid
bdr_get_supervisordb_oid(bool missingok)
{
	Oid			dboid;

	dboid = get_database_oid(BDR_SUPERVISOR_DBNAME, true);

	if (dboid == InvalidOid && !missingok)
	{
		/*
		 * We'll get relaunched soon, so just die rather than having a
		 * wait-and-test loop here
		 */
		elog(LOG, "exiting because BDR supervisor database " BDR_SUPERVISOR_DBNAME " does not yet exist");
		proc_exit(1);
	}

	return dboid;
}

#ifdef USE_ASSERT_CHECKING
/*
 * Verify that each BDR-enabled database has exactly one per-db worker.
 * Presence of more than one per-db worker is indicative of a race condition we
 * try to prevent in bdr_register_perdb_worker().
 */
static void
check_for_multiple_perdb_workers(void)
{
	int			i;
	bool		exists = false;
	List	   *perdb_w = NIL;

	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);

	for (i = 0; i < bdr_max_workers; i++)
	{
		BdrWorker  *w = &BdrWorkerCtl->slots[i];

		/* unused slot */
		if (w->worker_type == BDR_WORKER_EMPTY_SLOT)
			continue;

		/* unconnected slot */
		if (w->worker_proc == NULL)
			continue;

		if (w->worker_type == BDR_WORKER_PERDB)
		{
			BdrPerdbWorker *pw = &w->data.perdb;
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

	LWLockRelease(BdrWorkerCtl->lock);

	if (exists)
		elog(PANIC, "cannot have more than one per-db worker for a single BDR-enabled database");

	list_free(perdb_w);
}
#endif

/*
 * The BDR supervisor is a static bgworker that serves as the supervisor
 * for all BDR workers. It exists so that BDR can be enabled and disabled
 * dynamically for databases.
 *
 * It is responsible for identifying BDR-enabled databases at startup and
 * launching their dynamic per-db workers. It should do as little else as
 * possible, as it'll run when BDR is in shared_preload_libraries whether
 * or not it's otherwise actually in use.
 *
 * The supervisor worker has no access to any database.
 */
void
bdr_supervisor_worker_main(Datum main_arg)
{
	Assert(DatumGetInt32(main_arg) == 0);
	Assert(IsBackgroundWorker);

	pqsignal(SIGHUP, bdr_sighup);
	pqsignal(SIGTERM, bdr_sigterm);
	BackgroundWorkerUnblockSignals();

	/*
	 * bgworkers aren't started until after recovery, even in hot standby. But
	 * lets make this clear anyway; we can't safely start in recovery because
	 * we'd possibly connect to peer slots already used by our upstream.
	 */
	if (RecoveryInProgress())
	{
		elog(INFO, "bdr refusing to start during recovery");
		proc_exit(0);
	}

	MyProcPort = (Port *) calloc(1, sizeof(Port));

	/*
	 * Unfortunately we currently can't access shared catalogs like
	 * pg_shseclabel (where we store information about which database use bdr)
	 * without being connected to a database. Only shared & nailed catalogs
	 * can be accessed before being connected to a database - and
	 * pg_shseclabel is not one of those.
	 *
	 * Instead we have a database BDR_SUPERVISOR_DBNAME that's supposed to be
	 * empty which we just use to read pg_shseclabel. Not pretty, but it
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
	if (!BdrWorkerCtl->is_supervisor_restart)
	{
		BackgroundWorkerInitializeConnection("template1", NULL, 0);
		bdr_supervisor_createdb();

		BdrWorkerCtl->is_supervisor_restart = true;

		elog(LOG, "BDR supervisor restarting to connect to '%s' DB for shared catalog access",
			 BDR_SUPERVISOR_DBNAME);
		proc_exit(1);
	}

	BackgroundWorkerInitializeConnection(BDR_SUPERVISOR_DBNAME, NULL, 0);
	Assert(ThisTimeLineID > 0);

	MyProcPort->database_name = BDR_SUPERVISOR_DBNAME;

	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);
	BdrWorkerCtl->supervisor_latch = &MyProc->procLatch;
	LWLockRelease(BdrWorkerCtl->lock);

	elog(LOG, "BDR supervisor restarted and connected to DB " BDR_SUPERVISOR_DBNAME);

	SetConfigOption("application_name", "bdr supervisor", PGC_USERSET, PGC_S_SESSION);

	/* mark as idle, before starting to loop */
	pgstat_report_activity(STATE_IDLE, NULL);

	bdr_supervisor_rescan_dbs();

	while (!got_SIGTERM)
	{
		int			rc;
		long		timeout = 180000L;

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
		rc = WaitLatch(&MyProc->procLatch,
					   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
					   timeout, PG_WAIT_EXTENSION);

		ResetLatch(&MyProc->procLatch);

		/* emergency bailout if postmaster has died */
		if (rc & WL_POSTMASTER_DEATH)
			proc_exit(1);

		if (got_SIGHUP)
		{
			got_SIGHUP = false;
			ProcessConfigFile(PGC_SIGHUP);
			/* set log_min_messages */
			SetConfigOption("log_min_messages", bdr_error_severity(bdr_log_min_messages),
							PGC_POSTMASTER, PGC_S_OVERRIDE);
		}

		if (rc & WL_LATCH_SET)
		{
			/*
			 * We've been asked to launch new perdb workers if there are any
			 * changes to security labels.
			 */
			bdr_supervisor_rescan_dbs();
		}

		CHECK_FOR_INTERRUPTS();

#ifdef USE_ASSERT_CHECKING
		check_for_multiple_perdb_workers();
#endif
	}

	proc_exit(0);
}

/*
 * Register the BDR supervisor bgworker, which will start all the
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
bdr_supervisor_register()
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
	snprintf(bgw.bgw_library_name, BGW_MAXLEN, BDR_LIBRARY_NAME);
	snprintf(bgw.bgw_function_name, BGW_MAXLEN, "bdr_supervisor_worker_main");
	snprintf(bgw.bgw_name, BGW_MAXLEN, "bdr supervisor");
	snprintf(bgw.bgw_type, BGW_MAXLEN, "bdr supervisor");
	bgw.bgw_restart_time = 1;

	RegisterBackgroundWorker(&bgw);
}
