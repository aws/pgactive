#!/usr/bin/env perl
#
# Test re-joining after detaching and locally removed.
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
my $nodes = make_pgactive_group(2,'node_');
my ($node_0,$node_1) = @$nodes;

# Detach a node from 2 node cluster
note "Detach node_0 from 2 node cluster\n";
pgactive_detach_nodes([$node_0], $node_1);
check_detach_status([$node_0], $node_1);

# Remove pgactive from the detached node
$node_0->safe_psql($pgactive_test_dbname, "select pgactive.pgactive_remove(true)");

# Create a table on the detached and removed node
$node_0->safe_psql($pgactive_test_dbname, "create table db_not_empty(a int primary key)");

# Try re-joining the detached node
my $join_query = generate_pgactive_logical_join_query($node_0, $node_1);

# Must not use safe_psql since we expect an error here
my ($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');
($psql_ret, $psql_stdout, $psql_stderr) = $node_0->psql(
    $pgactive_test_dbname,
    $join_query);
like($psql_stderr, qr/.*ERROR.*database joining pgactive group has existing user tables/,
     "joining of a node failed due to existing user tables in database");

# Ensure database is empty before joining pgactive group
$node_0->safe_psql($pgactive_test_dbname, q[DROP TABLE db_not_empty;]);

# Now re-join the detached node
pgactive_logical_join($node_0, $node_1);
check_join_status($node_0, $node_1);

# Create a table on node_0 (now that it re-joined)
$node_0->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE fruits(id integer, name varchar);]);
$node_0->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (1, 'Cherry');]);
wait_for_apply($node_0, $node_1);

$node_1->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (2, 'Apple');]);
wait_for_apply($node_0, $node_1);

# Check data is available on all pgactive nodes after rejoin
my $query = qq[SELECT COUNT(*) FROM fruits;];
my $expected = 2;
my $node_0_res = $node_0->safe_psql($pgactive_test_dbname, $query);
my $node_1_res = $node_1->safe_psql($pgactive_test_dbname, $query);

is($node_0_res, $expected, "pgactive node node_0 has all the data");
is($node_1_res, $expected, "pgactive node node_1 has all the data");

# Again, detach node_0 from 2 node cluster
note "Detach node_0 from 2 node cluster\n";
pgactive_detach_nodes([$node_0], $node_1);
check_detach_status([$node_0], $node_1);

done_testing();
