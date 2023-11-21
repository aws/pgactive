\c postgres

SELECT pgactive.pgactive_is_active_in_db();

\set VERBOSITY terse

-- Verify an error case with the use of non-pgactive foreign data wrapper
CREATE FOREIGN DATA WRAPPER dummy;
CREATE SERVER dummy_fs FOREIGN DATA WRAPPER dummy;
CREATE USER MAPPING FOR public SERVER dummy_fs;

SELECT pgactive.pgactive_create_group(node_name := 'dummy_node',
	node_dsn := 'user_mapping=public pgactive_foreign_server=dummy_fs',
	replication_sets := ARRAY['for_dummy_node']);

\set VERBOSITY default

SELECT pgactive.pgactive_create_group(
	node_name := 'node-pg',
	node_dsn := 'dbname=postgres',
	replication_sets := ARRAY['default', 'important', 'for-node-1']
	);

SELECT pgactive.pgactive_is_active_in_db();

SELECT pgactive.pgactive_wait_for_node_ready();

SELECT pgactive.pgactive_is_active_in_db();

\c regression

SELECT pgactive.pgactive_is_active_in_db();

SELECT pgactive.pgactive_join_group(
	node_name := 'node-regression',
	node_dsn := 'dbname=regression',
	join_using_dsn := 'dbname=postgres',
	replication_sets := ARRAY['default', 'important', 'for-node-2', 'for-node-2-insert', 'for-node-2-update', 'for-node-2-delete']
	);

SELECT pgactive.pgactive_is_active_in_db();

SELECT * FROM  pgactive.pgactive_get_global_locks_info();

-- Silence dynamic messages here
SET client_min_messages = 'ERROR';
SELECT pgactive.pgactive_wait_for_node_ready();
RESET client_min_messages;

SELECT pgactive.pgactive_is_active_in_db();

SELECT owner_replorigin, (owner_sysid, owner_timeline, owner_dboid) = pgactive.pgactive_get_local_nodeid(), lock_mode, lock_state, owner_local_pid = pg_backend_pid() AS owner_pid_is_me, lockcount, npeers, npeers_confirmed, npeers_declined, npeers_replayed, replay_upto IS NOT NULL AS has_replay_upto FROM pgactive.pgactive_get_global_locks_info();

-- Make sure we see two slots and two active connections
SELECT plugin, slot_type, database, active FROM pg_replication_slots;
SELECT count(*) FROM pg_stat_replication;

\c postgres
SELECT conn_dsn, conn_replication_sets FROM pgactive.pgactive_connections ORDER BY conn_dsn;
SELECT node_status, node_dsn, node_init_from_dsn FROM pgactive.pgactive_nodes ORDER BY node_dsn;

SELECT 1 FROM pg_replication_slots WHERE restart_lsn <= confirmed_flush_lsn;

\c regression
SELECT conn_dsn, conn_replication_sets FROM pgactive.pgactive_connections ORDER BY conn_dsn;
SELECT node_status, node_dsn, node_init_from_dsn FROM pgactive.pgactive_nodes ORDER BY node_dsn;

SELECT 1 FROM pg_replication_slots WHERE restart_lsn <= confirmed_flush_lsn;

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE VIEW public.ddl_info AS
SELECT
	owner_replorigin,
	(owner_sysid, owner_timeline, owner_dboid) = pgactive.pgactive_get_local_nodeid() AS is_my_node,
	lock_mode,
	lock_state,
	owner_local_pid IS NOT NULL AS owner_pid_set,
	owner_local_pid = pg_backend_pid() AS owner_pid_is_me,
	lock_state = 'acquire_acquired' AND owner_local_pid = pg_backend_pid() AS fully_owned_by_me,
	lockcount,
	npeers,
	npeers_confirmed,
	npeers_declined,
	npeers_replayed,
	replay_upto IS NOT NULL AS has_replay_upto
FROM pgactive.pgactive_get_global_locks_info();
$DDL$);

BEGIN;

SELECT * FROM ddl_info;

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE OR REPLACE FUNCTION public.pgactive_regress_variables(
    OUT readdb1 text,
    OUT readdb2 text,
    OUT writedb1 text,
    OUT writedb2 text
    ) RETURNS record LANGUAGE SQL AS $f$
SELECT
    current_setting('pgactivetest.readdb1'),
    current_setting('pgactivetest.readdb2'),
    current_setting('pgactivetest.writedb1'),
    current_setting('pgactivetest.writedb2')
$f$;
$DDL$);

SELECT * FROM ddl_info;

COMMIT;

SELECT * FROM ddl_info;

-- Run the upgrade function, even though we started with 2.0, so we exercise it
-- and so we know it won't break things when run on a 2.0 cluster.
SELECT pgactive.pgactive_assign_seq_ids_post_upgrade();

-- Verify utility functions to handle pgactive statuses
SELECT
  c::"char" AS status_char,
  pgactive.pgactive_node_status_from_char(c::"char") AS status_str,
  pgactive.pgactive_node_status_to_char(pgactive.pgactive_node_status_from_char(c::"char")) AS roundtrip_char
FROM (VALUES ('b'),('i'),('c'),('o'),('r'),('k')) x(c)
ORDER BY c;

-- Verify that there are some stats already
SELECT COUNT(*) > 0 AS ok FROM pgactive.pgactive_stats;
