#!/usr/bin/env perl
#
# Test a mixture of logical and physical joins executing concurrently.
#
# This is intended to turn up issues in the detach/join protocol.
#
use strict;
use warnings;
use lib 't/';
use threads;
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use utils::nodemanagement;
use utils::concurrent;

# Create an upstream node and bring up bdr
my $node_a = PostgreSQL::Test::Cluster->new('node_a');
initandstart_bdr_group($node_a);
my $upstream_node = $node_a;

note "Concurrent logical and pysical join\n";
my $node_k = PostgreSQL::Test::Cluster->new('node_k');
my $node_l = PostgreSQL::Test::Cluster->new('node_l');
my $node_m = PostgreSQL::Test::Cluster->new('node_m');
concurrent_joins_logical_physical([\@{ [$node_l,$upstream_node]},\@{ [$node_m,$upstream_node]}],[\@{[$node_k,$upstream_node]}]);

note "Clean up\n";
detach_and_check_nodes([$node_m,$node_k,$node_l],$node_a);
stop_nodes([$node_a]);

SKIP: {
# TODO: node_q hangs in catch up state never reaching ready state thus gets
# stuck in bdr.bdr_node_join_wait_for_ready(). This is because node_q's per-db
# worker fails to find replication identifier for node_a on it
# (bdr_locks_startup() -> bdr_fetch_node_id_via_sysid() ->
# replorigin_by_name()) and restarts continuously. Note that node_a holds
# global DDL lock as it is an upstream node for node_p. Skip this test case for
# now and fix it after a bit more deeper understanding.
    skip "node_q hangs in catch up state";
    # Concurrent logical physical joins
    # to different upstreams
    # node_p==logicaljoin=>node_a and node_q==physical_join=>node_l
    my $node_p = PostgreSQL::Test::Cluster->new('node_p');
    my $node_q = PostgreSQL::Test::Cluster->new('node_q');
    concurrent_joins_logical_physical([\@{ [$node_p,$node_a]}],[\@{[$node_q,$node_l]}]);

    #clean up
    stop_nodes([$node_l,$node_p,$node_q,$node_a]);
}

done_testing();
