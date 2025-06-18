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

$node_a->safe_psql($pgactive_test_dbname, q{SET pgactive.skip_ddl_replication = true;});

# Move to new version 2.1.1.
$node_a->safe_psql($pgactive_test_dbname, q{ALTER EXTENSION pgactive UPDATE TO '2.1.1';});

# Move to new version 2.1.2.
$node_a->safe_psql($pgactive_test_dbname, q{ALTER EXTENSION pgactive UPDATE TO '2.1.2';});

# Move to new version 2.1.3.
$node_a->safe_psql($pgactive_test_dbname, q{ALTER EXTENSION pgactive UPDATE TO '2.1.3';});

# Move to new version 2.1.4.
$node_a->safe_psql($pgactive_test_dbname, q{ALTER EXTENSION pgactive UPDATE TO '2.1.4';});

# Move to new version 2.1.5.
$node_a->safe_psql($pgactive_test_dbname, q{ALTER EXTENSION pgactive UPDATE TO '2.1.5';});

done_testing();
