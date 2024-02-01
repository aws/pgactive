#!/usr/bin/env perl
#
# Tests that operate on a single pgactive node stand-alone, i.e.
# a pgactive group of size 1.
#
use strict;
use warnings;
use lib 'test/t/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use utils::nodemanagement;

my $node_a = PostgreSQL::Test::Cluster->new('node_a');

$node_a->init();
pgactive_update_postgresql_conf($node_a);
$node_a->start;

$node_a->safe_psql('postgres', qq{CREATE DATABASE $pgactive_test_dbname;});
$node_a->safe_psql($pgactive_test_dbname, q{CREATE EXTENSION pgactive;});

is($node_a->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()'), 'f',
	'pgactive is not active on node_a after create extension');

# Bring up a single pgactive node, stand-alone
create_pgactive_group($node_a);

is($node_a->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()'), 't',
	'pgactive is active on node_a after group create');

exec_ddl($node_a, q[CREATE TABLE public.reptest(id integer primary key, dummy text);]);

ok(!$node_a->psql($pgactive_test_dbname, "INSERT INTO reptest (id, dummy) VALUES (1, '42')"), 'simple DML succeeds');

is($node_a->safe_psql($pgactive_test_dbname, 'SELECT dummy FROM reptest WHERE id = 1'), '42', 'simple DDL and insert worked');

is($node_a->safe_psql($pgactive_test_dbname, "SELECT node_status FROM pgactive.pgactive_nodes WHERE node_name = pgactive.pgactive_get_local_node_name()"), 'r', 'node status is "r"');

ok(!$node_a->psql($pgactive_test_dbname, "SELECT pgactive.pgactive_detach_nodes(ARRAY['node_a'])"), 'detached without error');

is($node_a->safe_psql($pgactive_test_dbname, "SELECT node_status FROM pgactive.pgactive_nodes WHERE node_name = pgactive.pgactive_get_local_node_name()"), 'k', 'node status is "k"');

ok($node_a->psql($pgactive_test_dbname, "DROP EXTENSION pgactive"), 'DROP EXTENSION fails after detach');

is($node_a->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db();'), 't', 'still active after detach');

ok(!$node_a->psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_remove(true);'), 'pgactive_remove succeeds');

is($node_a->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db();'), 'f', 'not active after remove');

ok(!$node_a->psql($pgactive_test_dbname, 'DROP EXTENSION pgactive;'), 'extension dropped');

# Test old extension version entry points.
$node_a->safe_psql($pgactive_test_dbname, q{CREATE EXTENSION pgactive WITH VERSION '2.1.0';});

my $result210_expected = 'event trigger pgactive_truncate_trigger_add
foreign-data wrapper pgactive_fdw
function pgactive._pgactive_begin_join_private(text,text,text,text,boolean,boolean,boolean)
function pgactive._pgactive_destroy_temporary_dump_directories_private()
function pgactive._pgactive_generate_node_identifier_private()
function pgactive._pgactive_get_node_info_private(text,text)
function pgactive._pgactive_join_node_private(text,oid,oid,text,integer,text[])
function pgactive._pgactive_nid_shmem_reset_all_private()
function pgactive._pgactive_pause_worker_management_private(boolean)
function pgactive._pgactive_snowflake_id_nextval_private(regclass,bigint)
function pgactive._pgactive_update_seclabel_private()
function pgactive.check_file_system_mount_points(text,text)
function pgactive.get_free_disk_space(text)
function pgactive.get_last_applied_xact_info(text,oid,oid)
function pgactive.get_replication_lag_info()
function pgactive.has_required_privs()
function pgactive.pgactive_acquire_global_lock(text)
function pgactive.pgactive_apply_pause()
function pgactive.pgactive_apply_resume()
function pgactive.pgactive_assign_seq_ids_post_upgrade()
function pgactive.pgactive_connections_changed()
function pgactive.pgactive_conninfo_cmp(text,text)
function pgactive.pgactive_create_conflict_handler(regclass,name,regprocedure,pgactive.pgactive_conflict_type,interval)
function pgactive.pgactive_create_group(text,text,integer,text[])
function pgactive.pgactive_detach_nodes(text[])
function pgactive.pgactive_drop_conflict_handler(regclass,name)
function pgactive.pgactive_fdw_validator(text[],oid)
function pgactive.pgactive_format_replident_name(text,oid,oid,oid,name)
function pgactive.pgactive_format_slot_name(text,oid,oid,oid,name)
function pgactive.pgactive_get_connection_replication_sets(text)
function pgactive.pgactive_get_global_locks_info()
function pgactive.pgactive_get_local_node_name()
function pgactive.pgactive_get_local_nodeid()
function pgactive.pgactive_get_node_identifier()
function pgactive.pgactive_get_stats()
function pgactive.pgactive_get_table_replication_sets(regclass)
function pgactive.pgactive_get_workers_info()
function pgactive.pgactive_handle_rejoin()
function pgactive.pgactive_internal_create_truncate_trigger(regclass)
function pgactive.pgactive_is_active_in_db()
function pgactive.pgactive_is_apply_paused()
function pgactive.pgactive_join_group(text,text,text,integer,text[],boolean,boolean,boolean)
function pgactive.pgactive_min_remote_version_num()
function pgactive.pgactive_node_status_from_char("char")
function pgactive.pgactive_node_status_to_char(text)
function pgactive.pgactive_parse_replident_name(text)
function pgactive.pgactive_parse_slot_name(name)
function pgactive.pgactive_queue_truncate()
function pgactive.pgactive_remove(boolean)
function pgactive.pgactive_replicate_ddl_command(text)
function pgactive.pgactive_set_connection_replication_sets(text[],text)
function pgactive.pgactive_set_node_read_only(text,boolean)
function pgactive.pgactive_set_table_replication_sets(regclass,text[])
function pgactive.pgactive_skip_changes(text,oid,oid,pg_lsn)
function pgactive.pgactive_snowflake_id_nextval(regclass)
function pgactive.pgactive_terminate_workers(text,oid,oid,text)
function pgactive.pgactive_truncate_trigger_add()
function pgactive.pgactive_update_node_conninfo(text,text)
function pgactive.pgactive_variant()
function pgactive.pgactive_version()
function pgactive.pgactive_version_num()
function pgactive.pgactive_wait_for_node_ready(integer,integer)
function pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(name,pg_lsn)
function pgactive.pgactive_xact_replication_origin(xid)
schema pgactive
sequence pgactive.pgactive_conflict_history_id_seq
table pgactive.pgactive_conflict_handlers
table pgactive.pgactive_conflict_history
table pgactive.pgactive_connections
table pgactive.pgactive_global_locks
table pgactive.pgactive_nodes
table pgactive.pgactive_queued_commands
table pgactive.pgactive_queued_drops
table pgactive.pgactive_replication_set_config
type pgactive.dropped_object
type pgactive.pgactive_conflict_handler_action
type pgactive.pgactive_conflict_resolution
type pgactive.pgactive_conflict_type
type pgactive.pgactive_sync_type
view pgactive.pgactive_global_locks_info
view pgactive.pgactive_node_slots
view pgactive.pgactive_stats';

# List what version 2.1.0 contains.
my $result210 = $node_a->safe_psql($pgactive_test_dbname, q[\dx+ pgactive]);

is($result210, $result210_expected,
   'extension version 2.1.0 contains expected objects');

$node_a->safe_psql($pgactive_test_dbname, q{SET pgactive.skip_ddl_replication = true;});

# Move to new version 2.1.1.
$node_a->safe_psql($pgactive_test_dbname, q{ALTER EXTENSION pgactive UPDATE TO '2.1.1';});

my $result211_expected = 'event trigger pgactive_truncate_trigger_add
foreign-data wrapper pgactive_fdw
function pgactive._pgactive_begin_join_private(text,text,text,text,boolean,boolean,boolean)
function pgactive._pgactive_check_file_system_mount_points(text,text)
function pgactive._pgactive_destroy_temporary_dump_directories_private()
function pgactive._pgactive_generate_node_identifier_private()
function pgactive._pgactive_get_free_disk_space(text)
function pgactive._pgactive_get_node_info_private(text,text)
function pgactive._pgactive_has_required_privs()
function pgactive._pgactive_join_node_private(text,oid,oid,text,integer,text[])
function pgactive._pgactive_nid_shmem_reset_all_private()
function pgactive._pgactive_pause_worker_management_private(boolean)
function pgactive._pgactive_snowflake_id_nextval_private(regclass,bigint)
function pgactive._pgactive_update_seclabel_private()
function pgactive.has_required_privs()
function pgactive.pgactive_acquire_global_lock(text)
function pgactive.pgactive_apply_pause()
function pgactive.pgactive_apply_resume()
function pgactive.pgactive_assign_seq_ids_post_upgrade()
function pgactive.pgactive_connections_changed()
function pgactive.pgactive_conninfo_cmp(text,text)
function pgactive.pgactive_create_conflict_handler(regclass,name,regprocedure,pgactive.pgactive_conflict_type,interval)
function pgactive.pgactive_create_group(text,text,integer,text[])
function pgactive.pgactive_detach_nodes(text[])
function pgactive.pgactive_drop_conflict_handler(regclass,name)
function pgactive.pgactive_fdw_validator(text[],oid)
function pgactive.pgactive_format_replident_name(text,oid,oid,oid,name)
function pgactive.pgactive_format_slot_name(text,oid,oid,oid,name)
function pgactive.pgactive_get_connection_replication_sets(text)
function pgactive.pgactive_get_global_locks_info()
function pgactive.pgactive_get_last_applied_xact_info(text,oid,oid)
function pgactive.pgactive_get_local_node_name()
function pgactive.pgactive_get_local_nodeid()
function pgactive.pgactive_get_node_identifier()
function pgactive.pgactive_get_replication_lag_info()
function pgactive.pgactive_get_stats()
function pgactive.pgactive_get_table_replication_sets(regclass)
function pgactive.pgactive_get_workers_info()
function pgactive.pgactive_handle_rejoin()
function pgactive.pgactive_internal_create_truncate_trigger(regclass)
function pgactive.pgactive_is_active_in_db()
function pgactive.pgactive_is_apply_paused()
function pgactive.pgactive_join_group(text,text,text,integer,text[],boolean,boolean,boolean)
function pgactive.pgactive_min_remote_version_num()
function pgactive.pgactive_node_status_from_char("char")
function pgactive.pgactive_node_status_to_char(text)
function pgactive.pgactive_parse_replident_name(text)
function pgactive.pgactive_parse_slot_name(name)
function pgactive.pgactive_queue_truncate()
function pgactive.pgactive_remove(boolean)
function pgactive.pgactive_replicate_ddl_command(text)
function pgactive.pgactive_set_connection_replication_sets(text[],text)
function pgactive.pgactive_set_node_read_only(text,boolean)
function pgactive.pgactive_set_table_replication_sets(regclass,text[])
function pgactive.pgactive_skip_changes(text,oid,oid,pg_lsn)
function pgactive.pgactive_snowflake_id_nextval(regclass)
function pgactive.pgactive_terminate_workers(text,oid,oid,text)
function pgactive.pgactive_truncate_trigger_add()
function pgactive.pgactive_update_node_conninfo(text,text)
function pgactive.pgactive_variant()
function pgactive.pgactive_version()
function pgactive.pgactive_version_num()
function pgactive.pgactive_wait_for_node_ready(integer,integer)
function pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(name,pg_lsn)
function pgactive.pgactive_xact_replication_origin(xid)
schema pgactive
sequence pgactive.pgactive_conflict_history_id_seq
table pgactive.pgactive_conflict_handlers
table pgactive.pgactive_conflict_history
table pgactive.pgactive_connections
table pgactive.pgactive_global_locks
table pgactive.pgactive_nodes
table pgactive.pgactive_queued_commands
table pgactive.pgactive_queued_drops
table pgactive.pgactive_replication_set_config
type pgactive.dropped_object
type pgactive.pgactive_conflict_handler_action
type pgactive.pgactive_conflict_resolution
type pgactive.pgactive_conflict_type
type pgactive.pgactive_sync_type
view pgactive.pgactive_global_locks_info
view pgactive.pgactive_node_slots
view pgactive.pgactive_stats';

# List what version 2.1.1 contains.
my $result211 = $node_a->safe_psql($pgactive_test_dbname, q[\dx+ pgactive]);

is($result211, $result211_expected,
   'extension version 2.1.1 contains expected objects');

# Move to new version 2.1.2.
$node_a->safe_psql($pgactive_test_dbname, q{ALTER EXTENSION pgactive UPDATE TO '2.1.2';});

my $result212_expected = 'event trigger pgactive_truncate_trigger_add
foreign-data wrapper pgactive_fdw
function pgactive._pgactive_begin_join_private(text,text,text,text,boolean,boolean,boolean)
function pgactive._pgactive_check_file_system_mount_points(text,text)
function pgactive._pgactive_destroy_temporary_dump_directories_private()
function pgactive._pgactive_generate_node_identifier_private()
function pgactive._pgactive_get_free_disk_space(text)
function pgactive._pgactive_get_node_info_private(text,text)
function pgactive._pgactive_has_required_privs()
function pgactive._pgactive_join_node_private(text,oid,oid,text,integer,text[])
function pgactive._pgactive_nid_shmem_reset_all_private()
function pgactive._pgactive_pause_worker_management_private(boolean)
function pgactive._pgactive_snowflake_id_nextval_private(regclass,bigint)
function pgactive._pgactive_update_seclabel_private()
function pgactive.has_required_privs()
function pgactive.pgactive_acquire_global_lock(text)
function pgactive.pgactive_apply_pause()
function pgactive.pgactive_apply_resume()
function pgactive.pgactive_assign_seq_ids_post_upgrade()
function pgactive.pgactive_connections_changed()
function pgactive.pgactive_conninfo_cmp(text,text)
function pgactive.pgactive_create_conflict_handler(regclass,name,regprocedure,pgactive.pgactive_conflict_type,interval)
function pgactive.pgactive_create_group(text,text,integer,text[])
function pgactive.pgactive_detach_nodes(text[])
function pgactive.pgactive_drop_conflict_handler(regclass,name)
function pgactive.pgactive_fdw_validator(text[],oid)
function pgactive.pgactive_format_replident_name(text,oid,oid,oid,name)
function pgactive.pgactive_format_slot_name(text,oid,oid,oid,name)
function pgactive.pgactive_get_connection_replication_sets(text)
function pgactive.pgactive_get_global_locks_info()
function pgactive.pgactive_get_last_applied_xact_info(text,oid,oid)
function pgactive.pgactive_get_local_node_name()
function pgactive.pgactive_get_local_nodeid()
function pgactive.pgactive_get_node_identifier()
function pgactive.pgactive_get_replication_lag_info()
function pgactive.pgactive_get_stats()
function pgactive.pgactive_get_table_replication_sets(regclass)
function pgactive.pgactive_get_workers_info()
function pgactive.pgactive_handle_rejoin()
function pgactive.pgactive_internal_create_truncate_trigger(regclass)
function pgactive.pgactive_is_active_in_db()
function pgactive.pgactive_is_apply_paused()
function pgactive.pgactive_join_group(text,text,text,integer,text[],boolean,boolean,boolean)
function pgactive.pgactive_min_remote_version_num()
function pgactive.pgactive_node_status_from_char("char")
function pgactive.pgactive_node_status_to_char(text)
function pgactive.pgactive_parse_replident_name(text)
function pgactive.pgactive_parse_slot_name(name)
function pgactive.pgactive_queue_truncate()
function pgactive.pgactive_remove(boolean)
function pgactive.pgactive_replicate_ddl_command(text)
function pgactive.pgactive_set_connection_replication_sets(text[],text)
function pgactive.pgactive_set_node_read_only(text,boolean)
function pgactive.pgactive_set_table_replication_sets(regclass,text[])
function pgactive.pgactive_skip_changes(text,oid,oid,pg_lsn)
function pgactive.pgactive_snowflake_id_nextval(regclass)
function pgactive.pgactive_terminate_workers(text,oid,oid,text)
function pgactive.pgactive_truncate_trigger_add()
function pgactive.pgactive_update_node_conninfo(text,text)
function pgactive.pgactive_variant()
function pgactive.pgactive_version()
function pgactive.pgactive_version_num()
function pgactive.pgactive_wait_for_node_ready(integer,integer)
function pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(name,pg_lsn)
function pgactive.pgactive_xact_replication_origin(xid)
schema pgactive
sequence pgactive.pgactive_conflict_history_id_seq
table pgactive.pgactive_conflict_handlers
table pgactive.pgactive_conflict_history
table pgactive.pgactive_connections
table pgactive.pgactive_global_locks
table pgactive.pgactive_nodes
table pgactive.pgactive_queued_commands
table pgactive.pgactive_queued_drops
table pgactive.pgactive_replication_set_config
type pgactive.dropped_object
type pgactive.pgactive_conflict_handler_action
type pgactive.pgactive_conflict_resolution
type pgactive.pgactive_conflict_type
type pgactive.pgactive_sync_type
view pgactive.pgactive_global_locks_info
view pgactive.pgactive_node_slots
view pgactive.pgactive_stats';

# List what version 2.1.2 contains.
my $result212 = $node_a->safe_psql($pgactive_test_dbname, q[\dx+ pgactive]);

is($result212, $result212_expected,
   'extension version 2.1.2 contains expected objects');

done_testing();
