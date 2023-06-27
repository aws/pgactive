#!/usr/bin/env perl
#
# Use pause and resume to make a test of a 3-node group where we detach a node
# that has replayed changes to one of its peers but not the other. 
#
# This is much the same functionally as the other desync test that uses
# apply_delay, it just does explicit pause and resume instead.
#
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

# Create a cluster of 3 nodes
my $nodes = make_bdr_group(3,'node_');
my $node_0 = $nodes->[0];
my $node_1 = $nodes->[1];
my $node_2 = $nodes->[2];

# Create a test table
my $test_table = "apply_pause_test";
my $value = 1;
create_table($node_0,"$test_table");

wait_for_apply($node_0,$node_1);
wait_for_apply($node_0,$node_2);

$node_0->safe_psql($bdr_test_dbname,"select bdr.bdr_apply_pause()");
# Pause doesn't try to wait for any sort of acknowledgement from the worker and
# there's no way to check if a worker is paused, so we'd better wait a
# moment...
sleep(1);

$node_2->safe_psql($bdr_test_dbname,qq(INSERT INTO $test_table VALUES($value)));

# Check changes from node_2 are replayed on node_1 and not on node_0
wait_for_apply($node_2,$node_1);
is($node_1->safe_psql($bdr_test_dbname,"SELECT id FROM $test_table"),
    $value,"Changes replayed to node_1");
is($node_0->safe_psql($bdr_test_dbname,"SELECT id FROM $test_table"),
    '',"Changes not replayed to node_0 due to apply pause");

detach_and_check_nodes([$node_2],$node_1);

$node_0->safe_psql($bdr_test_dbname,"select bdr.bdr_apply_resume()");
wait_for_apply($node_0,$node_1);
wait_for_apply($node_1,$node_0);

TODO: {
    # See detailed explanation in t/050_node_loss_desync.pl
    local $TODO = 'BDR EHA required';
    is($node_0->safe_psql($bdr_test_dbname,"SELECT id FROM $test_table"),
        '',"Changes not replayed to " . $node_0->name() . " after resume");
}

done_testing();
