#!/usr/bin/env perl
#
# [Join/Detach] Use per-node apply_delay to make a test of a 3-node group
# where we detach a node that has replayed changes to one of its peers but 
# not the other. 
#
# Verify that the remaining 2 nodes are consistent after detach of the 3rd node.
#
# This test exercises a BDR behaviour where, if one node goes down while its
# peers have replayed up to different points on the lost node, we cannot
# re-sync from the furthest-ahead peer.
#
use strict;
use warnings;
use lib 't/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use threads;
use Test::More;
use utils::nodemanagement;

# Create an upstream node and bring up bdr
my $node_a = PostgreSQL::Test::Cluster->new('node_a');
initandstart_bdr_group($node_a);
my $upstream_node = $node_a;

# Join a new node to first node using bdr_join_group and apply delay
my $node_b = PostgreSQL::Test::Cluster->new('node_b');
my $delay = 1000; # ms
initandstart_node($node_b);
bdr_logical_join( $node_b, $upstream_node,apply_delay=>$delay );
check_join_status( $node_b,$upstream_node);

my $node_c = PostgreSQL::Test::Cluster->new('node_c');
initandstart_logicaljoin_node($node_c,$node_a);

#
# Node C took a global DDL lock on node A when it joined. We need to wait until
# the lock is released on A before we can successfully run a CREATE TABLE on A,
# otherwise it'll conflict with the other transaction (from node C's join) that
# still holds the DDL lock.
#
# Without an apply_delay this happens so quickly we don't need to care.
#
# There's no blocking mode for DDL lock acquisition, which would make this all
# much simpler (but introduce deadlock hazards instead), so we must wait until
# the lock is free.
#
# TODO: add a blocking-mode DDL lock acquisition function so we can avoid this
# and save users the hassle. See #59, #60
#
$node_b->poll_query_until($bdr_test_dbname,"select NOT EXISTS (SELECT * from bdr.bdr_global_locks);");

# Create a test table
my $table_name = "delaytest";
my $value = 1;
create_table($node_a,$table_name);	

# We can't INSERT until the DDL lock for CREATE TABLE is clear
$node_b->poll_query_until($bdr_test_dbname,"select NOT EXISTS (SELECT * from bdr.bdr_global_locks);");

# INSERT some changes into C
#
# This change will replicate immediately to node A, and on a delay to node B
# due to B's configuration.
#
$node_c->safe_psql($bdr_test_dbname,qq(INSERT INTO $table_name VALUES($value)));
wait_for_apply($node_c, $node_a);

# Check changes are replayed on node_b and not on node_a

is($node_a->safe_psql($bdr_test_dbname,"SELECT id FROM $table_name"),
    $value, "Changes replayed to node_a");

is($node_b->safe_psql($bdr_test_dbname,"SELECT id FROM $table_name"),
    '', "Changes not replayed to node_b due to apply delay");

# Detach node_c before the change can replay to b
detach_and_check_nodes([$node_c],$node_a);

# Make sure B is fully caught up with A's changes and vice versa
wait_for_apply($node_a, $node_b);
wait_for_apply($node_b, $node_a);

TODO: {
    # Right now, BDR doesn't know how to connect to the furthest-ahead
    # surviving peer of a lost node and catch up using it. When that's
    # implemented as part of BDR extended HA work, this test should
    # start passing.
    #
    local $TODO = "BDR EHA: we should copy C's changes from A to B";
    is($node_b->safe_psql($bdr_test_dbname,"SELECT id FROM $table_name"),
        $value, "Changes from node_c recovered on node_b via node_a");
}

done_testing();
