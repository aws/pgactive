-- We're one instance with two databases so we should
-- have two walsenders and two apply workers.

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE FUNCTION public.wait_for_nwalsenders(nsenders integer)
RETURNS void
LANGUAGE plpgsql
AS $$
BEGIN
  WHILE (SELECT count(1) FROM pg_stat_get_wal_senders() s) != nsenders
  LOOP
    PERFORM pg_sleep(0.2);
    PERFORM pg_stat_clear_snapshot();
  END LOOP;
END;
$$;
$DDL$);


SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE FUNCTION public.wait_for_nworkers(nsenders integer)
RETURNS void
LANGUAGE plpgsql
AS $$
BEGIN
  WHILE (SELECT count(1) FROM pg_stat_activity WHERE application_name LIKE 'node%:apply') != nsenders
  LOOP
    PERFORM pg_sleep(0.2);
    PERFORM pg_stat_clear_snapshot();
  END LOOP;
END;
$$;
$DDL$);


SELECT wait_for_nwalsenders(2);
SELECT wait_for_nworkers(2);

BEGIN; SET LOCAL pgactive.skip_ddl_replication = true; SELECT pgactive._pgactive_pause_worker_management_private(true); COMMIT;

-- We're one instance with two databases so we should have two apply workers
SELECT COUNT(*) = 2 AS ok FROM pgactive.pgactive_get_workers_info() WHERE worker_type = 'apply';

-- Kill all apply workers except our own
SELECT n.node_name, pgactive.pgactive_terminate_workers(node_sysid, node_timeline, node_dboid, 'apply')
  FROM pgactive.pgactive_nodes n
  WHERE (node_sysid, node_timeline, node_dboid) <> pgactive.pgactive_get_local_nodeid();

-- We must remain with our own apply worker
SELECT COUNT(*) = 1 AS ok FROM pgactive.pgactive_get_workers_info()
  WHERE (sysid, timeline, dboid) = pgactive.pgactive_get_local_nodeid() AND worker_type = 'apply';

-- One worker should vanish and not have restarted because of the timer
SELECT wait_for_nworkers(1);

-- Wait for reconnect. No need for pgactive_connections_changed()
-- since this'll just stop the apply workers quitting as soon
-- as they launch.
BEGIN; SET LOCAL pgactive.skip_ddl_replication = true; SELECT pgactive._pgactive_pause_worker_management_private(false); COMMIT;

SELECT wait_for_nworkers(2);

SELECT pg_sleep(10);

BEGIN; SET LOCAL pgactive.skip_ddl_replication = true; SELECT pgactive._pgactive_pause_worker_management_private(true); COMMIT;

-- We're one instance with two databases so we should have two walsender workers
SELECT COUNT(*) = 2 AS ok FROM pgactive.pgactive_get_workers_info() WHERE worker_type = 'walsender';

-- Kill all walsenders except our own
SELECT n.node_name, pgactive.pgactive_terminate_workers(node_sysid, node_timeline, node_dboid, 'walsender')
  FROM pgactive.pgactive_nodes n
  WHERE (node_sysid, node_timeline, node_dboid) <> pgactive.pgactive_get_local_nodeid();

SELECT pg_sleep(10);

-- We must remain with our own walsender
SELECT COUNT(*) = 1 AS ok FROM pgactive.pgactive_get_workers_info()
  WHERE (sysid, timeline, dboid) = pgactive.pgactive_get_local_nodeid() AND worker_type = 'walsender';

-- One left
SELECT wait_for_nwalsenders(1);

-- OK, let them come back up
BEGIN; SET LOCAL pgactive.skip_ddl_replication = true; SELECT pgactive._pgactive_pause_worker_management_private(false); COMMIT;

SELECT wait_for_nwalsenders(2);
