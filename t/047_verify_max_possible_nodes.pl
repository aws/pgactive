#!/usr/bin/env perl
#
# Test max possible nodes in a BDR group with bdr.max_nodes GUC.
use strict;
use warnings;
use lib 't/';
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
$node_a->safe_psql($bdr_test_dbname, q[SELECT pg_reload_conf();]);

my $upstream_node = $node_a;

# Create a node with different value for bdr.max_nodes parameter, and try
# joining to the BDR group - that must fail.
my $node_b = PostgreSQL::Test::Cluster->new('node_b');
initandstart_node($node_b);

$node_b->append_conf('postgresql.conf', q{bdr.max_nodes = 4});
$node_b->safe_psql($bdr_test_dbname, q[SELECT pg_reload_conf();]);

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
$node_b->safe_psql($bdr_test_dbname, q[SELECT pg_reload_conf();]);

bdr_logical_join($node_b, $upstream_node);
check_join_status($node_b, $upstream_node);

# Try joining a 3rd node when the BDR group's bdr.max_nodes limit is only 2,
# the joing must fail.
my $node_c = PostgreSQL::Test::Cluster->new('node_c');
initandstart_node($node_c);
$node_c->append_conf('postgresql.conf', qq(bdr.max_nodes = 2));
$node_c->safe_psql($bdr_test_dbname, q[SELECT pg_reload_conf();]);

$join_query = generate_bdr_logical_join_query($node_c, $upstream_node);

# Must not use safe_psql since we expect an error here
($psql_ret, $psql_stdout, $psql_stderr) = $node_c->psql(
    $bdr_test_dbname,
    $join_query);
like($psql_stderr, qr/cannot allow more than bdr.max_nodes number of nodes in a BDR group/,
     "joining of a node failed due to bdr.max_nodes limit reached");

done_testing();
