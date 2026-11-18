/* pgactive--2.1.9--2.1.10.sql */

-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION pgactive UPDATE TO '2.1.10'" to load this file. \quit

SET pgactive.skip_ddl_replication = true;
SET LOCAL search_path = pgactive;
-- Start Upgrade SQLs/Functions/Procedures

-- Fresh 2.1.9 installs left this executable by public.
REVOKE ALL ON FUNCTION pgactive_get_replication_lag_info() FROM public;

-- Report a failed initialization of a joining node.
DROP FUNCTION pgactive_wait_for_node_ready(integer, integer);

CREATE FUNCTION pgactive_wait_for_node_ready(
  timeout integer DEFAULT 0,
  progress_interval integer DEFAULT 60)
RETURNS void LANGUAGE plpgsql VOLATILE
AS $body$
DECLARE
  local_node record;
  remote_node record;
  t_lp_cnt integer := 0;
  p_lp_cnt integer := 0;
  w_lp_cnt integer;
  l_db_init_sz int8;
  l_db_sz int8;
  r_db text;
  p_pct integer;
  sleep_sec integer;
  worker_timeout integer;
  init_error text;
  init_unregistered boolean;
  init_failed boolean;
  init_origins int8;
  init_roname text;
  init_slot text;
  cleanup_hint text;
  have_remote boolean := false;
BEGIN

    IF timeout < 0 THEN
      RAISE EXCEPTION '''timeout'' parameter must not be 0';
    END IF;

    IF progress_interval <= 0 THEN
      RAISE EXCEPTION '''progress_interval'' parameter must be > 0';
    END IF;
    w_lp_cnt := 0;
    sleep_sec := 5;
    worker_timeout := 120;
    LOOP
      PERFORM pg_sleep( sleep_sec );
      PERFORM PID from pg_stat_activity where application_name = 'pgactive:supervisor';
      IF FOUND THEN
        EXIT;
      END IF;
      IF w_lp_cnt > worker_timeout THEN
        RAISE EXCEPTION 'pgactive supervisor is not running';
      ELSE
        RAISE NOTICE 'waiting for pgactive supervisor to start %/%', w_lp_cnt, worker_timeout;
      END IF;
      w_lp_cnt := w_lp_cnt + sleep_sec;
    END LOOP;

    IF current_setting('transaction_isolation') <> 'read committed' THEN
        RAISE EXCEPTION 'can only wait for node join in an ISOLATION LEVEL READ COMMITTED transaction, not %',
                        current_setting('transaction_isolation');
    END IF;

    SELECT * FROM pgactive.pgactive_nodes
      WHERE (node_sysid, node_timeline, node_dboid) = pgactive.pgactive_get_local_nodeid()
      INTO local_node;

    IF local_node.node_init_from_dsn is NULL THEN
      RAISE NOTICE 'checking status of pgactive.pgactive_create_group';
    ELSE
      RAISE NOTICE 'checking status of pgactive.pgactive_join_group';
    END IF;
    w_lp_cnt := 0;
    sleep_sec := 10;
    worker_timeout := 300;
    LOOP
      SELECT * FROM pgactive.pgactive_nodes
      WHERE (node_sysid, node_timeline, node_dboid)
        = pgactive.pgactive_get_local_nodeid()
      INTO local_node;

      -- The per-db worker records why initializing this node from the
      -- join_using_dsn node failed, and unregisters, as it doesn't retry
      -- it. Report that instead of waiting, and say which server logs have
      -- the error. Don't connect to the join_using_dsn node for this: that
      -- can fail, even with FATAL, and hide the error.
      init_failed := false;
      IF local_node.node_init_from_dsn IS NOT NULL AND local_node.node_status <> 'r' THEN
        SELECT last_error, unregistered FROM pgactive.pgactive_get_workers_info()
          WHERE worker_type = 'per-db' AND dboid = local_node.node_dboid
          INTO init_error, init_unregistered;
        init_failed := coalesce(init_error IN ('pgactive_init_dump_failure',
                                               'pgactive_init_restore_failure',
                                               'pgactive_previous_init_failure'),
                                false);

        IF init_failed AND init_unregistered THEN
          -- What a failed initialization leaves behind: the replication
          -- origin for the join_using_dsn node here, and this node's slot on
          -- that node, whose name has that node's database OID, from the
          -- origin's name. pgactive_parse_replident_name() fails on any
          -- other name, so filter those out first. Name them only if there
          -- is exactly one such origin.
          SELECT count(*), min(ro.roname),
                 min(pgactive.pgactive_format_slot_name(local_node.node_sysid,
                                                        local_node.node_timeline,
                                                        local_node.node_dboid,
                                                        ri.remote_dboid))
            FROM (SELECT roname FROM pg_catalog.pg_replication_origin
                  WHERE roname ~ '^pgactive_[0-9]+_[0-9]+_[0-9]+_[0-9]+_$'
                  OFFSET 0) ro,
                 pgactive.pgactive_parse_replident_name(ro.roname) ri
            WHERE ri.local_dboid = local_node.node_dboid
            INTO init_origins, init_roname, init_slot;

          IF init_origins = 1 THEN
            cleanup_hint := format('Then drop replication slot "%s" on the join_using_dsn node and replication origin "%s" on this node, drop and recreate this database, and try again.',
                                   init_slot, init_roname);
          ELSE
            cleanup_hint := 'Then drop the replication slot of this node on the join_using_dsn node and the replication origin for the join_using_dsn node on this node, drop and recreate this database, and try again.';
          END IF;

          IF init_error = 'pgactive_init_dump_failure' THEN
            RAISE EXCEPTION 'node % could not dump the database from the join_using_dsn node',
                            local_node.node_name
            USING DETAIL = 'pgactive_dump failed. Initialization of a node is not retried after a failure.',
            HINT = format('Check the server logs of this node and of the join_using_dsn node for the error. %s',
                          cleanup_hint);
          ELSIF init_error = 'pgactive_init_restore_failure' THEN
            RAISE EXCEPTION 'node % could not restore the dump of the database from the join_using_dsn node',
                            local_node.node_name
            USING DETAIL = 'pg_restore failed. Initialization of a node is not retried after a failure.',
            HINT = format('Check the server log of this node for the error. %s',
                          cleanup_hint);
          ELSE
            RAISE EXCEPTION 'node % could not be initialized from the join_using_dsn node',
                            local_node.node_name
            USING DETAIL = 'An earlier initialization attempt failed. Initialization of a node is not retried after a failure.',
            HINT = format('Check the server log of this node from that attempt for the error, and the server log of the join_using_dsn node if pgactive_dump failed. %s',
                          cleanup_hint);
          END IF;
        END IF;
      END IF;

      -- Look up the join_using_dsn node once, for the messages below, but
      -- not while a recorded failure waits for the worker to unregister.
      IF local_node.node_init_from_dsn IS NOT NULL AND NOT have_remote
         AND NOT init_failed THEN
        SELECT * FROM pgactive._pgactive_get_node_info_private(local_node.node_init_from_dsn)
          INTO remote_node;
        SELECT pg_size_pretty(remote_node.dbsize) INTO r_db;
        SELECT pg_database_size(local_node.node_dboid) INTO l_db_init_sz;
        have_remote := true;
      END IF;

      IF local_node.node_status = 'r' THEN
        IF remote_node IS NOT NULL THEN
          RAISE NOTICE
              USING MESSAGE = format('successfully joined the node and restored database ''%s'' from node %s',
                                     remote_node.dbname, remote_node.node_name);
        ELSE
          RAISE NOTICE 'successfully created first node in pgactive group';
        END IF;
        EXIT;
      END IF;

      IF timeout > 0 THEN
        t_lp_cnt := t_lp_cnt + sleep_sec;
        IF t_lp_cnt > timeout THEN
          RAISE EXCEPTION 'node % cannot reach ready state within % seconds, current state is %',
                          local_node.node_name, timeout, local_node.node_status;
        END IF;
      END IF;

      PERFORM pg_sleep( sleep_sec );
      w_lp_cnt := w_lp_cnt + sleep_sec;
      IF w_lp_cnt > worker_timeout THEN
        w_lp_cnt := 0;
        PERFORM PID FROM pg_stat_activity where application_name = 'pgactive:'|| local_node.node_sysid ||':perdb';
        IF NOT FOUND THEN
          RAISE EXCEPTION 'could not detect a running pgactive perdb worker, current node state is %',  local_node.node_status
          USING DETAIL = format( 'Either pgactive perdb worker exited due to an error or it did not start in %s seconds.', worker_timeout),
          HINT = 'Please check PostgreSQL log file for more details.';
        END IF;
      END IF;

      IF progress_interval > 0 AND have_remote THEN
        p_lp_cnt := p_lp_cnt + sleep_sec;

        IF p_lp_cnt > progress_interval THEN
          SELECT pg_database_size(local_node.node_dboid) INTO l_db_sz;
          IF l_db_sz = 0 OR l_db_sz = l_db_init_sz THEN
            RAISE NOTICE
                USING MESSAGE = format('transferring of database ''%s'' (%s) from node %s in progress',
                                       remote_node.dbname, r_db, remote_node.node_name);
          ELSE
            SELECT (l_db_sz/remote_node.dbsize) * 100 INTO p_pct;
            RAISE NOTICE
              USING MESSAGE = format('restoring database ''%s'', %s%% of %s complete',
                                     remote_node.dbname, p_pct, r_db);
          END IF;
          p_lp_cnt := 0;
        END IF;
      END IF;
    END LOOP;
END;
$body$;

REVOKE ALL ON FUNCTION pgactive_wait_for_node_ready(integer, integer) FROM public;

-- Finish Upgrade SQLs/Functions/Procedures
RESET pgactive.skip_ddl_replication;
RESET search_path;
