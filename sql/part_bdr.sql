\set VERBOSITY terse
\c regression

-- Create a funnily named table and sequence for use during node
-- part testing.

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE SCHEMA "some $SCHEMA";
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE TABLE "some $SCHEMA"."table table table" ("a column" integer);
$DDL$);

-- Also for dependency testing, a global sequence if supported
DO LANGUAGE plpgsql $$
BEGIN
  IF bdr.have_global_sequences() THEN
    EXECUTE $DDL$CREATE SEQUENCE "some $SCHEMA"."some ""sequence"" name" USING bdr;$DDL$;
  END IF;
END;
$$;

SELECT bdr.bdr_replicate_ddl_command($DDL$
DROP VIEW public.ddl_info;
$DDL$);

-- Dropping the BDR extension isn't allowed while BDR is active
DROP EXTENSION bdr;

-- Initial state
SELECT node_name, node_status FROM bdr.bdr_nodes ORDER BY node_name;

-- You can't part your own node
SELECT bdr.bdr_part_by_node_names(ARRAY['node-regression']);

-- Or a nonexistent node
SELECT bdr.bdr_part_by_node_names(ARRAY['node-nosuch']);

-- Unsubscribe must also fail, since this is a BDR connection
SELECT bdr.bdr_unsubscribe('node-pg');

-- Nothing has changed
SELECT node_name, node_status FROM bdr.bdr_nodes ORDER BY node_name;

-- This part should successfully remove the node
SELECT bdr.bdr_part_by_node_names(ARRAY['node-pg']);

SELECT bdr.bdr_is_active_in_db();

-- Wait 'till all connections gone...
DO
$$
DECLARE
    timeout integer := 60;
BEGIN
    WHILE timeout > 0
    LOOP
        IF (SELECT count(*) FROM pg_stat_replication) = 0 THEN
            RAISE NOTICE 'All connections dropped';
            EXIT;
        END IF;
        PERFORM pg_sleep(1);
        PERFORM pg_stat_clear_snapshot();
        timeout := timeout - 1;
    END LOOP;
    IF timeout = 0 THEN
        RAISE EXCEPTION 'Timed out waiting for replication disconnect';
    END IF;
END;
$$
LANGUAGE plpgsql;

-- Wait 'till all slots gone
DO
$$
DECLARE
    timeout integer := 60;
BEGIN
    WHILE timeout > 0
    LOOP
        IF (SELECT count(*) FROM pg_replication_slots) = 0 THEN
            RAISE NOTICE 'All slots dropped';
            EXIT;
        END IF;
        PERFORM pg_sleep(1);
        PERFORM pg_stat_clear_snapshot();
        timeout := timeout - 1;
    END LOOP;
    IF timeout = 0 THEN
        RAISE EXCEPTION 'Timed out waiting for slot drop';
    END IF;
END;
$$
LANGUAGE plpgsql;

-- There should now be zero slots and no connections to them
SELECT count(*) FROM pg_stat_replication;
SELECT slot_name, plugin, slot_type, datoid, database, active, xmin, catalog_xmin, restart_lsn, confirmed_flush_lsn FROM pg_replication_slots;

-- Zero active connections
SELECT count(*) FROM pg_stat_replication;
-- and the node state for the removed node should show 'k'
SELECT node_name, node_status FROM bdr.bdr_nodes ORDER BY node_name;

\c postgres
-- ... on both nodes.
SELECT node_name, node_status FROM bdr.bdr_nodes ORDER BY node_name;

\c regression

-- If we try to part the same node again its state won't be 'r'
-- so a warning will be generated.
SELECT bdr.bdr_part_by_node_names(ARRAY['node-pg']);

-- BDR is parted, but not fully removed, so don't allow the extension
-- to be dropped yet.
DROP EXTENSION bdr;

SELECT bdr.bdr_is_active_in_db();

-- Strip BDR from this node entirely and convert global sequences to local.
BEGIN;
SET LOCAL client_min_messages = 'notice';
SELECT bdr.remove_bdr_from_local_node(true, true);
COMMIT;

SELECT bdr.bdr_is_active_in_db();

-- Should be able to drop the extension now
--
-- This would cascade-drop any triggers that we hadn't already
-- dropped in remove_bdr_from_local_node()
--
DROP EXTENSION bdr;
