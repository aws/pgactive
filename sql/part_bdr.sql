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

-- We can tell a part has taken effect when the downstream's (node-pg) slot
-- vanishes on the upstream (node-regression).
DO
$$
DECLARE
    timeout integer := 180;
BEGIN
    WHILE timeout > 0
    LOOP
        IF (SELECT count(*) FROM bdr.bdr_node_slots WHERE node_name = 'node-pg') = 0 THEN
            RAISE NOTICE 'Downstream replication slot vanished on the upstream';
            EXIT;
        END IF;
        PERFORM pg_sleep(1);
        timeout := timeout - 1;
    END LOOP;
    IF timeout = 0 THEN
        RAISE EXCEPTION 'Timed out waiting for replication disconnect';
    END IF;
END;
$$
LANGUAGE plpgsql;

-- Status of the downstream node on upstream node after part is 'k'
SELECT node_status FROM bdr.bdr_nodes WHERE node_name = 'node-pg'; -- 'k'

\c postgres

-- It is unsafe/incorrect to expect the parted node to know it's parted and
-- have a 'k' state. Sometimes it will, sometimes it won't, it depends on a
-- race between the parting node terminating its connections and it
-- receiving notification of its own parting. That's a bit of a wart in BDR,
-- but won't be fixed in 2.0 and is actually very hard to truly "fix" in a
-- distributed system. So we allow the local node status to be 'k' or 'r'.
SELECT COUNT(*) = 1 AS OK FROM bdr.bdr_nodes
    WHERE node_name = 'node-pg' AND node_status IN('k', 'r');  -- 'k' or 'r'

\c regression

-- The downstream's slot on the upstream MUST be gone
SELECT * FROM bdr.bdr_node_slots WHERE node_name = 'node-pg'; -- EMPTY

\c postgres

-- The upstream's slot on the downstream MAY be gone, or may be present, so
-- there's no point checking. But the upstream's connection to the downstream
-- MUST be gone, so we can look for the apply worker's connection.
SELECT * FROM pg_stat_activity WHERE application_name = 'node-regression:send'; -- EMPTY

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
