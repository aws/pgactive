\c postgres

SELECT bdr.bdr_is_active_in_db();

SELECT bdr.bdr_create_group(
	local_node_name := 'node-pg',
	node_external_dsn := 'dbname=postgres',
	replication_sets := ARRAY['default', 'important', 'for-node-1']
	);

SELECT bdr.bdr_is_active_in_db();

SELECT bdr.bdr_wait_for_node_ready();

SELECT bdr.bdr_is_active_in_db();

\c regression

SELECT bdr.bdr_is_active_in_db();

SELECT bdr.bdr_join_group(
	local_node_name := 'node-regression',
	node_external_dsn := 'dbname=regression',
	join_using_dsn := 'dbname=postgres',
	node_local_dsn := 'dbname=regression',
	replication_sets := ARRAY['default', 'important', 'for-node-2', 'for-node-2-insert', 'for-node-2-update', 'for-node-2-delete']
	);

SELECT bdr.bdr_is_active_in_db();

SELECT * FROM  bdr.bdr_get_global_locks_info();

-- Silence dynamic messages here
SET client_min_messages = 'ERROR';
SELECT bdr.bdr_wait_for_node_ready();
RESET client_min_messages;

SELECT bdr.bdr_is_active_in_db();

SELECT owner_replorigin, (owner_sysid, owner_timeline, owner_dboid) = bdr.bdr_get_local_nodeid(), lock_mode, lock_state, owner_local_pid = pg_backend_pid() AS owner_pid_is_me, lockcount, npeers, npeers_confirmed, npeers_declined, npeers_replayed, replay_upto IS NOT NULL AS has_replay_upto FROM bdr.bdr_get_global_locks_info();

-- Make sure we see two slots and two active connections
SELECT plugin, slot_type, database, active FROM pg_replication_slots;
SELECT count(*) FROM pg_stat_replication;

\c postgres
SELECT conn_dsn, conn_replication_sets FROM bdr.bdr_connections ORDER BY conn_dsn;
SELECT node_status, node_local_dsn, node_init_from_dsn FROM bdr.bdr_nodes ORDER BY node_local_dsn;

SELECT 1 FROM pg_replication_slots WHERE restart_lsn <= confirmed_flush_lsn;

\c regression
SELECT conn_dsn, conn_replication_sets FROM bdr.bdr_connections ORDER BY conn_dsn;
SELECT node_status, node_local_dsn, node_init_from_dsn FROM bdr.bdr_nodes ORDER BY node_local_dsn;

SELECT 1 FROM pg_replication_slots WHERE restart_lsn <= confirmed_flush_lsn;

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE VIEW public.ddl_info AS
SELECT
	owner_replorigin,
	(owner_sysid, owner_timeline, owner_dboid) = bdr.bdr_get_local_nodeid() AS is_my_node,
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
FROM bdr.bdr_get_global_locks_info();
$DDL$);

BEGIN;

SELECT * FROM ddl_info;

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE OR REPLACE FUNCTION public.bdr_regress_variables(
    OUT readdb1 text,
    OUT readdb2 text,
    OUT writedb1 text,
    OUT writedb2 text
    ) RETURNS record LANGUAGE SQL AS $f$
SELECT
    current_setting('bdrtest.readdb1'),
    current_setting('bdrtest.readdb2'),
    current_setting('bdrtest.writedb1'),
    current_setting('bdrtest.writedb2')
$f$;
$DDL$);

SELECT * FROM ddl_info;

COMMIT;

SELECT * FROM ddl_info;

-- Run the upgrade function, even though we started with 2.0, so we exercise it
-- and so we know it won't break things when run on a 2.0 cluster.
SELECT bdr.bdr_assign_seq_ids_post_upgrade();
