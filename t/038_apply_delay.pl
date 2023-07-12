#!/usr/bin/env perl
#
# This test, added for RT#59119, verfies that apply_delay works.
#
use strict;
use warnings;
use lib 't/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use IPC::Run;
use Test::More;
use utils::nodemanagement;

# Create an upstream node and bring up bdr
my $nodes = make_bdr_group(2,'node_');
my ($node_0,$node_1) = @$nodes;

exec_ddl($node_0, q[CREATE TABLE public.city(city_sid INT PRIMARY KEY, name VARCHAR, UNIQUE(name));]);
wait_for_apply($node_0, $node_1);

foreach my $node ($node_0, $node_1)
{
    $node->safe_psql($bdr_test_dbname,
        q[ALTER SYSTEM SET bdr.default_apply_delay = 500;]);
    $node->safe_psql($bdr_test_dbname,
        q[ALTER SYSTEM SET bdr.log_conflicts_to_table = on;]);
    $node->safe_psql($bdr_test_dbname,
        q[ALTER SYSTEM SET bdr.synchronous_commit = on;]);
    $node->safe_psql($bdr_test_dbname,
        q[ALTER SYSTEM SET bdr.conflict_logging_include_tuples = on;]);
    $node->safe_psql($bdr_test_dbname,
        q[SELECT pg_reload_conf();]);
}

# Repeat a conflicting action multiple times.
#
# apply_delay isn't a sleep after each apply, its the minimum age before
# an xact may be applied on the peer(s).
my ($nerrors_0, $nerrors_1) = (0,0);
foreach my $i (0..2)
{
    $nerrors_0 += (0 != $node_0->psql($bdr_test_dbname, q[INSERT INTO city(city_sid, name) VALUES (2, 'Tom Price');]));
    $nerrors_1 += (0 != $node_1->psql($bdr_test_dbname, q[INSERT INTO city(city_sid, name) VALUES (3, 'Tom Price');]));
    $node_1->safe_psql($bdr_test_dbname, q[SELECT * FROM bdr.wait_slot_confirm_lsn(NULL, NULL)]);
    $node_1->safe_psql($bdr_test_dbname, q[DELETE FROM city;]);
    $node_1->safe_psql($bdr_test_dbname, q[SELECT * FROM bdr.wait_slot_confirm_lsn(NULL, NULL)]);
}

my $nconflicts_0 = $node_0->safe_psql($bdr_test_dbname, q[SELECT count(*) FROM bdr.bdr_conflict_history]);
my $nconflicts_1 = $node_1->safe_psql($bdr_test_dbname, q[SELECT count(*) FROM bdr.bdr_conflict_history]);
cmp_ok($nconflicts_0 + $nconflicts_1, "==", 3, "detected required conflicts");

# check insert/insert output
my $ch_query = q[SELECT conflict_type, conflict_resolution, local_tuple, remote_tuple, local_commit_time IS NULL, local_conflict_time - local_commit_time BETWEEN INTERVAL '0.25' SECOND AND INTERVAL '0.75' SECOND FROM bdr.bdr_conflict_history];
my $ch_query_diag = q[SELECT conflict_type, conflict_resolution, local_tuple, remote_tuple, local_commit_time, local_conflict_time, remote_commit_time, local_conflict_time - local_commit_time AS local_times_diff FROM bdr.bdr_conflict_history];
is($node_0->safe_psql($bdr_test_dbname, "SELECT count(*) FROM bdr.bdr_conflict_history"),
   0, "found no conflicts on node0");
is($node_1->safe_psql($bdr_test_dbname, $ch_query . " WHERE conflict_id <= 3"),
   q[insert_insert|last_update_wins_keep_local|{"city_sid":3,"name":"Tom Price"}|{"city_sid":2,"name":"Tom Price"}|f|t
insert_insert|last_update_wins_keep_local|{"city_sid":3,"name":"Tom Price"}|{"city_sid":2,"name":"Tom Price"}|f|t
insert_insert|last_update_wins_keep_local|{"city_sid":3,"name":"Tom Price"}|{"city_sid":2,"name":"Tom Price"}|f|t],
   "expected insert/insert conflicts found on node1")
    or diag $node_1->safe_psql($bdr_test_dbname, $ch_query_diag);

# simple update/update conflict
$node_0->psql($bdr_test_dbname, q[INSERT INTO city(city_sid, name) VALUES (2, 'Tom Price');]);
$node_0->safe_psql($bdr_test_dbname, q[SELECT * FROM bdr.wait_slot_confirm_lsn(NULL, NULL)]);
$node_0->psql($bdr_test_dbname, q[UPDATE city SET name = 'Bork' WHERE city_sid = 2]);
$node_1->psql($bdr_test_dbname, q[UPDATE city SET name = 'Bawk' WHERE city_sid = 2]);
$node_1->safe_psql($bdr_test_dbname, q[SELECT * FROM bdr.wait_slot_confirm_lsn(NULL, NULL)]);
$node_1->safe_psql($bdr_test_dbname, q[DELETE FROM city;]);
$node_1->safe_psql($bdr_test_dbname, q[SELECT * FROM bdr.wait_slot_confirm_lsn(NULL, NULL)]);

is($node_0->safe_psql($bdr_test_dbname, "SELECT count(*) FROM bdr.bdr_conflict_history"),
   0, "found no conflicts on node0");
is($node_1->safe_psql($bdr_test_dbname, $ch_query . " WHERE conflict_id = 4"),
   q[update_update|last_update_wins_keep_local|{"city_sid":2,"name":"Bawk"}|{"city_sid":2,"name":"Bork"}|f|t],
   "expected insert/update conflicts found on node1")
   or diag $node_1->safe_psql($bdr_test_dbname, $ch_query_diag);

# simple update/delete conflict
$node_0->psql($bdr_test_dbname, q[INSERT INTO city(city_sid, name) VALUES (2, 'Tom Price');]);
$node_0->safe_psql($bdr_test_dbname, q[SELECT * FROM bdr.wait_slot_confirm_lsn(NULL, NULL)]);
$node_0->psql($bdr_test_dbname, q[UPDATE city SET name = 'Bork' WHERE city_sid = 2]);
$node_1->psql($bdr_test_dbname, q[DELETE FROM city WHERE name = 'Tom Price']);
$node_1->safe_psql($bdr_test_dbname, q[SELECT * FROM bdr.wait_slot_confirm_lsn(NULL, NULL)]);

is($node_0->safe_psql($bdr_test_dbname, "SELECT count(*) FROM bdr.bdr_conflict_history"),
   0, "found no conflicts on node0");
is($node_1->safe_psql($bdr_test_dbname, $ch_query . " WHERE conflict_id = 5"),
   q[update_delete|skip_change||{"city_sid":2,"name":"Bork"}|t|],
   "expected insert/delete conflicts found on node1")
   or diag $node_1->safe_psql($bdr_test_dbname, $ch_query_diag);

# simple delete/delete conflict
$node_0->psql($bdr_test_dbname, q[INSERT INTO city(city_sid, name) VALUES (2, 'Tom Price');]);
wait_for_apply($node_0, $node_1);

# Delete same tuple on two nodes at the same time to generate delete/delete
# conflict.
foreach my $node (@{$nodes})
{
    $node->psql($bdr_test_dbname, q[DELETE FROM city WHERE city_sid = 2;]);
}

# wait for the delete/delete conflict to be logged
foreach my $node (@{$nodes})
{
    $node->poll_query_until($bdr_test_dbname,
        qq{SELECT COUNT(*) = 1 FROM bdr.bdr_conflict_history WHERE conflict_type = 'delete_delete';});
}

# now, check the expected delete/delete conflict
foreach my $node (@{$nodes})
{
    is($node_0->safe_psql($bdr_test_dbname,
        qq[SELECT conflict_type, conflict_resolution, local_tuple, remote_tuple FROM bdr.bdr_conflict_history
           WHERE conflict_type = 'delete_delete';]),
        q[delete_delete|skip_change||{"city_sid":2,"name":null}],
        "expected delete/delete conflicts found on " . $node->name);
}

done_testing();
