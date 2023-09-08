#!/usr/bin/env perl
#
# Logically join a node (which is a base backup of another node) to an upstream
# node. With pgactive generating its own node identifier, this should work without
# having to use physical join with pgactive_init_copy.
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

# Create an upstream node and bring up pgactive
my $node_a = PostgreSQL::Test::Cluster->new('node_a');
initandstart_pgactive_group($node_a);
my $upstream_node = $node_a;

# Create a base backup of upstream node to make another pgactive member from it.
my $backup_name = 'my_backup';
$node_a->backup($backup_name);

my $node_b = PostgreSQL::Test::Cluster->new('node_b');
$node_b->init_from_backup($node_a, $backup_name);
$node_b->start;

# Let's get rid of pgactive completely on restored instance
$node_b->safe_psql($pgactive_test_dbname, qq[SELECT pgactive.pgactive_remove(true);]);
$node_b->safe_psql($pgactive_test_dbname, qq[DROP EXTENSION pgactive;]);

# Let's restart for pgactive supervisor worker to go away
$node_b->restart;

# Create some data on upstream node. We do this after base backup is done; just
# for testing purposes. For pgactive logical join to work, database mustn't contain
# any objects. Because, logical join does a dump and restore of the database
# from the upstream node, and fails if any pre-existing objects of the same
# name exist in the database of the node that's logically joinig.
$node_a->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE fruits(id integer, name varchar);]);
$node_a->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (1, 'Mango');]);

# Logically join node_b (a base backup of node_a) to upstream node node_a. With
# pgactive generating its own node identifier, this should work.
note "Logically join node_b (a base backup of node_a) to node_a\n";
$node_b->safe_psql($pgactive_test_dbname, qq[CREATE EXTENSION pgactive;]);
pgactive_logical_join($node_b, $upstream_node);
check_join_status($node_b, $upstream_node);

$node_a->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (2, 'Cherry');]);
wait_for_apply($node_a, $node_b);

$node_b->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (3, 'Apple');]);
wait_for_apply($node_b, $node_a);

is($node_a->safe_psql($pgactive_test_dbname, q[SELECT COUNT(*) FROM fruits;]),
   '3', "Changes not available on node_a");

is($node_b->safe_psql($pgactive_test_dbname, q[SELECT COUNT(*) FROM fruits;]),
   '3', "Changes not available on node_b");

note "Detach node_b from pgactive group\n";
detach_and_check_nodes( [ $node_b ], $upstream_node );

done_testing();
