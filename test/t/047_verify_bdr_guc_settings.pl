#!/usr/bin/env perl
#
# Test max possible nodes in a BDR group with bdr.max_nodes GUC.
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
use utils::nodemanagement qw(
		:DEFAULT
		generate_bdr_logical_join_query
		);

# Create an upstream node and bring up bdr
my $node_a = PostgreSQL::Test::Cluster->new('node_a');
initandstart_bdr_group($node_a);

$node_a->append_conf('postgresql.conf', q{bdr.max_nodes = 2});
$node_a->restart;

my $upstream_node = $node_a;

# Create a node with different value for bdr.max_nodes parameter, and try
# joining to the BDR group - that must fail.
my $node_b = PostgreSQL::Test::Cluster->new('node_b');
initandstart_node($node_b);

my $logstart_b = get_log_size($node_b);

$node_b->append_conf('postgresql.conf', q{bdr.max_nodes = 4});
$node_b->restart;

my $join_query = generate_bdr_logical_join_query($node_b, $upstream_node);

# Must not use safe_psql since we expect an error here
my ($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');
($psql_ret, $psql_stdout, $psql_stderr) = $node_b->psql(
    $bdr_test_dbname,
    $join_query);
like($psql_stderr, qr/joining node and BDR group have different values for bdr.max_nodes parameter/,
     "joining of a node failed due to different values for bdr.max_nodes parameter");

# Change bdr.max_nodes value on joining node to make it successfully join the
# BDR group.
$node_b->append_conf('postgresql.conf', qq(bdr.max_nodes = 2));
$node_b->restart;

bdr_logical_join($node_b, $upstream_node);
check_join_status($node_b, $upstream_node);

# Change/deviate bdr.max_nodes value from the group and restart the node, the
# node mustn't start per-db and apply workers.
$node_b->append_conf('postgresql.conf', qq(bdr.max_nodes = 4));
$node_b->restart;
my $result = find_in_log($node_b,
	qr[ERROR:  bdr.max_nodes parameter value .* on local node .* doesn't match with remote node .*],
	$logstart_b);
ok($result, "bdr.max_nodes parameter value mismatch between local node and remote node is detected");

# Change bdr.max_nodes value on node to make it successfully start per-db and
# apply workers.
$node_b->append_conf('postgresql.conf', qq(bdr.max_nodes = 2));
$node_b->restart;

# Try joining a 3rd node when the BDR group's bdr.max_nodes limit is only 2,
# the joining must fail.
my $node_c = PostgreSQL::Test::Cluster->new('node_c');
initandstart_node($node_c);
$node_c->append_conf('postgresql.conf', qq(bdr.max_nodes = 2));
$node_c->restart;

$join_query = generate_bdr_logical_join_query($node_c, $upstream_node);

# Must not use safe_psql since we expect an error here
($psql_ret, $psql_stdout, $psql_stderr) = $node_c->psql(
    $bdr_test_dbname,
    $join_query);
like($psql_stderr, qr/cannot allow more than bdr.max_nodes number of nodes in a BDR group/,
     "joining of a node failed due to bdr.max_nodes limit reached");

# Create some data on upstream node after node_b joins the group successfully.
$node_a->safe_psql($bdr_test_dbname,
    q[CREATE TABLE fruits(id integer, name varchar);]);
$node_a->safe_psql($bdr_test_dbname,
    q[INSERT INTO fruits VALUES (1, 'Cherry');]);
wait_for_apply($node_a, $node_b);

$node_b->safe_psql($bdr_test_dbname,
    q[INSERT INTO fruits VALUES (2, 'Apple');]);
wait_for_apply($node_b, $node_a);

is($node_a->safe_psql($bdr_test_dbname, q[SELECT COUNT(*) FROM fruits;]),
   '2', "Changes available on node_a");
is($node_b->safe_psql($bdr_test_dbname, q[SELECT COUNT(*) FROM fruits;]),
   '2', "Changes available on node_b");

$node_a->stop;
$node_b->stop;
$node_c->stop;

# bdr.skip_ddl_replication check

# Create an upstream node and bring up bdr
my $node_0 = PostgreSQL::Test::Cluster->new('node_0');
initandstart_bdr_group($node_0);

$node_0->append_conf('postgresql.conf', q{bdr.skip_ddl_replication = true});
$node_0->restart;

$upstream_node = $node_0;

# Create a node with different value for bdr.skip_ddl_replication and try
# joining to the BDR group - that must fail.
my $node_1 = PostgreSQL::Test::Cluster->new('node_1');
initandstart_node($node_1);

my $logstart_0 = get_log_size($node_0);

$node_1->append_conf('postgresql.conf', q{bdr.skip_ddl_replication = false});
$node_1->restart;

$join_query = generate_bdr_logical_join_query($node_1, $upstream_node);

# Must not use safe_psql since we expect an error here
($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');
($psql_ret, $psql_stdout, $psql_stderr) = $node_1->psql(
    $bdr_test_dbname,
    $join_query);
like($psql_stderr, qr/joining node and BDR group have different values for bdr.skip_ddl_replication parameter/,
     "joining of a node failed due to different values for bdr.skip_ddl_replication parameter");

# Change bdr.skip_ddl_replication.max_nodes value on joining node to make it successfully join the
# BDR group.
$node_1->append_conf('postgresql.conf', qq(bdr.skip_ddl_replication = true));
$node_1->restart;

bdr_logical_join($node_1, $upstream_node);
check_join_status($node_1, $upstream_node);

# This time, on the "creator" node, change/deviate bdr.skip_ddl_replication value
# from the group and restart the node, the node mustn't start per-db and apply workers.
$node_0->append_conf('postgresql.conf', qq(bdr.skip_ddl_replication = false));
$node_0->restart;
$result = find_in_log($node_0,
	qr[ERROR:  bdr.skip_ddl_replication parameter value .* on local node .* doesn't match with remote node .*],
	$logstart_0);
ok($result, "bdr.skip_ddl_replication parameter value mismatch between local node and remote node is detected");

# Change bdr.max_nodes value on node to make it successfully start per-db and
# apply workers.
$node_0->append_conf('postgresql.conf', qq(bdr.skip_ddl_replication = true));
$node_0->restart;

# Create some data on upstream node after node_1 joins the group successfully.
$node_0->safe_psql($bdr_test_dbname,
    q[CREATE TABLE fruits(id integer, name varchar);]);
$node_1->safe_psql($bdr_test_dbname,
    q[CREATE TABLE fruits(id integer, name varchar);]);
$node_0->safe_psql($bdr_test_dbname,
    q[INSERT INTO fruits VALUES (1, 'Cherry');]);
wait_for_apply($node_0, $node_1);

$node_1->safe_psql($bdr_test_dbname,
    q[INSERT INTO fruits VALUES (2, 'Apple');]);
wait_for_apply($node_1, $node_0);

is($node_0->safe_psql($bdr_test_dbname, q[SELECT COUNT(*) FROM fruits;]),
   '2', "Changes available on node_0");
is($node_1->safe_psql($bdr_test_dbname, q[SELECT COUNT(*) FROM fruits;]),
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

$node_0->stop;
$node_1->stop;

done_testing();

# Return the size of logfile of $node in bytes
sub get_log_size
{
	my ($node) = @_;

	return (stat $node->logfile)[7];
}

# Find $pat in logfile of $node after $off-th byte
sub find_in_log
{
	my ($node, $pat, $off) = @_;
	#my $max_attempts = $PostgreSQL::Test::Utils::timeout_default * 10;
	my $max_attempts = 60 * 10;
	my $log;

	while ($max_attempts-- >= 0)
	{
		$log = PostgreSQL::Test::Utils::slurp_file($node->logfile, $off);
		last if ($log =~ m/$pat/);
		usleep(100_000);
	}

	return $log =~ m/$pat/;
}
