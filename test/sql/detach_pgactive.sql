\set VERBOSITY terse
\c regression

-- Create a funnily named table and sequence for use during node
-- detach testing.

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE SCHEMA "some $SCHEMA";
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE TABLE "some $SCHEMA"."table table table" ("a column" integer);
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
DROP VIEW public.ddl_info;
$DDL$);

-- Dropping the pgactive extension isn't allowed while pgactive is active
DROP EXTENSION pgactive;

-- Initial state
SELECT node_name, node_status FROM pgactive.pgactive_nodes ORDER BY node_name;

-- You can't detach your own node
SELECT pgactive.pgactive_detach_nodes(ARRAY['node-regression']);

-- Or a nonexistent node
SELECT pgactive.pgactive_detach_nodes(ARRAY['node-nosuch']);

-- Nothing has changed
SELECT node_name, node_status FROM pgactive.pgactive_nodes ORDER BY node_name;

-- This detach should successfully remove the node
SELECT pgactive.pgactive_detach_nodes(ARRAY['node-pg']);

SELECT pgactive.pgactive_is_active_in_db();

-- We can tell a detach has taken effect when the downstream's (node-pg) slot
-- vanishes on the upstream (node-regression).
DO
$$
DECLARE
    timeout integer := 180;
BEGIN
    WHILE timeout > 0
    LOOP
        IF (SELECT count(*) FROM pgactive.pgactive_node_slots WHERE node_name = 'node-pg') = 0 THEN
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

-- Status of the downstream node on upstream node after detach is 'k'
SELECT node_status FROM pgactive.pgactive_nodes WHERE node_name = 'node-pg'; -- 'k'

\c postgres

-- It is unsafe/incorrect to expect the detached node to know it's detached and
-- have a 'k' state. Sometimes it will, sometimes it won't, it depends on a
-- race between the detaching node terminating its connections and it
-- receiving notification of its own detaching. That's a bit of a wart in pgactive,
-- but won't be fixed in 2.0 and is actually very hard to truly "fix" in a
-- distributed system. So we allow the local node status to be 'k' or 'r'.
SELECT COUNT(*) = 1 AS OK FROM pgactive.pgactive_nodes
    WHERE node_name = 'node-pg' AND node_status IN('k', 'r');  -- 'k' or 'r'

\c regression

-- The downstream's slot on the upstream MUST be gone
SELECT COUNT(*) = 0 AS OK FROM pgactive.pgactive_node_slots WHERE node_name = 'node-pg'; -- EMPTY

\c postgres

-- The upstream's slot on the downstream MAY be gone, or may be present, so
-- there's no point checking. But the upstream's connection to the downstream
-- MUST be gone, so we can look for the apply worker's connection.
SELECT count(*) FROM pg_stat_activity WHERE application_name = 'node-regression:send'; -- EMPTY

\c regression

-- If we try to detach the same node again its state won't be 'r'
-- so a warning will be generated.
SELECT pgactive.pgactive_detach_nodes(ARRAY['node-pg']);

-- pgactive is detached, but not fully removed, so don't allow the extension
-- to be dropped yet.
DROP EXTENSION pgactive;

SELECT pgactive.pgactive_is_active_in_db();

-- Strip pgactive from this node entirely and convert global sequences to local.
BEGIN;
-- We silence notice messages here as some of them depend on when pgactive workers
-- on the detached node 'node-pg' are gone.
SET LOCAL client_min_messages = 'ERROR';
SELECT pgactive.pgactive_remove(true);
COMMIT;

SELECT pgactive.pgactive_is_active_in_db();

-- Should be able to drop the extension now
--
-- This would cascade-drop any triggers that we hadn't already
-- dropped in pgactive_remove()
--
DROP EXTENSION pgactive;
