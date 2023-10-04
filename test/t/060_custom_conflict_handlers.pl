#!/usr/bin/env perl
#
# This test demonstrates how to add custom conflict handlers in pgactive.
#
use strict;
use warnings;
use lib 'test/t/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use IPC::Run;
use Test::More;
use utils::nodemanagement;

# Create an upstream node and bring up pgactive
my $nodes = make_pgactive_group(2,'node_');
my ($node_0,$node_1) = @$nodes;

exec_ddl($node_0, q[CREATE TABLE public.city(city_sid INT PRIMARY KEY, name VARCHAR, UNIQUE(name));]);
wait_for_apply($node_0, $node_1);

# Create a custom conflict handler function for insert_insert conflict on one
# of the node, the other node gets this function via replication.
$node_0->safe_psql($pgactive_test_dbname,
    q{CREATE FUNCTION city_custom_ch_func(
        local_row city,
        remote_row city,
        command_tag text,
        table_name regclass,
        conflict_type pgactive.pgactive_conflict_type,
        result_row OUT city,
        handler_action OUT pgactive.pgactive_conflict_handler_action)
        RETURNS record
        LANGUAGE 'plpgsql'
        AS $$
        BEGIN
        IF conflict_type = 'insert_insert'::pgactive.pgactive_conflict_type THEN
            RAISE WARNING 'conflict % detected for table:%, command_tag:%, local_row:%, remote_row:%',
                conflict_type, table_name, command_tag, local_row, remote_row;
            result_row.city_sid := local_row.city_sid + remote_row.city_sid + 100;
            result_row.name := 'Pune';
        ELSE
            RAISE 'unexpected conflict % detected for table:%',
                conflict_type, table_name;
        END IF;
        handler_action := 'ROW';
        END;
        $$;
     ;});

$node_0->safe_psql($pgactive_test_dbname,
    q{SELECT * FROM pgactive.pgactive_create_conflict_handler(
        ch_rel := 'city',
        ch_name := 'city_insert_insert_ch',
        ch_proc := 'city_custom_ch_func(city, city, text, regclass, pgactive.pgactive_conflict_type)',
        ch_type := 'insert_insert');
    });

# Similarly, one can pgactive_create_conflict_handler hanlders for other types
# of conflicts such as insert_update, update_update and so on, and branch
# out for different treatment in city_custom_ch_func.

wait_for_apply($node_0, $node_1);

my $result = $node_0->safe_psql($pgactive_test_dbname,
  q[SELECT count(*) FROM pgactive.pgactive_conflict_handlers
    WHERE ch_name = 'city_insert_insert_ch';]);
is($result, qq(1), 'custom conflict handler exists on node0');

$result = $node_1->safe_psql($pgactive_test_dbname,
  q[SELECT count(*) FROM pgactive.pgactive_conflict_handlers
    WHERE ch_name = 'city_insert_insert_ch';]);
is($result, qq(1), 'custom conflict handler exists on node1');

foreach my $node ($node_0, $node_1)
{
    $node->safe_psql($pgactive_test_dbname,
        q[ALTER SYSTEM SET pgactive.debug_apply_delay = '5s';]);
    $node->safe_psql($pgactive_test_dbname,
        q[ALTER SYSTEM SET pgactive.log_conflicts_to_table = on;]);
    $node->safe_psql($pgactive_test_dbname,
        q[ALTER SYSTEM SET pgactive.log_conflicts_to_logfile = on;]);
    $node->safe_psql($pgactive_test_dbname,
        q[ALTER SYSTEM SET pgactive.synchronous_commit = on;]);
    $node->safe_psql($pgactive_test_dbname,
        q[ALTER SYSTEM SET pgactive.conflict_logging_include_tuples = on;]);
    $node->safe_psql($pgactive_test_dbname,
        q[ALTER SYSTEM SET pgactive.debug_trace_replay = on;]);
    $node->safe_psql($pgactive_test_dbname,
        q[SELECT pg_reload_conf();]);
}

my $logstart_0 = get_log_size($node_0);
my $logstart_1 = get_log_size($node_1);

# generate the conflict
$node_0->psql($pgactive_test_dbname, q[INSERT INTO city(city_sid, name) VALUES (1, 'Hyderabad');]);
$node_1->psql($pgactive_test_dbname, q[INSERT INTO city(city_sid, name) VALUES (2, 'Hyderabad');]);

wait_for_apply($node_0, $node_1);
wait_for_apply($node_1, $node_0);

# Check that messages emitted by custom conflict handler are reported in server
# log.
$result = find_in_log($node_0,
	qr!WARNING: ( [A-Z0-9]+:)? conflict insert_insert detected for table:public.city, command_tag:INSERT, local_row:\(1,Hyderabad\), remote_row:\(2,Hyderabad\)!,
	$logstart_0);
ok($result, "custom conflict handler message is found in node0 server log");

$result = find_in_log($node_1,
	qr!WARNING: ( [A-Z0-9]+:)? conflict insert_insert detected for table:public.city, command_tag:INSERT, local_row:\(2,Hyderabad\), remote_row:\(1,Hyderabad\)!,
	$logstart_1);
ok($result, "custom conflict handler message is found in node1 server log");

# check insert/insert output
my $query = q[SELECT count(*) FROM pgactive.pgactive_conflict_history
    WHERE conflict_type = 'insert_insert' AND
    conflict_resolution = 'conflict_trigger_returned_tuple';];

$result = $node_0->safe_psql($pgactive_test_dbname, $query);
is($result, qq(1), 'expected insert_insert custom conflict handler resolution is found on node0');

$result = $node_1->safe_psql($pgactive_test_dbname, $query);
is($result, qq(1), 'expected insert_insert custom conflict handler resolution is found on node1');

$result = $node_0->safe_psql($pgactive_test_dbname, q[SELECT * FROM city;]);
is($result, qq(103|Pune), 'expected row after custom conflict handler resolution is found on node0');

$result = $node_1->safe_psql($pgactive_test_dbname, q[SELECT * FROM city;]);
is($result, qq(103|Pune), 'expected row after custom conflict handler resolution is found on node1');

$result = $node_0->safe_psql($pgactive_test_dbname,
    q[SELECT * FROM pgactive.pgactive_drop_conflict_handler('city', 'city_insert_insert_ch');]);
is($result, qq(), 'dropped custom conflict handler function on node0');

$result = $node_1->safe_psql($pgactive_test_dbname,
    q[SELECT * FROM pgactive.pgactive_drop_conflict_handler('city', 'city_insert_insert_ch');]);
is($result, qq(), 'dropped custom conflict handler function on node1');

done_testing();
