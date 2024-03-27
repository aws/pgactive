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

my $pg_version = $node_a->safe_psql('postgres', qq{SHOW server_version;});

my $major_version =  substr($pg_version, 0, 2);

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

# pgactive tables' attributes must never change with schema changes.
# Only new attributes can be appended and only if nullable.
my $result_expected = 'pgactive.pgactive_conflict_handlers|1|ch_name|f|name|t
pgactive.pgactive_conflict_handlers|2|ch_type|f|pgactive.pgactive_conflict_type|t
pgactive.pgactive_conflict_handlers|3|ch_reloid|f|oid|t
pgactive.pgactive_conflict_handlers|4|ch_fun|f|text|t
pgactive.pgactive_conflict_handlers|5|ch_timeframe|f|interval|f
pgactive.pgactive_conflict_history|1|conflict_id|f|bigint|t
pgactive.pgactive_conflict_history|2|local_node_sysid|f|text|t
pgactive.pgactive_conflict_history|3|local_conflict_xid|f|xid|t
pgactive.pgactive_conflict_history|4|local_conflict_lsn|f|pg_lsn|t
pgactive.pgactive_conflict_history|5|local_conflict_time|f|timestamp with time zone|t
pgactive.pgactive_conflict_history|6|object_schema|f|text|f
pgactive.pgactive_conflict_history|7|object_name|f|text|f
pgactive.pgactive_conflict_history|8|remote_node_sysid|f|text|t
pgactive.pgactive_conflict_history|9|remote_txid|f|xid|t
pgactive.pgactive_conflict_history|10|remote_commit_time|f|timestamp with time zone|t
pgactive.pgactive_conflict_history|11|remote_commit_lsn|f|pg_lsn|t
pgactive.pgactive_conflict_history|12|conflict_type|f|pgactive.pgactive_conflict_type|t
pgactive.pgactive_conflict_history|13|conflict_resolution|f|pgactive.pgactive_conflict_resolution|t
pgactive.pgactive_conflict_history|14|local_tuple|f|json|f
pgactive.pgactive_conflict_history|15|remote_tuple|f|json|f
pgactive.pgactive_conflict_history|16|local_tuple_xmin|f|xid|f
pgactive.pgactive_conflict_history|17|local_tuple_origin_sysid|f|text|f
pgactive.pgactive_conflict_history|18|error_message|f|text|f
pgactive.pgactive_conflict_history|19|error_sqlstate|f|text|f
pgactive.pgactive_conflict_history|20|error_querystring|f|text|f
pgactive.pgactive_conflict_history|21|error_cursorpos|f|integer|f
pgactive.pgactive_conflict_history|22|error_detail|f|text|f
pgactive.pgactive_conflict_history|23|error_hint|f|text|f
pgactive.pgactive_conflict_history|24|error_context|f|text|f
pgactive.pgactive_conflict_history|25|error_columnname|f|text|f
pgactive.pgactive_conflict_history|26|error_typename|f|text|f
pgactive.pgactive_conflict_history|27|error_constraintname|f|text|f
pgactive.pgactive_conflict_history|28|error_filename|f|text|f
pgactive.pgactive_conflict_history|29|error_lineno|f|integer|f
pgactive.pgactive_conflict_history|30|error_funcname|f|text|f
pgactive.pgactive_conflict_history|31|remote_node_timeline|f|oid|f
pgactive.pgactive_conflict_history|32|remote_node_dboid|f|oid|f
pgactive.pgactive_conflict_history|33|local_tuple_origin_timeline|f|oid|f
pgactive.pgactive_conflict_history|34|local_tuple_origin_dboid|f|oid|f
pgactive.pgactive_conflict_history|35|local_commit_time|f|timestamp with time zone|f
pgactive.pgactive_connections|1|conn_sysid|f|text|t
pgactive.pgactive_connections|2|conn_timeline|f|oid|t
pgactive.pgactive_connections|3|conn_dboid|f|oid|t
pgactive.pgactive_connections|4|conn_dsn|f|text|t
pgactive.pgactive_connections|5|conn_apply_delay|f|integer|f
pgactive.pgactive_connections|6|conn_replication_sets|f|text[]|f
pgactive.pgactive_global_locks|1|locktype|f|text|t
pgactive.pgactive_global_locks|2|owning_sysid|f|text|t
pgactive.pgactive_global_locks|3|owning_timeline|f|oid|t
pgactive.pgactive_global_locks|4|owning_datid|f|oid|t
pgactive.pgactive_global_locks|5|owner_created_lock_at|f|pg_lsn|t
pgactive.pgactive_global_locks|6|acquired_sysid|f|text|t
pgactive.pgactive_global_locks|7|acquired_timeline|f|oid|t
pgactive.pgactive_global_locks|8|acquired_datid|f|oid|t
pgactive.pgactive_global_locks|9|acquired_lock_at|f|pg_lsn|f
pgactive.pgactive_global_locks|10|state|f|text|t
pgactive.pgactive_nodes|1|node_sysid|f|text|t
pgactive.pgactive_nodes|2|node_timeline|f|oid|t
pgactive.pgactive_nodes|3|node_dboid|f|oid|t
pgactive.pgactive_nodes|4|node_status|f|"char"|t
pgactive.pgactive_nodes|5|node_name|f|text|t
pgactive.pgactive_nodes|6|node_dsn|f|text|f
pgactive.pgactive_nodes|7|node_init_from_dsn|f|text|f
pgactive.pgactive_nodes|8|node_read_only|f|boolean|f
pgactive.pgactive_nodes|9|node_seq_id|f|smallint|f
pgactive.pgactive_queued_commands|1|lsn|f|pg_lsn|t
pgactive.pgactive_queued_commands|2|queued_at|f|timestamp with time zone|t
pgactive.pgactive_queued_commands|3|perpetrator|f|text|t
pgactive.pgactive_queued_commands|4|command_tag|f|text|t
pgactive.pgactive_queued_commands|5|command|f|text|t
pgactive.pgactive_queued_commands|6|search_path|f|text|f
pgactive.pgactive_queued_drops|1|lsn|f|pg_lsn|t
pgactive.pgactive_queued_drops|2|queued_at|f|timestamp with time zone|t
pgactive.pgactive_queued_drops|3|dropped_objects|f|pgactive.dropped_object[]|t
pgactive.pgactive_replication_set_config|1|set_name|f|name|t
pgactive.pgactive_replication_set_config|2|replicate_inserts|f|boolean|t
pgactive.pgactive_replication_set_config|3|replicate_updates|f|boolean|t
pgactive.pgactive_replication_set_config|4|replicate_deletes|f|boolean|t';

my $query = qq[SELECT attrelid::regclass::text, attnum, attname, attisdropped, atttypid::regtype, attnotnull
FROM pg_attribute WHERE attrelid = ANY (ARRAY[
	'pgactive.pgactive_nodes', 'pgactive.pgactive_connections', 'pgactive.pgactive_queued_drops',
	'pgactive.pgactive_queued_commands', 'pgactive.pgactive_global_locks',
	'pgactive.pgactive_conflict_handlers', 'pgactive.pgactive_conflict_history',
	'pgactive.pgactive_replication_set_config']::regclass[])
	AND attnum >= 1
ORDER BY attrelid, attnum;
;];

my $result = $node_a->safe_psql($pgactive_test_dbname, $query);
is($result, $result_expected, "pgactive tables' attributes haven't changed");
ok(!$node_a->psql($pgactive_test_dbname, 'DROP EXTENSION pgactive;'), 'extension dropped');

# Test old extension version entry points.
$node_a->safe_psql($pgactive_test_dbname, q{CREATE EXTENSION pgactive WITH VERSION '2.1.0';});

my $result210_expected = 'event trigger pgactive_truncate_trigger_add
foreign-data wrapper pgactive_fdw
function pgactive.check_file_system_mount_points(text,text)
function pgactive.get_free_disk_space(text)
function pgactive.get_last_applied_xact_info(text,oid,oid)
function pgactive.get_replication_lag_info()
function pgactive.has_required_privs()
function pgactive.pgactive_acquire_global_lock(text)
function pgactive.pgactive_apply_pause()
function pgactive.pgactive_apply_resume()
function pgactive.pgactive_assign_seq_ids_post_upgrade()
function pgactive._pgactive_begin_join_private(text,text,text,text,boolean,boolean,boolean)
function pgactive.pgactive_connections_changed()
function pgactive.pgactive_conninfo_cmp(text,text)
function pgactive.pgactive_create_conflict_handler(regclass,name,regprocedure,pgactive.pgactive_conflict_type,interval)
function pgactive.pgactive_create_group(text,text,integer,text[])
function pgactive._pgactive_destroy_temporary_dump_directories_private()
function pgactive.pgactive_detach_nodes(text[])
function pgactive.pgactive_drop_conflict_handler(regclass,name)
function pgactive.pgactive_fdw_validator(text[],oid)
function pgactive.pgactive_format_replident_name(text,oid,oid,oid,name)
function pgactive.pgactive_format_slot_name(text,oid,oid,oid,name)
function pgactive._pgactive_generate_node_identifier_private()
function pgactive.pgactive_get_connection_replication_sets(text)
function pgactive.pgactive_get_global_locks_info()
function pgactive.pgactive_get_local_nodeid()
function pgactive.pgactive_get_local_node_name()
function pgactive.pgactive_get_node_identifier()
function pgactive._pgactive_get_node_info_private(text,text)
function pgactive.pgactive_get_stats()
function pgactive.pgactive_get_table_replication_sets(regclass)
function pgactive.pgactive_get_workers_info()
function pgactive.pgactive_handle_rejoin()
function pgactive.pgactive_internal_create_truncate_trigger(regclass)
function pgactive.pgactive_is_active_in_db()
function pgactive.pgactive_is_apply_paused()
function pgactive.pgactive_join_group(text,text,text,integer,text[],boolean,boolean,boolean)
function pgactive._pgactive_join_node_private(text,oid,oid,text,integer,text[])
function pgactive.pgactive_min_remote_version_num()
function pgactive._pgactive_nid_shmem_reset_all_private()
function pgactive.pgactive_node_status_from_char("char")
function pgactive.pgactive_node_status_to_char(text)
function pgactive.pgactive_parse_replident_name(text)
function pgactive.pgactive_parse_slot_name(name)
function pgactive._pgactive_pause_worker_management_private(boolean)
function pgactive.pgactive_queue_truncate()
function pgactive.pgactive_remove(boolean)
function pgactive.pgactive_replicate_ddl_command(text)
function pgactive.pgactive_set_connection_replication_sets(text[],text)
function pgactive.pgactive_set_node_read_only(text,boolean)
function pgactive.pgactive_set_table_replication_sets(regclass,text[])
function pgactive.pgactive_skip_changes(text,oid,oid,pg_lsn)
function pgactive._pgactive_snowflake_id_nextval_private(regclass,bigint)
function pgactive.pgactive_snowflake_id_nextval(regclass)
function pgactive.pgactive_terminate_workers(text,oid,oid,text)
function pgactive.pgactive_truncate_trigger_add()
function pgactive.pgactive_update_node_conninfo(text,text)
function pgactive._pgactive_update_seclabel_private()
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

my $result210_17_expected = 'event trigger pgactive_truncate_trigger_add
foreign-data wrapper pgactive_fdw
function pgactive.check_file_system_mount_points(text,text)
function pgactive.get_free_disk_space(text)
function pgactive.get_last_applied_xact_info(text,oid,oid)
function pgactive.get_replication_lag_info()
function pgactive.has_required_privs()
function pgactive.pgactive_acquire_global_lock(text)
function pgactive.pgactive_apply_pause()
function pgactive.pgactive_apply_resume()
function pgactive.pgactive_assign_seq_ids_post_upgrade()
function pgactive._pgactive_begin_join_private(text,text,text,text,boolean,boolean,boolean)
function pgactive.pgactive_connections_changed()
function pgactive.pgactive_conninfo_cmp(text,text)
function pgactive.pgactive_create_conflict_handler(regclass,name,regprocedure,pgactive.pgactive_conflict_type,interval)
function pgactive.pgactive_create_group(text,text,integer,text[])
function pgactive._pgactive_destroy_temporary_dump_directories_private()
function pgactive.pgactive_detach_nodes(text[])
function pgactive.pgactive_drop_conflict_handler(regclass,name)
function pgactive.pgactive_fdw_validator(text[],oid)
function pgactive.pgactive_format_replident_name(text,oid,oid,oid,name)
function pgactive.pgactive_format_slot_name(text,oid,oid,oid,name)
function pgactive._pgactive_generate_node_identifier_private()
function pgactive.pgactive_get_connection_replication_sets(text)
function pgactive.pgactive_get_global_locks_info()
function pgactive.pgactive_get_local_nodeid()
function pgactive.pgactive_get_local_node_name()
function pgactive.pgactive_get_node_identifier()
function pgactive._pgactive_get_node_info_private(text,text)
function pgactive.pgactive_get_stats()
function pgactive.pgactive_get_table_replication_sets(regclass)
function pgactive.pgactive_get_workers_info()
function pgactive.pgactive_handle_rejoin()
function pgactive.pgactive_internal_create_truncate_trigger(regclass)
function pgactive.pgactive_is_active_in_db()
function pgactive.pgactive_is_apply_paused()
function pgactive.pgactive_join_group(text,text,text,integer,text[],boolean,boolean,boolean)
function pgactive._pgactive_join_node_private(text,oid,oid,text,integer,text[])
function pgactive.pgactive_min_remote_version_num()
function pgactive._pgactive_nid_shmem_reset_all_private()
function pgactive.pgactive_node_status_from_char("char")
function pgactive.pgactive_node_status_to_char(text)
function pgactive.pgactive_parse_replident_name(text)
function pgactive.pgactive_parse_slot_name(name)
function pgactive._pgactive_pause_worker_management_private(boolean)
function pgactive.pgactive_queue_truncate()
function pgactive.pgactive_remove(boolean)
function pgactive.pgactive_replicate_ddl_command(text)
function pgactive.pgactive_set_connection_replication_sets(text[],text)
function pgactive.pgactive_set_node_read_only(text,boolean)
function pgactive.pgactive_set_table_replication_sets(regclass,text[])
function pgactive.pgactive_skip_changes(text,oid,oid,pg_lsn)
function pgactive._pgactive_snowflake_id_nextval_private(regclass,bigint)
function pgactive.pgactive_snowflake_id_nextval(regclass)
function pgactive.pgactive_terminate_workers(text,oid,oid,text)
function pgactive.pgactive_truncate_trigger_add()
function pgactive.pgactive_update_node_conninfo(text,text)
function pgactive._pgactive_update_seclabel_private()
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
type pgactive.dropped_object[]
type pgactive.pgactive_conflict_handler_action
type pgactive.pgactive_conflict_handler_action[]
type pgactive.pgactive_conflict_handlers
type pgactive.pgactive_conflict_handlers[]
type pgactive.pgactive_conflict_history
type pgactive.pgactive_conflict_history[]
type pgactive.pgactive_conflict_resolution
type pgactive.pgactive_conflict_resolution[]
type pgactive.pgactive_conflict_type
type pgactive.pgactive_conflict_type[]
type pgactive.pgactive_connections
type pgactive.pgactive_connections[]
type pgactive.pgactive_global_locks
type pgactive.pgactive_global_locks[]
type pgactive.pgactive_global_locks_info
type pgactive.pgactive_global_locks_info[]
type pgactive.pgactive_nodes
type pgactive.pgactive_nodes[]
type pgactive.pgactive_node_slots
type pgactive.pgactive_node_slots[]
type pgactive.pgactive_queued_commands
type pgactive.pgactive_queued_commands[]
type pgactive.pgactive_queued_drops
type pgactive.pgactive_queued_drops[]
type pgactive.pgactive_replication_set_config
type pgactive.pgactive_replication_set_config[]
type pgactive.pgactive_stats
type pgactive.pgactive_stats[]
type pgactive.pgactive_sync_type
type pgactive.pgactive_sync_type[]
view pgactive.pgactive_global_locks_info
view pgactive.pgactive_node_slots
view pgactive.pgactive_stats';

# List what version 2.1.0 contains.
#my $result210 = $node_a->safe_psql($pgactive_test_dbname, q[\dx+ pgactive]);

#if ($major_version <= 16)
#{
#  is($result210, $result210_expected,
#     'extension version 2.1.0 contains expected objects');
#}
#else
#{
#  is($result210, $result210_17_expected,
#     'extension version 2.1.0 contains expected objects on PG >=17');
#}

$node_a->safe_psql($pgactive_test_dbname, q{SET pgactive.skip_ddl_replication = true;});

# Move to new version 2.1.1.
$node_a->safe_psql($pgactive_test_dbname, q{ALTER EXTENSION pgactive UPDATE TO '2.1.1';});

my $result211_expected = 'event trigger pgactive_truncate_trigger_add
foreign-data wrapper pgactive_fdw
function pgactive.has_required_privs()
function pgactive.pgactive_acquire_global_lock(text)
function pgactive.pgactive_apply_pause()
function pgactive.pgactive_apply_resume()
function pgactive.pgactive_assign_seq_ids_post_upgrade()
function pgactive._pgactive_begin_join_private(text,text,text,text,boolean,boolean,boolean)
function pgactive._pgactive_check_file_system_mount_points(text,text)
function pgactive.pgactive_connections_changed()
function pgactive.pgactive_conninfo_cmp(text,text)
function pgactive.pgactive_create_conflict_handler(regclass,name,regprocedure,pgactive.pgactive_conflict_type,interval)
function pgactive.pgactive_create_group(text,text,integer,text[])
function pgactive._pgactive_destroy_temporary_dump_directories_private()
function pgactive.pgactive_detach_nodes(text[])
function pgactive.pgactive_drop_conflict_handler(regclass,name)
function pgactive.pgactive_fdw_validator(text[],oid)
function pgactive.pgactive_format_replident_name(text,oid,oid,oid,name)
function pgactive.pgactive_format_slot_name(text,oid,oid,oid,name)
function pgactive._pgactive_generate_node_identifier_private()
function pgactive.pgactive_get_connection_replication_sets(text)
function pgactive._pgactive_get_free_disk_space(text)
function pgactive.pgactive_get_global_locks_info()
function pgactive.pgactive_get_last_applied_xact_info(text,oid,oid)
function pgactive.pgactive_get_local_nodeid()
function pgactive.pgactive_get_local_node_name()
function pgactive.pgactive_get_node_identifier()
function pgactive._pgactive_get_node_info_private(text,text)
function pgactive.pgactive_get_replication_lag_info()
function pgactive.pgactive_get_stats()
function pgactive.pgactive_get_table_replication_sets(regclass)
function pgactive.pgactive_get_workers_info()
function pgactive.pgactive_handle_rejoin()
function pgactive._pgactive_has_required_privs()
function pgactive.pgactive_internal_create_truncate_trigger(regclass)
function pgactive.pgactive_is_active_in_db()
function pgactive.pgactive_is_apply_paused()
function pgactive.pgactive_join_group(text,text,text,integer,text[],boolean,boolean,boolean)
function pgactive._pgactive_join_node_private(text,oid,oid,text,integer,text[])
function pgactive.pgactive_min_remote_version_num()
function pgactive._pgactive_nid_shmem_reset_all_private()
function pgactive.pgactive_node_status_from_char("char")
function pgactive.pgactive_node_status_to_char(text)
function pgactive.pgactive_parse_replident_name(text)
function pgactive.pgactive_parse_slot_name(name)
function pgactive._pgactive_pause_worker_management_private(boolean)
function pgactive.pgactive_queue_truncate()
function pgactive.pgactive_remove(boolean)
function pgactive.pgactive_replicate_ddl_command(text)
function pgactive.pgactive_set_connection_replication_sets(text[],text)
function pgactive.pgactive_set_node_read_only(text,boolean)
function pgactive.pgactive_set_table_replication_sets(regclass,text[])
function pgactive.pgactive_skip_changes(text,oid,oid,pg_lsn)
function pgactive._pgactive_snowflake_id_nextval_private(regclass,bigint)
function pgactive.pgactive_snowflake_id_nextval(regclass)
function pgactive.pgactive_terminate_workers(text,oid,oid,text)
function pgactive.pgactive_truncate_trigger_add()
function pgactive.pgactive_update_node_conninfo(text,text)
function pgactive._pgactive_update_seclabel_private()
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

my $result211_17_expected = 'event trigger pgactive_truncate_trigger_add
foreign-data wrapper pgactive_fdw
function pgactive.has_required_privs()
function pgactive.pgactive_acquire_global_lock(text)
function pgactive.pgactive_apply_pause()
function pgactive.pgactive_apply_resume()
function pgactive.pgactive_assign_seq_ids_post_upgrade()
function pgactive._pgactive_begin_join_private(text,text,text,text,boolean,boolean,boolean)
function pgactive._pgactive_check_file_system_mount_points(text,text)
function pgactive.pgactive_connections_changed()
function pgactive.pgactive_conninfo_cmp(text,text)
function pgactive.pgactive_create_conflict_handler(regclass,name,regprocedure,pgactive.pgactive_conflict_type,interval)
function pgactive.pgactive_create_group(text,text,integer,text[])
function pgactive._pgactive_destroy_temporary_dump_directories_private()
function pgactive.pgactive_detach_nodes(text[])
function pgactive.pgactive_drop_conflict_handler(regclass,name)
function pgactive.pgactive_fdw_validator(text[],oid)
function pgactive.pgactive_format_replident_name(text,oid,oid,oid,name)
function pgactive.pgactive_format_slot_name(text,oid,oid,oid,name)
function pgactive._pgactive_generate_node_identifier_private()
function pgactive.pgactive_get_connection_replication_sets(text)
function pgactive._pgactive_get_free_disk_space(text)
function pgactive.pgactive_get_global_locks_info()
function pgactive.pgactive_get_last_applied_xact_info(text,oid,oid)
function pgactive.pgactive_get_local_nodeid()
function pgactive.pgactive_get_local_node_name()
function pgactive.pgactive_get_node_identifier()
function pgactive._pgactive_get_node_info_private(text,text)
function pgactive.pgactive_get_replication_lag_info()
function pgactive.pgactive_get_stats()
function pgactive.pgactive_get_table_replication_sets(regclass)
function pgactive.pgactive_get_workers_info()
function pgactive.pgactive_handle_rejoin()
function pgactive._pgactive_has_required_privs()
function pgactive.pgactive_internal_create_truncate_trigger(regclass)
function pgactive.pgactive_is_active_in_db()
function pgactive.pgactive_is_apply_paused()
function pgactive.pgactive_join_group(text,text,text,integer,text[],boolean,boolean,boolean)
function pgactive._pgactive_join_node_private(text,oid,oid,text,integer,text[])
function pgactive.pgactive_min_remote_version_num()
function pgactive._pgactive_nid_shmem_reset_all_private()
function pgactive.pgactive_node_status_from_char("char")
function pgactive.pgactive_node_status_to_char(text)
function pgactive.pgactive_parse_replident_name(text)
function pgactive.pgactive_parse_slot_name(name)
function pgactive._pgactive_pause_worker_management_private(boolean)
function pgactive.pgactive_queue_truncate()
function pgactive.pgactive_remove(boolean)
function pgactive.pgactive_replicate_ddl_command(text)
function pgactive.pgactive_set_connection_replication_sets(text[],text)
function pgactive.pgactive_set_node_read_only(text,boolean)
function pgactive.pgactive_set_table_replication_sets(regclass,text[])
function pgactive.pgactive_skip_changes(text,oid,oid,pg_lsn)
function pgactive._pgactive_snowflake_id_nextval_private(regclass,bigint)
function pgactive.pgactive_snowflake_id_nextval(regclass)
function pgactive.pgactive_terminate_workers(text,oid,oid,text)
function pgactive.pgactive_truncate_trigger_add()
function pgactive.pgactive_update_node_conninfo(text,text)
function pgactive._pgactive_update_seclabel_private()
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
type pgactive.dropped_object[]
type pgactive.pgactive_conflict_handler_action
type pgactive.pgactive_conflict_handler_action[]
type pgactive.pgactive_conflict_handlers
type pgactive.pgactive_conflict_handlers[]
type pgactive.pgactive_conflict_history
type pgactive.pgactive_conflict_history[]
type pgactive.pgactive_conflict_resolution
type pgactive.pgactive_conflict_resolution[]
type pgactive.pgactive_conflict_type
type pgactive.pgactive_conflict_type[]
type pgactive.pgactive_connections
type pgactive.pgactive_connections[]
type pgactive.pgactive_global_locks
type pgactive.pgactive_global_locks[]
type pgactive.pgactive_global_locks_info
type pgactive.pgactive_global_locks_info[]
type pgactive.pgactive_nodes
type pgactive.pgactive_nodes[]
type pgactive.pgactive_node_slots
type pgactive.pgactive_node_slots[]
type pgactive.pgactive_queued_commands
type pgactive.pgactive_queued_commands[]
type pgactive.pgactive_queued_drops
type pgactive.pgactive_queued_drops[]
type pgactive.pgactive_replication_set_config
type pgactive.pgactive_replication_set_config[]
type pgactive.pgactive_stats
type pgactive.pgactive_stats[]
type pgactive.pgactive_sync_type
type pgactive.pgactive_sync_type[]
view pgactive.pgactive_global_locks_info
view pgactive.pgactive_node_slots
view pgactive.pgactive_stats';

# List what version 2.1.1 contains.
#my $result211 = $node_a->safe_psql($pgactive_test_dbname, q[\dx+ pgactive]);

#if ($major_version <= 16)
#{
#  is($result211, $result211_expected,
#     'extension version 2.1.1 contains expected objects');
#}
#else
#{
#  is($result211, $result211_17_expected,
#     'extension version 2.1.1 contains expected objects on PG >=17');
#}
# Move to new version 2.1.2.
$node_a->safe_psql($pgactive_test_dbname, q{ALTER EXTENSION pgactive UPDATE TO '2.1.2';});

my $result212_expected = 'event trigger pgactive_truncate_trigger_add
foreign-data wrapper pgactive_fdw
function pgactive.has_required_privs()
function pgactive.pgactive_acquire_global_lock(text)
function pgactive.pgactive_apply_pause()
function pgactive.pgactive_apply_resume()
function pgactive.pgactive_assign_seq_ids_post_upgrade()
function pgactive._pgactive_begin_join_private(text,text,text,text,boolean,boolean,boolean)
function pgactive._pgactive_check_file_system_mount_points(text,text)
function pgactive.pgactive_connections_changed()
function pgactive.pgactive_conninfo_cmp(text,text)
function pgactive.pgactive_create_conflict_handler(regclass,name,regprocedure,pgactive.pgactive_conflict_type,interval)
function pgactive.pgactive_create_group(text,text,integer,text[])
function pgactive._pgactive_destroy_temporary_dump_directories_private()
function pgactive.pgactive_detach_nodes(text[])
function pgactive.pgactive_drop_conflict_handler(regclass,name)
function pgactive.pgactive_fdw_validator(text[],oid)
function pgactive.pgactive_format_replident_name(text,oid,oid,oid,name)
function pgactive.pgactive_format_slot_name(text,oid,oid,oid,name)
function pgactive._pgactive_generate_node_identifier_private()
function pgactive.pgactive_get_connection_replication_sets(text)
function pgactive._pgactive_get_free_disk_space(text)
function pgactive.pgactive_get_global_locks_info()
function pgactive.pgactive_get_last_applied_xact_info(text,oid,oid)
function pgactive.pgactive_get_local_nodeid()
function pgactive.pgactive_get_local_node_name()
function pgactive.pgactive_get_node_identifier()
function pgactive._pgactive_get_node_info_private(text,text)
function pgactive.pgactive_get_replication_lag_info()
function pgactive.pgactive_get_stats()
function pgactive.pgactive_get_table_replication_sets(regclass)
function pgactive.pgactive_get_workers_info()
function pgactive.pgactive_handle_rejoin()
function pgactive._pgactive_has_required_privs()
function pgactive.pgactive_internal_create_truncate_trigger(regclass)
function pgactive.pgactive_is_active_in_db()
function pgactive.pgactive_is_apply_paused()
function pgactive.pgactive_join_group(text,text,text,integer,text[],boolean,boolean,boolean)
function pgactive._pgactive_join_node_private(text,oid,oid,text,integer,text[])
function pgactive.pgactive_min_remote_version_num()
function pgactive._pgactive_nid_shmem_reset_all_private()
function pgactive.pgactive_node_status_from_char("char")
function pgactive.pgactive_node_status_to_char(text)
function pgactive.pgactive_parse_replident_name(text)
function pgactive.pgactive_parse_slot_name(name)
function pgactive._pgactive_pause_worker_management_private(boolean)
function pgactive.pgactive_queue_truncate()
function pgactive.pgactive_remove(boolean)
function pgactive.pgactive_replicate_ddl_command(text)
function pgactive.pgactive_set_connection_replication_sets(text[],text)
function pgactive.pgactive_set_node_read_only(text,boolean)
function pgactive.pgactive_set_table_replication_sets(regclass,text[])
function pgactive.pgactive_skip_changes(text,oid,oid,pg_lsn)
function pgactive._pgactive_snowflake_id_nextval_private(regclass,bigint)
function pgactive.pgactive_snowflake_id_nextval(regclass)
function pgactive.pgactive_terminate_workers(text,oid,oid,text)
function pgactive.pgactive_truncate_trigger_add()
function pgactive.pgactive_update_node_conninfo(text,text)
function pgactive._pgactive_update_seclabel_private()
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

my $result212_17_expected = 'event trigger pgactive_truncate_trigger_add
foreign-data wrapper pgactive_fdw
function pgactive.has_required_privs()
function pgactive.pgactive_acquire_global_lock(text)
function pgactive.pgactive_apply_pause()
function pgactive.pgactive_apply_resume()
function pgactive.pgactive_assign_seq_ids_post_upgrade()
function pgactive._pgactive_begin_join_private(text,text,text,text,boolean,boolean,boolean)
function pgactive._pgactive_check_file_system_mount_points(text,text)
function pgactive.pgactive_connections_changed()
function pgactive.pgactive_conninfo_cmp(text,text)
function pgactive.pgactive_create_conflict_handler(regclass,name,regprocedure,pgactive.pgactive_conflict_type,interval)
function pgactive.pgactive_create_group(text,text,integer,text[])
function pgactive._pgactive_destroy_temporary_dump_directories_private()
function pgactive.pgactive_detach_nodes(text[])
function pgactive.pgactive_drop_conflict_handler(regclass,name)
function pgactive.pgactive_fdw_validator(text[],oid)
function pgactive.pgactive_format_replident_name(text,oid,oid,oid,name)
function pgactive.pgactive_format_slot_name(text,oid,oid,oid,name)
function pgactive._pgactive_generate_node_identifier_private()
function pgactive.pgactive_get_connection_replication_sets(text)
function pgactive._pgactive_get_free_disk_space(text)
function pgactive.pgactive_get_global_locks_info()
function pgactive.pgactive_get_last_applied_xact_info(text,oid,oid)
function pgactive.pgactive_get_local_nodeid()
function pgactive.pgactive_get_local_node_name()
function pgactive.pgactive_get_node_identifier()
function pgactive._pgactive_get_node_info_private(text,text)
function pgactive.pgactive_get_replication_lag_info()
function pgactive.pgactive_get_stats()
function pgactive.pgactive_get_table_replication_sets(regclass)
function pgactive.pgactive_get_workers_info()
function pgactive.pgactive_handle_rejoin()
function pgactive._pgactive_has_required_privs()
function pgactive.pgactive_internal_create_truncate_trigger(regclass)
function pgactive.pgactive_is_active_in_db()
function pgactive.pgactive_is_apply_paused()
function pgactive.pgactive_join_group(text,text,text,integer,text[],boolean,boolean,boolean)
function pgactive._pgactive_join_node_private(text,oid,oid,text,integer,text[])
function pgactive.pgactive_min_remote_version_num()
function pgactive._pgactive_nid_shmem_reset_all_private()
function pgactive.pgactive_node_status_from_char("char")
function pgactive.pgactive_node_status_to_char(text)
function pgactive.pgactive_parse_replident_name(text)
function pgactive.pgactive_parse_slot_name(name)
function pgactive._pgactive_pause_worker_management_private(boolean)
function pgactive.pgactive_queue_truncate()
function pgactive.pgactive_remove(boolean)
function pgactive.pgactive_replicate_ddl_command(text)
function pgactive.pgactive_set_connection_replication_sets(text[],text)
function pgactive.pgactive_set_node_read_only(text,boolean)
function pgactive.pgactive_set_table_replication_sets(regclass,text[])
function pgactive.pgactive_skip_changes(text,oid,oid,pg_lsn)
function pgactive._pgactive_snowflake_id_nextval_private(regclass,bigint)
function pgactive.pgactive_snowflake_id_nextval(regclass)
function pgactive.pgactive_terminate_workers(text,oid,oid,text)
function pgactive.pgactive_truncate_trigger_add()
function pgactive.pgactive_update_node_conninfo(text,text)
function pgactive._pgactive_update_seclabel_private()
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
type pgactive.dropped_object[]
type pgactive.pgactive_conflict_handler_action
type pgactive.pgactive_conflict_handler_action[]
type pgactive.pgactive_conflict_handlers
type pgactive.pgactive_conflict_handlers[]
type pgactive.pgactive_conflict_history
type pgactive.pgactive_conflict_history[]
type pgactive.pgactive_conflict_resolution
type pgactive.pgactive_conflict_resolution[]
type pgactive.pgactive_conflict_type
type pgactive.pgactive_conflict_type[]
type pgactive.pgactive_connections
type pgactive.pgactive_connections[]
type pgactive.pgactive_global_locks
type pgactive.pgactive_global_locks[]
type pgactive.pgactive_global_locks_info
type pgactive.pgactive_global_locks_info[]
type pgactive.pgactive_nodes
type pgactive.pgactive_nodes[]
type pgactive.pgactive_node_slots
type pgactive.pgactive_node_slots[]
type pgactive.pgactive_queued_commands
type pgactive.pgactive_queued_commands[]
type pgactive.pgactive_queued_drops
type pgactive.pgactive_queued_drops[]
type pgactive.pgactive_replication_set_config
type pgactive.pgactive_replication_set_config[]
type pgactive.pgactive_stats
type pgactive.pgactive_stats[]
type pgactive.pgactive_sync_type
type pgactive.pgactive_sync_type[]
view pgactive.pgactive_global_locks_info
view pgactive.pgactive_node_slots
view pgactive.pgactive_stats';

# List what version 2.1.2 contains.
#my $result212 = $node_a->safe_psql($pgactive_test_dbname, q[\dx+ pgactive]);

#if ($major_version <= 16)
#{
#  is($result212, $result212_expected,
#     'extension version 2.1.2 contains expected objects');
#}
#else
#{
#  is($result212, $result212_17_expected,
#     'extension version 2.1.2 contains expected objects on PG >=17');
#}

# Move to new version 2.1.3.
$node_a->safe_psql($pgactive_test_dbname, q{ALTER EXTENSION pgactive UPDATE TO '2.1.3';});

my $result213_expected = 'event trigger pgactive_truncate_trigger_add
foreign-data wrapper pgactive_fdw
function pgactive.has_required_privs()
function pgactive.pgactive_acquire_global_lock(text)
function pgactive.pgactive_apply_pause()
function pgactive.pgactive_apply_resume()
function pgactive.pgactive_assign_seq_ids_post_upgrade()
function pgactive._pgactive_begin_join_private(text,text,text,text,boolean,boolean,boolean)
function pgactive._pgactive_check_file_system_mount_points(text,text)
function pgactive.pgactive_connections_changed()
function pgactive.pgactive_conninfo_cmp(text,text)
function pgactive.pgactive_create_conflict_handler(regclass,name,regprocedure,pgactive.pgactive_conflict_type,interval)
function pgactive.pgactive_create_group(text,text,integer,text[])
function pgactive._pgactive_destroy_temporary_dump_directories_private()
function pgactive.pgactive_detach_nodes(text[])
function pgactive.pgactive_drop_conflict_handler(regclass,name)
function pgactive.pgactive_fdw_validator(text[],oid)
function pgactive.pgactive_format_replident_name(text,oid,oid,oid,name)
function pgactive.pgactive_format_slot_name(text,oid,oid,oid,name)
function pgactive._pgactive_generate_node_identifier_private()
function pgactive.pgactive_get_connection_replication_sets(text)
function pgactive.pgactive_get_connection_replication_sets(text,oid,oid,text,oid,oid)
function pgactive.pgactive_get_connection_replication_sets(text[],text,oid,oid,text,oid,oid)
function pgactive._pgactive_get_free_disk_space(text)
function pgactive.pgactive_get_global_locks_info()
function pgactive.pgactive_get_last_applied_xact_info(text,oid,oid)
function pgactive.pgactive_get_local_nodeid()
function pgactive.pgactive_get_local_node_name()
function pgactive.pgactive_get_node_identifier()
function pgactive._pgactive_get_node_info_private(text,text)
function pgactive.pgactive_get_replication_lag_info()
function pgactive.pgactive_get_stats()
function pgactive.pgactive_get_table_replication_sets(regclass)
function pgactive.pgactive_get_workers_info()
function pgactive.pgactive_handle_rejoin()
function pgactive._pgactive_has_required_privs()
function pgactive.pgactive_internal_create_truncate_trigger(regclass)
function pgactive.pgactive_is_active_in_db()
function pgactive.pgactive_is_apply_paused()
function pgactive.pgactive_join_group(text,text,text,integer,text[],boolean,boolean,boolean)
function pgactive._pgactive_join_node_private(text,oid,oid,text,integer,text[])
function pgactive.pgactive_min_remote_version_num()
function pgactive._pgactive_nid_shmem_reset_all_private()
function pgactive.pgactive_node_status_from_char("char")
function pgactive.pgactive_node_status_to_char(text)
function pgactive.pgactive_parse_replident_name(text)
function pgactive.pgactive_parse_slot_name(name)
function pgactive._pgactive_pause_worker_management_private(boolean)
function pgactive.pgactive_queue_truncate()
function pgactive.pgactive_remove(boolean)
function pgactive.pgactive_replicate_ddl_command(text)
function pgactive.pgactive_set_connection_replication_sets(text[],text)
function pgactive.pgactive_set_node_read_only(text,boolean)
function pgactive.pgactive_set_table_replication_sets(regclass,text[])
function pgactive.pgactive_skip_changes(text,oid,oid,pg_lsn)
function pgactive._pgactive_snowflake_id_nextval_private(regclass,bigint)
function pgactive.pgactive_snowflake_id_nextval(regclass)
function pgactive.pgactive_terminate_workers(text,oid,oid,text)
function pgactive.pgactive_truncate_trigger_add()
function pgactive.pgactive_update_node_conninfo(text,text)
function pgactive._pgactive_update_seclabel_private()
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

my $result213_17_expected = 'event trigger pgactive_truncate_trigger_add
foreign-data wrapper pgactive_fdw
function pgactive.has_required_privs()
function pgactive.pgactive_acquire_global_lock(text)
function pgactive.pgactive_apply_pause()
function pgactive.pgactive_apply_resume()
function pgactive.pgactive_assign_seq_ids_post_upgrade()
function pgactive._pgactive_begin_join_private(text,text,text,text,boolean,boolean,boolean)
function pgactive._pgactive_check_file_system_mount_points(text,text)
function pgactive.pgactive_connections_changed()
function pgactive.pgactive_conninfo_cmp(text,text)
function pgactive.pgactive_create_conflict_handler(regclass,name,regprocedure,pgactive.pgactive_conflict_type,interval)
function pgactive.pgactive_create_group(text,text,integer,text[])
function pgactive._pgactive_destroy_temporary_dump_directories_private()
function pgactive.pgactive_detach_nodes(text[])
function pgactive.pgactive_drop_conflict_handler(regclass,name)
function pgactive.pgactive_fdw_validator(text[],oid)
function pgactive.pgactive_format_replident_name(text,oid,oid,oid,name)
function pgactive.pgactive_format_slot_name(text,oid,oid,oid,name)
function pgactive._pgactive_generate_node_identifier_private()
function pgactive.pgactive_get_connection_replication_sets(text)
function pgactive.pgactive_get_connection_replication_sets(text,oid,oid,text,oid,oid)
function pgactive.pgactive_get_connection_replication_sets(text[],text,oid,oid,text,oid,oid)
function pgactive._pgactive_get_free_disk_space(text)
function pgactive.pgactive_get_global_locks_info()
function pgactive.pgactive_get_last_applied_xact_info(text,oid,oid)
function pgactive.pgactive_get_local_nodeid()
function pgactive.pgactive_get_local_node_name()
function pgactive.pgactive_get_node_identifier()
function pgactive._pgactive_get_node_info_private(text,text)
function pgactive.pgactive_get_replication_lag_info()
function pgactive.pgactive_get_stats()
function pgactive.pgactive_get_table_replication_sets(regclass)
function pgactive.pgactive_get_workers_info()
function pgactive.pgactive_handle_rejoin()
function pgactive._pgactive_has_required_privs()
function pgactive.pgactive_internal_create_truncate_trigger(regclass)
function pgactive.pgactive_is_active_in_db()
function pgactive.pgactive_is_apply_paused()
function pgactive.pgactive_join_group(text,text,text,integer,text[],boolean,boolean,boolean)
function pgactive._pgactive_join_node_private(text,oid,oid,text,integer,text[])
function pgactive.pgactive_min_remote_version_num()
function pgactive._pgactive_nid_shmem_reset_all_private()
function pgactive.pgactive_node_status_from_char("char")
function pgactive.pgactive_node_status_to_char(text)
function pgactive.pgactive_parse_replident_name(text)
function pgactive.pgactive_parse_slot_name(name)
function pgactive._pgactive_pause_worker_management_private(boolean)
function pgactive.pgactive_queue_truncate()
function pgactive.pgactive_remove(boolean)
function pgactive.pgactive_replicate_ddl_command(text)
function pgactive.pgactive_set_connection_replication_sets(text[],text)
function pgactive.pgactive_set_node_read_only(text,boolean)
function pgactive.pgactive_set_table_replication_sets(regclass,text[])
function pgactive.pgactive_skip_changes(text,oid,oid,pg_lsn)
function pgactive._pgactive_snowflake_id_nextval_private(regclass,bigint)
function pgactive.pgactive_snowflake_id_nextval(regclass)
function pgactive.pgactive_terminate_workers(text,oid,oid,text)
function pgactive.pgactive_truncate_trigger_add()
function pgactive.pgactive_update_node_conninfo(text,text)
function pgactive._pgactive_update_seclabel_private()
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
type pgactive.dropped_object[]
type pgactive.pgactive_conflict_handler_action
type pgactive.pgactive_conflict_handler_action[]
type pgactive.pgactive_conflict_handlers
type pgactive.pgactive_conflict_handlers[]
type pgactive.pgactive_conflict_history
type pgactive.pgactive_conflict_history[]
type pgactive.pgactive_conflict_resolution
type pgactive.pgactive_conflict_resolution[]
type pgactive.pgactive_conflict_type
type pgactive.pgactive_conflict_type[]
type pgactive.pgactive_connections
type pgactive.pgactive_connections[]
type pgactive.pgactive_global_locks
type pgactive.pgactive_global_locks[]
type pgactive.pgactive_global_locks_info
type pgactive.pgactive_global_locks_info[]
type pgactive.pgactive_nodes
type pgactive.pgactive_nodes[]
type pgactive.pgactive_node_slots
type pgactive.pgactive_node_slots[]
type pgactive.pgactive_queued_commands
type pgactive.pgactive_queued_commands[]
type pgactive.pgactive_queued_drops
type pgactive.pgactive_queued_drops[]
type pgactive.pgactive_replication_set_config
type pgactive.pgactive_replication_set_config[]
type pgactive.pgactive_stats
type pgactive.pgactive_stats[]
type pgactive.pgactive_sync_type
type pgactive.pgactive_sync_type[]
view pgactive.pgactive_global_locks_info
view pgactive.pgactive_node_slots
view pgactive.pgactive_stats';

# List what version 2.1.3 contains.
#my $result213 = $node_a->safe_psql($pgactive_test_dbname, q[\dx+ pgactive]);

#if ($major_version <= 16)
#{
#  is($result213, $result213_expected,
#    'extension version 2.1.3 contains expected objects');
#}
#else
#{
#  is($result213, $result213_17_expected,
#     'extension version 2.1.3 contains expected objects on PG >=17');
#}

done_testing();
