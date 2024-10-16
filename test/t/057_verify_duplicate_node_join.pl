#!/usr/bin/env perl
#
# Tests that duplicate node name is rejected
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

# check that with more than one node exclude or include are not possible
my $nodes = make_pgactive_group(2,'node_');
my ($node_0,$node_1) = @$nodes;

# Remove pgactive trace from node_1 without detaching it
$node_1->safe_psql($pgactive_test_dbname,
    q[SELECT pgactive.pgactive_remove(true);]);

# Rejoin node_1, this will fail with duplicate node error
my $join_query = generate_pgactive_logical_join_query($node_1, $node_0);

my ($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');
($psql_ret, $psql_stdout, $psql_stderr) = $node_1->psql($pgactive_test_dbname, $join_query);

like($psql_stderr, qr/ERROR:  node_name already present on remote/,
     "DETAIL:  Node name 'node_1' is present on remote with node_status != 'k'");

$node_0->stop;
$node_1->stop;

done_testing();
