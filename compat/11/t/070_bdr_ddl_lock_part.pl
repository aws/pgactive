#!/usr/bin/env perl
#
# This test is intended to verify that if a node is cleanly parted
# from the group while holding the global DDL lock, the lock will
# become available for another peer to take.
#
use strict;
use warnings;
use lib 'compat/11/t/';
use Cwd;
use Config;
use PostgresNode;
use TestLib;
use Test::More;
use utils::nodemanagement;

# Create an upstream node and bring up bdr
my $nodes = make_bdr_group(3,'node_');
my $node_0 = $nodes->[0];
my $node_1 = $nodes->[1];
my $node_2 = $nodes->[2];

# Acquire the global ddl lock in a background psql session so that 
# we keep holding it until we commit/abort.
my ($psql_stdin, $psql_stdout, $psql_stderr) = ('','', '');
note "Acquiring global ddl lock on node_1";
my $timer = IPC::Run::timeout($TestLib::timeout_default);
my $handle = start_acquire_ddl_lock($node_1, 'ddl_lock', $timer);
note "waiting for lock acqusition";
wait_acquire_ddl_lock($handle);
note "acquired";

is( $node_0->safe_psql( $bdr_test_dbname, "SELECT state FROM bdr.bdr_global_locks"), 'acquired', "ddl lock acquired");

print("Global DDL lock state on node_0 is: " . $node_0->safe_psql($bdr_test_dbname, 'SELECT * FROM bdr.bdr_locks') . "\n");
print("Global DDL lock state on node_1 is: " . $node_1->safe_psql($bdr_test_dbname, 'SELECT * FROM bdr.bdr_locks') . "\n");
print("Global DDL lock state on node_2 is: " . $node_2->safe_psql($bdr_test_dbname, 'SELECT * FROM bdr.bdr_locks') . "\n");
is(
    $node_0->safe_psql($bdr_test_dbname, 'SELECT lock_state, lock_mode, owner_node_name, owner_is_my_node, owner_is_my_backend FROM bdr.bdr_locks'),
    'peer_confirmed|ddl_lock|node_1|f|f',
    'node 0 confirmed lock as peer');
is(
    $node_1->safe_psql($bdr_test_dbname, 'SELECT lock_state, lock_mode, owner_node_name, owner_is_my_node, owner_is_my_backend FROM bdr.bdr_locks'),
    'acquire_acquired|ddl_lock|node_1|t|f',
    'node 1 confirmed lock as acquirer');
is(
    $node_2->safe_psql($bdr_test_dbname, 'SELECT lock_state, lock_mode, owner_node_name, owner_is_my_node, owner_is_my_backend FROM bdr.bdr_locks'),
    'peer_confirmed|ddl_lock|node_1|f|f',
    'node 2 confirmed lock as peer');

# Part node node_1. The part should fail if node_1 currently holds the global
# DDL lock.  (or we should release it?).
TODO: {
    local $TODO = 'ddl lock check on part not implemented yet';
    is($node_0->psql( $bdr_test_dbname, "SELECT bdr.bdr_part_by_node_names(ARRAY['node_1'])" ),
        3, 'part_by_node_names call should fail');
    is( $node_0->safe_psql( $bdr_test_dbname, "SELECT node_status FROM bdr.bdr_nodes WHERE node_name = 'node_1' "), 'r',
        "Part should fail");
};

# If the node that holds the DDL lock goes down permanently while holding the
# DDL lock, parting the node with bdr.bdr_part_by_node_names() will release the
# lock on other nodes.
#
# Bug 2ndQuadrant/bdr-private#72
TODO: {
    local $TODO = 'ddl lock release on part not implemented yet';
    is( $node_0->safe_psql( $bdr_test_dbname, "SELECT lock_state FROM bdr.bdr_locks"), 'nolock', "ddl lock released after part");
};

# Because we have to terminate the apply worker it can take a little while for
# the lock to be released.
$node_0->poll_query_until($bdr_test_dbname, "SELECT lock_state = 'nolock' FROM bdr.bdr_locks");

is( $node_0->safe_psql( $bdr_test_dbname, "SELECT lock_state FROM bdr.bdr_locks"), 'nolock', "ddl lock released after part");
is( $node_0->safe_psql( $bdr_test_dbname, "SELECT state FROM bdr.bdr_global_locks"), '', "bdr.bdr_global_locks row removed");

# TODO:
#
# Have a node try to acquire the DDL lock while another node is down, so it can
# never successfully acquire it. Run the acquire command in the background; if
# we ran in the foreground with a timer the lock attempt would get released
# when the backend died and the xact aborted.
#
# Then hard-kill the node that's trying to acquire the lock. Verify that the
# other nodes consider it still held. Part the acquiring node from the others
# and verify that the lock was force-released.

done_testing();
