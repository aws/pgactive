#!/usr/bin/env perl
#
# Test max possible nodes in a pgactive group with pgactive.max_nodes GUC.
# Also test that skip_ddl_replication has to be the same on all nodes.
use strict;
use warnings;
use lib 'test/t/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Time::HiRes qw(usleep);
use IPC::Run;
use Test::More;
use utils::nodemanagement;

# Create an upstream node and bring up pgactive
my $node_a = PostgreSQL::Test::Cluster->new('node_a');
initandstart_pgactive_group($node_a);

$node_a->append_conf('postgresql.conf', q{pgactive.max_nodes = 2});
$node_a->restart;

my $upstream_node = $node_a;

# Create a node with different value for pgactive.max_nodes parameter, and try
# joining to the pgactive group - that must fail.
my $node_b = PostgreSQL::Test::Cluster->new('node_b');
initandstart_node($node_b);

$node_b->append_conf('postgresql.conf', q{pgactive.max_nodes = 4});
$node_b->restart;

my $join_query = generate_pgactive_logical_join_query($node_b, $upstream_node);

# Must not use safe_psql since we expect an error here
my ($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');
($psql_ret, $psql_stdout, $psql_stderr) = $node_b->psql(
    $pgactive_test_dbname,
    $join_query);
like($psql_stderr, qr/joining node and pgactive group have different values for pgactive.max_nodes parameter/,
     "joining of a node failed due to different values for pgactive.max_nodes parameter");

# Change pgactive.max_nodes value on joining node to make it successfully join the
# pgactive group.
$node_b->append_conf('postgresql.conf', qq(pgactive.max_nodes = 2));
$node_b->restart;

pgactive_logical_join($node_b, $upstream_node);
check_join_status($node_b, $upstream_node);

# Change/deviate pgactive.max_nodes value from the group and restart the node, the
# node mustn't start per-db and apply workers.
$node_b->append_conf('postgresql.conf', qq(pgactive.max_nodes = 4));

my $logstart_b = get_log_size($node_b);
$node_b->restart;
my $result = find_in_log($node_b,
	qr[ERROR:  pgactive.max_nodes parameter value .* on local node .* doesn't match with remote node node_a value .*],
	$logstart_b);
ok($result, "pgactive.max_nodes parameter value mismatch between local node and remote node is detected");

# Change pgactive.max_nodes value on node to make it successfully start per-db and
# apply workers.
$node_b->append_conf('postgresql.conf', qq(pgactive.max_nodes = 2));
$node_b->restart;

# Try joining a 3rd node when the pgactive group's pgactive.max_nodes limit is only 2,
# the joining must fail.
my $node_c = PostgreSQL::Test::Cluster->new('node_c');
initandstart_node($node_c);
$node_c->append_conf('postgresql.conf', qq(pgactive.max_nodes = 2));
$node_c->restart;

$join_query = generate_pgactive_logical_join_query($node_c, $upstream_node);

# Must not use safe_psql since we expect an error here
($psql_ret, $psql_stdout, $psql_stderr) = $node_c->psql(
    $pgactive_test_dbname,
    $join_query);
like($psql_stderr, qr/cannot allow more than pgactive.max_nodes number of nodes in a pgactive group/,
     "joining of a node failed due to pgactive.max_nodes limit reached");

# Create some data on upstream node after node_b joins the group successfully.
$node_a->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE fruits(id integer, name varchar);]);
$node_a->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (1, 'Cherry');]);
wait_for_apply($node_a, $node_b);

$node_b->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (2, 'Apple');]);
wait_for_apply($node_b, $node_a);

is($node_a->safe_psql($pgactive_test_dbname, q[SELECT COUNT(*) FROM fruits;]),
   '2', "Changes available on node_a");
is($node_b->safe_psql($pgactive_test_dbname, q[SELECT COUNT(*) FROM fruits;]),
   '2', "Changes available on node_b");

$node_a->stop;
$node_b->stop;
$node_c->stop;

# pgactive.skip_ddl_replication check

# Create an upstream node and bring up pgactive
my $node_0 = PostgreSQL::Test::Cluster->new('node_0');
initandstart_pgactive_group($node_0);

$node_0->append_conf('postgresql.conf', q{pgactive.skip_ddl_replication = true});
$node_0->restart;

$upstream_node = $node_0;

# Create a node with different value for pgactive.skip_ddl_replication and try
# joining to the pgactive group - that must fail.
my $node_1 = PostgreSQL::Test::Cluster->new('node_1');
initandstart_node($node_1);

$node_1->append_conf('postgresql.conf', q{pgactive.skip_ddl_replication = false});
$node_1->restart;

$join_query = generate_pgactive_logical_join_query($node_1, $upstream_node);

# Must not use safe_psql since we expect an error here
($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');
($psql_ret, $psql_stdout, $psql_stderr) = $node_1->psql(
    $pgactive_test_dbname,
    $join_query);
like($psql_stderr, qr/joining node and pgactive group have different values for pgactive.skip_ddl_replication parameter/,
     "joining of a node failed due to different values for pgactive.skip_ddl_replication parameter");

# Change pgactive.skip_ddl_replication.max_nodes value on joining node to make it successfully join the
# pgactive group.
$node_1->append_conf('postgresql.conf', qq(pgactive.skip_ddl_replication = true));
$node_1->restart;

pgactive_logical_join($node_1, $upstream_node);
check_join_status($node_1, $upstream_node);

# This time, on the "creator" node, change/deviate pgactive.skip_ddl_replication value
# from the group and restart the node, the node mustn't start per-db and apply workers.
$node_0->append_conf('postgresql.conf', qq(pgactive.skip_ddl_replication = false));
my $logstart_0 = get_log_size($node_0);
$node_0->restart;
$result = find_in_log($node_0,
	qr[ERROR:  pgactive.skip_ddl_replication parameter value .* on local node .* doesn't match with remote node node_1 value .*],
	$logstart_0);
ok($result, "pgactive.skip_ddl_replication parameter value mismatch between local node and remote node is detected");

# Change pgactive.max_nodes value on node to make it successfully start per-db and
# apply workers.
$node_0->append_conf('postgresql.conf', qq(pgactive.skip_ddl_replication = true));
$node_0->restart;

# Create some data on upstream node after node_1 joins the group successfully.
$node_0->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE fruits(id integer, name varchar);]);
$node_1->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE fruits(id integer, name varchar);]);
$node_0->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (1, 'Cherry');]);
wait_for_apply($node_0, $node_1);

$node_1->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (2, 'Apple');]);
wait_for_apply($node_1, $node_0);

is($node_0->safe_psql($pgactive_test_dbname, q[SELECT COUNT(*) FROM fruits;]),
   '2', "Changes available on node_0");
is($node_1->safe_psql($pgactive_test_dbname, q[SELECT COUNT(*) FROM fruits;]),
   '2', "Changes available on node_1");

$node_0->stop;
$node_1->stop;

# Check that we error out if we are not able to connect to any remote nodes

$logstart_0 = get_log_size($node_0);
$node_0->start;

$result = find_in_log($node_0,
	qr[FATAL:  local node.*is not able to connect to any remote node to compare its parameters with.*],
	$logstart_0);
ok($result, "Error out if no remote nodes to compare with");

# Check no error as soon as local node can connect to one remote node
$node_1->start;
$logstart_0 = get_log_size($node_0);

$result = !find_in_log($node_0,
	qr[FATAL:  local node.*is not able to connect to any remote node to compare its parameters with.*],
	$logstart_0);
ok($result, "No error out as soon as local node can connect to one remote node to compare with");

# Check that generic error for connection failure is emitted
$node_0->append_conf('postgresql.conf', qq(pgactive.debug_trace_connection_errors = false));
$node_0->reload;

# Must not use safe_psql since we expect an error here
($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');
($psql_ret, $psql_stdout, $psql_stderr) = $node_0->psql(
    $pgactive_test_dbname,
    q[SELECT * FROM pgactive._pgactive_get_node_info_private('dbname=unknown');]);
like($psql_stderr, qr/.*ERROR.*could not connect to the server in replication mode: connection failed/,
     "generic error for connection failure is detected");

done_testing();
