\c regression

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE TABLE public.test_replication(id integer not null primary key, atlsn pg_lsn default pg_current_wal_insert_lsn());
$DDL$);

INSERT INTO test_replication(id) VALUES (1);

-- Error cases
SELECT pgactive.pgactive_skip_changes(n.node_sysid, n.node_timeline, n.node_dboid, '0/0')
FROM pgactive.pgactive_nodes n
WHERE (n.node_sysid, n.node_timeline, n.node_dboid) != pgactive.pgactive_get_local_nodeid();

SET pgactive.skip_ddl_replication = on;

SELECT pgactive.pgactive_skip_changes(n.node_sysid, n.node_timeline, n.node_dboid, '0/0')
FROM pgactive.pgactive_nodes n
WHERE (n.node_sysid, n.node_timeline, n.node_dboid) != pgactive.pgactive_get_local_nodeid();

-- Access a bogus node.
-- Needs a wrapper because of the dynamic content in the error message.
\set VERBOSITY terse

DO LANGUAGE plpgsql
$$
DECLARE
  errm text;
BEGIN
  PERFORM pgactive.pgactive_skip_changes('0', 0, 1234, '0/1');
EXCEPTION
  WHEN others THEN
    GET STACKED DIAGNOSTICS
       errm = MESSAGE_TEXT;
    IF errm LIKE 'replication origin "pgactive_0_0_%" does not exist' THEN
      RAISE EXCEPTION 'Got expected error from pgactive.pgactive_skip_changes()';
    ELSE
      RAISE;
    END IF;
END;
$$;

SELECT pgactive.pgactive_skip_changes(n.node_sysid, n.node_timeline, n.node_dboid, '0/1')
FROM pgactive.pgactive_nodes n
WHERE (n.node_sysid, n.node_timeline, n.node_dboid) = pgactive.pgactive_get_local_nodeid();

-- Skipping the past must do nothing. The LSN isn't exposed in
-- pg_replication_identifier so this'll just produce no visible result, but not
-- break anything.
SELECT pgactive.pgactive_skip_changes(n.node_sysid, n.node_timeline, n.node_dboid, '0/1')
FROM pgactive.pgactive_nodes n
WHERE (n.node_sysid, n.node_timeline, n.node_dboid) != pgactive.pgactive_get_local_nodeid();
