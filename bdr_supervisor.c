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
#include "bdr_label.h"

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

#if PG_VERSION_NUM >= 90500
	#define CONNECTION_LIMIT_STR "connection_limit"
#else
	#define CONNECTION_LIMIT_STR "connectionlimit"
#endif

/*
 * Register a new perdb worker for the named database. The worker MUST
 * not already exist.
 *
 * This is called by the supervisor during startup, and by user backends when
 * the first connection is added for a database.
 */
static void
bdr_register_perdb_worker(const char * dbname)
{
	BackgroundWorkerHandle *bgw_handle;
	BackgroundWorker		bgw;
	BdrWorker			   *worker;
	BdrPerdbWorker		   *perdb;
	unsigned int			worker_slot_number;
	uint32					worker_arg;

	Assert(LWLockHeldByMe(BdrWorkerCtl->lock));

	elog(DEBUG2, "Registering per-db worker for db %s", dbname);

	worker = bdr_worker_shmem_alloc(
				BDR_WORKER_PERDB,
				&worker_slot_number
			);

	perdb = &worker->data.perdb;

	strncpy(NameStr(perdb->dbname),
			dbname, NAMEDATALEN);
	NameStr(perdb->dbname)[NAMEDATALEN-1] = '\0';
	/* Nodecount is set when apply workers are registered */
	perdb->nnodes = -1;
	perdb->seq_slot = bdr_sequencer_get_next_free_slot();

	/*
	 * The rest of the perdb worker's shmem segment - proclatch
	 * and nnodes - gets set up by the worker during startup.
	 */

	bgw.bgw_flags = BGWORKER_SHMEM_ACCESS |
		BGWORKER_BACKEND_DATABASE_CONNECTION;
	bgw.bgw_start_time = BgWorkerStart_RecoveryFinished;
	//bgw.bgw_main = NULL;
	strncpy(bgw.bgw_library_name, BDR_LIBRARY_NAME, BGW_MAXLEN);
	strncpy(bgw.bgw_function_name, "bdr_perdb_worker_main", BGW_MAXLEN);
	bgw.bgw_restart_time = 5;
	bgw.bgw_notify_pid = 0;
	snprintf(bgw.bgw_name, BGW_MAXLEN,
			 "bdr db: %s", dbname);

	/*
	 * The main arg is composed of two uint16 parts - the worker
	 * generation number (see bdr_worker_shmem_startup) and the index into
	 * BdrWorkerCtl->slots in shared memory.
	 */
	Assert(worker_slot_number <= UINT16_MAX);
	worker_arg = (((uint32)BdrWorkerCtl->worker_generation) << 16) | (uint32)worker_slot_number;
	bgw.bgw_main_arg = Int32GetDatum(worker_arg);

	if (!RegisterDynamicBackgroundWorker(&bgw, &bgw_handle))
	{
		ereport(ERROR,
				(errcode(ERRCODE_CONFIGURATION_LIMIT_EXCEEDED),
				 errmsg("Registering BDR worker failed, check prior log messages for details")));
	}

	elog(DEBUG2, "Registered per-db worker for %s successfully", dbname);
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
	ScanKeyData	skey[2];
	SysScanDesc scan;
	HeapTuple	secTuple;
	int			n_new_workers = 0, bdr_dbs = 0;

	elog(DEBUG1, "Supervisor scanning for BDR-enabled databases");

	pgstat_report_activity(STATE_RUNNING, "scanning backends");

	StartTransactionCommand();

	/*
	 * Scan pg_shseclabel looking for entries for pg_database with the bdr label
	 * provider. We'll find all labels for the BDR provider, irrespective
	 * of value.
	 *
	 * The only index present isn't much use for this scan and using it makes
	 * us set up more keys, so do a heap scan.
	 *
	 * The lock taken on pg_shseclabel must be strong enough to conflict with
	 * the lock taken be bdr.bdr_connection_add(...) to ensure that any
	 * transactions adding new labels have commited and cleaned up before we
	 * read it. Otherwise a race between the supervisor latch being set in a
	 * commit hook and the tuples actually becoming visible is possible.
	 */
	secrel = heap_open(SharedSecLabelRelationId, RowShareLock);

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
	 * Now examine each label and if there's no worker for the labled
	 * DB already, start one.
	 */
	while (HeapTupleIsValid(secTuple = systable_getnext(scan)))
	{
		FormData_pg_shseclabel *sec;
		char				   *label_dbname;

		sec = (FormData_pg_shseclabel*) GETSTRUCT(secTuple);

		/*
		 * The per-db workers are mapped by name not oid, and that's necessary
		 * because the bgworker API requires that databases be identified by
		 * name.
		 *
		 * Look up the name of the DB with this OID and compare it. It's a bit slow,
		 * but we aren't doing this much.
		 *
		 * FIXME: Currently if a database is renamed, you'll have to restart
		 * PostgreSQL before BDR notices.
		 *
		 * The oid-based bgworker API in 9.5 would allow this to be fixed.
		 */
		label_dbname = get_database_name(sec->objoid);

		if (!bdr_is_bdr_activated_db(sec->objoid))
		{
			pfree(label_dbname);
			continue;
		}

		elog(DEBUG1, "Found BDR-enabled database %s (oid=%i)",
			 label_dbname, sec->objoid);

		bdr_dbs++;

		/*
		 * Check if we have a per-db worker for this db oid already and if
		 * we don't, start one.
		 *
		 * This is O(n^2) for n BDR-enabled DBs; to be more scalable we could
		 * accumulate and sort the oids, then do a single scan of the shmem
		 * segment. But really, if you have that many DBs this cost is nothing.
		 */
		if (find_perdb_worker_slot(sec->objoid, NULL) == -1)
		{
			/* No perdb worker exists for this DB, make one */
			bdr_register_perdb_worker(label_dbname);
			n_new_workers++;
		} else {
			elog(DEBUG2, "per-db worker for db %s already exists, not registering",
				 label_dbname);
		}

		pfree(label_dbname);
	}

	elog(DEBUG2, "Found %i BDR-labeled DBs; registered %i new per-db workers",
		 bdr_dbs, n_new_workers);

	LWLockRelease(BdrWorkerCtl->lock);

	systable_endscan(scan);
	heap_close(secrel, RowShareLock);

	CommitTransactionCommand();

	elog(DEBUG2, "Finished scanning for BDR-enabled databases");

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
	Oid dboid;
	ParseState *pstate;

	StartTransactionCommand();

	/* If the DB already exists, no need to create it */
	dboid = get_database_oid(BDR_SUPERVISOR_DBNAME, true);

	if (dboid == InvalidOid)
	{
		CreatedbStmt stmt;
		DefElem de_template;
		DefElem de_connlimit;

		de_template.defname = "template";
		de_template.type = T_String;
		de_template.arg = (Node*) makeString("template1");

		de_connlimit.defname = CONNECTION_LIMIT_STR;
		de_template.type = T_Integer;
		de_connlimit.arg = (Node*) makeInteger(1);

		stmt.dbname = BDR_SUPERVISOR_DBNAME;
		stmt.options = list_make2(&de_template, &de_connlimit);

		pstate = make_parsestate(NULL);

		dboid = createdb(pstate, &stmt);

		if (dboid == InvalidOid)
			elog(ERROR, "Failed to create "BDR_SUPERVISOR_DBNAME" DB");

		/* TODO DYNCONF: Add a comment to the db, and/or a dummy table */

		elog(LOG, "Created database "BDR_SUPERVISOR_DBNAME" (oid=%i) during BDR startup", dboid);
	}
	else
	{
		elog(DEBUG3, "Database "BDR_SUPERVISOR_DBNAME" (oid=%i) already exists, not creating", dboid);
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
		/* We'll get relaunched soon, so just die rather than having a wait-and-test loop here */
		elog(DEBUG1, "exiting because BDR supervisor database "BDR_SUPERVISOR_DBNAME" does not yet exist");
		proc_exit(1);
	}

	return dboid;
}

/*
 * The BDR supervisor is a static bgworker that serves as the master/supervisor
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
	 * Instead we have a database BDR_SUPERVISOR_DBNAME that's supposed to
	 * be empty which we just use to read pg_shseclabel. Not pretty, but it
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

		elog(DEBUG1, "BDR supervisor restarting to connect to '%s' DB",
			 BDR_SUPERVISOR_DBNAME);
		proc_exit(1);
	}

	BackgroundWorkerInitializeConnection(BDR_SUPERVISOR_DBNAME, NULL, 0);
	Assert(ThisTimeLineID > 0);

	MyProcPort->database_name = BDR_SUPERVISOR_DBNAME;

	LWLockAcquire(BdrWorkerCtl->lock, LW_EXCLUSIVE);
	BdrWorkerCtl->supervisor_latch = &MyProc->procLatch;
	LWLockRelease(BdrWorkerCtl->lock);

	elog(DEBUG1, "BDR supervisor connected to DB "BDR_SUPERVISOR_DBNAME);

	SetConfigOption("application_name", "bdr supervisor", PGC_USERSET, PGC_S_SESSION);

	/* mark as idle, before starting to loop */
	pgstat_report_activity(STATE_IDLE, NULL);

	bdr_supervisor_rescan_dbs();

	while (!got_SIGTERM)
	{
		int rc;

		/*
		 * After startup the supervisor doesn't currently have anything to do,
		 * so it can just go to sleep on its latch. It could exit after running
		 * startup, but we're expecting to need it to do other things down the
		 * track, so might as well keep it alive...
		 */
		rc = WaitLatch(&MyProc->procLatch,
					   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
					   180000L, PG_WAIT_EXTENSION);

		ResetLatch(&MyProc->procLatch);

		/* emergency bailout if postmaster has died */
		if (rc & WL_POSTMASTER_DEATH)
			proc_exit(1);

		if (got_SIGHUP)
		{
			got_SIGHUP = false;
			ProcessConfigFile(PGC_SIGHUP);
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
	BackgroundWorker bgw;

	Assert(IsPostmasterEnvironment && !IsUnderPostmaster);

	/*
	 * The supervisor worker accesses shared relations, but does not connect to
	 * any specific database. We still have to flag it as using a connection in
	 * the bgworker API.
	 */
	bgw.bgw_flags = BGWORKER_SHMEM_ACCESS |
		BGWORKER_BACKEND_DATABASE_CONNECTION;
	bgw.bgw_start_time = BgWorkerStart_RecoveryFinished;
	//bgw.bgw_main = NULL;
	strncpy(bgw.bgw_library_name, BDR_LIBRARY_NAME, BGW_MAXLEN);
	strncpy(bgw.bgw_function_name, "bdr_supervisor_worker_main", BGW_MAXLEN);
	bgw.bgw_restart_time = 1;
	bgw.bgw_notify_pid = 0;
	snprintf(bgw.bgw_name, BGW_MAXLEN,
			 "bdr supervisor");
	bgw.bgw_main_arg = Int32GetDatum(0); /* unused */

	RegisterBackgroundWorker(&bgw);
}
