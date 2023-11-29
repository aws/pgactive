#!/usr/bin/env perl
#
# Test miscellaneous use-cases
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

$node_0->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE fruits(id integer, name varchar);]);
$node_0->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (1, 'Mango');]);
wait_for_apply($node_0, $node_1);

# Kill pgactive workers, verify if they come up again and replication works
$node_0->safe_psql($pgactive_test_dbname,
    "SELECT pgactive.pgactive_terminate_workers(node_sysid, node_timeline, node_dboid, 'apply')
     FROM pgactive.pgactive_nodes;");
$node_0->safe_psql($pgactive_test_dbname,
    "SELECT pgactive.pgactive_terminate_workers(node_sysid, node_timeline, node_dboid, 'walsender')
     FROM pgactive.pgactive_nodes;");
$node_0->safe_psql($pgactive_test_dbname,
    "SELECT pgactive.pgactive_terminate_workers(node_sysid, node_timeline, node_dboid, 'per-db')
     FROM pgactive.pgactive_nodes;");

# Let the killed pgactive workers come up
$node_0->poll_query_until($pgactive_test_dbname,
  qq[SELECT COUNT(*) = 1 AS ok FROM pgactive.pgactive_get_workers_info() WHERE worker_type = 'apply';]);
$node_0->poll_query_until($pgactive_test_dbname,
  qq[SELECT COUNT(*) = 1 AS ok FROM pgactive.pgactive_get_workers_info() WHERE worker_type = 'walsender';]);
$node_0->poll_query_until($pgactive_test_dbname,
  qq[SELECT COUNT(*) = 1 AS ok FROM pgactive.pgactive_get_workers_info() WHERE worker_type = 'per-db';]);

$node_0->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (2, 'Apple');]);
wait_for_apply($node_0, $node_1);

$node_0->poll_query_until($pgactive_test_dbname,
  qq[SELECT COUNT(*) = 2 FROM fruits;]);

done_testing();
