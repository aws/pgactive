#!/usr/bin/env perl
use strict;
use warnings;
use lib 'test/t/';
use threads;
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use utils::nodemanagement;
use utils::concurrent;

sub pgactive_detach_join_tests {
    my $type = shift;

    # Create an upstream node and bring up pgactive
    my $node_a = PostgreSQL::Test::Cluster->new('node_a');
    initandstart_pgactive_group($node_a);
    my $upstream_node = $node_a;

    # Join a new node to first node using pgactive_join_group
    my $node_b = PostgreSQL::Test::Cluster->new('node_b');
    initandstart_join_node( $node_b, $node_a, $type );

    # Detach a node from two node cluster
    note "Detach node-b from two node cluster\n";
    detach_and_check_nodes( [$node_b], $node_a );

    # Join a new nodes to same upstream node after detach.
    # And create 3+ node cluster
    note "Join new nodes C, D, E to same upstream node after detach of B\n";
    dump_nodes_statuses($node_a);
    my $node_c = PostgreSQL::Test::Cluster->new('node_c');
    initandstart_join_node( $node_c, $node_a, $type );
    my $node_d = PostgreSQL::Test::Cluster->new('node_d');
    initandstart_join_node( $node_d, $node_a, $type );
    my $node_e = PostgreSQL::Test::Cluster->new('node_e');
    initandstart_join_node( $node_e, $node_a, $type );

    # Detach nodes in series  from multinode  cluster
    note "Detach nodes C, D, E from multi node cluster\n";
    detach_and_check_nodes( [ $node_c, $node_d, $node_e ], $node_a );
}
1;
