#!/usr/bin/env perl
#
# Test max possible nodes in a BDR group with bdr.max_nodes GUC.
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
