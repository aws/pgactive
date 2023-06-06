\c regression

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE TABLE public.test_replication(id integer not null primary key, atlsn pg_lsn default pg_current_wal_insert_lsn());
$DDL$);

INSERT INTO test_replication(id) VALUES (1);

-- Error cases
SELECT bdr.skip_changes_upto(n.node_sysid, n.node_timeline, n.node_dboid, '0/0')
FROM bdr.bdr_nodes n
WHERE (n.node_sysid, n.node_timeline, n.node_dboid) != bdr.bdr_get_local_nodeid();

SET bdr.permit_unsafe_ddl_commands = on;

SELECT bdr.skip_changes_upto(n.node_sysid, n.node_timeline, n.node_dboid, '0/0')
FROM bdr.bdr_nodes n
WHERE (n.node_sysid, n.node_timeline, n.node_dboid) != bdr.bdr_get_local_nodeid();

-- Access a bogus node.
-- Needs a wrapper because of the dynamic content in the error message.
\set VERBOSITY terse

DO LANGUAGE plpgsql
$$
DECLARE
  errm text;
BEGIN
  PERFORM bdr.skip_changes_upto('0', 0, 1234, '0/1');
EXCEPTION
  WHEN others THEN
    GET STACKED DIAGNOSTICS
       errm = MESSAGE_TEXT;
    IF errm LIKE 'replication origin "bdr_0_0_%" does not exist' THEN
      RAISE EXCEPTION 'Got expected error from bdr.skip_changes_upto()';
    ELSE
      RAISE;
    END IF;
END;
$$;

SELECT bdr.skip_changes_upto(n.node_sysid, n.node_timeline, n.node_dboid, '0/1')
FROM bdr.bdr_nodes n
WHERE (n.node_sysid, n.node_timeline, n.node_dboid) = bdr.bdr_get_local_nodeid();

-- Skipping the past must do nothing. The LSN isn't exposed in
-- pg_replication_identifier so this'll just produce no visible result, but not
-- break anything.
SELECT bdr.skip_changes_upto(n.node_sysid, n.node_timeline, n.node_dboid, '0/1')
FROM bdr.bdr_nodes n
WHERE (n.node_sysid, n.node_timeline, n.node_dboid) != bdr.bdr_get_local_nodeid();

INSERT INTO test_replication(id) VALUES (2);

-- Create a situation where a commit will constantly fail to replay until skipped.
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);

\c postgres
SELECT id FROM test_replication ORDER BY id;

\c regression

-- This should become visible on the downstream
INSERT INTO test_replication(id) VALUES (3);

BEGIN;
SET LOCAL bdr.skip_ddl_replication = on;
SET LOCAL bdr.skip_ddl_locking = on;
CREATE TABLE break_me(blah text);
COMMIT;

-- So should this
INSERT INTO test_replication(id) VALUES (4);

SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);

-- break replication
BEGIN;
SET LOCAL bdr.skip_ddl_locking = on;
SELECT bdr.bdr_replicate_ddl_command($DDL$
DROP TABLE public.break_me;
$DDL$);
COMMIT;

-- Should never be seen on downstream, since it won't replay past the broken
-- DDL and we'll drop changes from the broken DDL until after it since we take
-- the skip-to position after this statement runs.
INSERT INTO test_replication(id) VALUES (5);

SELECT pg_sleep(1);

-- Most likely the other side's apply worker just died and will keep reconnecting
-- then immediately dying when it tries to replay the bad statement. We can't use
-- pg_xlog_wait_remote_apply(..) with a timeout because it won't wait for a worker
-- that isn't there. So we can't usefully wait to ensure there's no catchup.


\c postgres
-- Entry 5 shouldn't be visible since we can't replay past the broken
-- DDL
SELECT id FROM test_replication ORDER BY id;

\c regression

-- Better tell the downstream to discard changes up to and including the
-- problem commit. This would be easier in a real programming language not
-- psql, but we have \gset at least.
SELECT pg_current_wal_insert_lsn() AS lsn_to_skip_to \gset
SELECT * FROM bdr.bdr_get_local_nodeid() \gset regression_

-- This is committed after the point we'll skip to, and should
-- be replayed on the downstream, so test_replication will contain
-- 1, 2, 3, 4, 6
INSERT INTO test_replication(id) VALUES (6);

\c postgres

SELECT id FROM test_replication ORDER BY id;

BEGIN;
SET LOCAL bdr.permit_unsafe_ddl_commands = on;
SELECT bdr.skip_changes_upto(:'regression_sysid', :regression_timeline, :regression_dboid, :'lsn_to_skip_to');
COMMIT;

\c regression

-- It should catch up now. We can't pg_xlog_wait_remote_apply() since the apply worker
-- might not have started back up after the last failure; wait 5s (the restart interval),
-- make sure there are two workers, and wait for catchup.
SELECT pg_sleep(12);
SELECT count(*) FROM pg_stat_replication;
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);

\c postgres

-- Should see all rows 1-4 and 6 now
SELECT id FROM test_replication ORDER BY id;

\c regression

-- on the upstream we should see all rows 1-6, since
-- we never deleted 5, just skipped replicating it.
SELECT id FROM test_replication ORDER BY id;
