#!/usr/bin/env perl
use strict;
use warnings;
use lib "compat/11/t/";
use Cwd;
use Config;
use PostgresNode;
use TestLib;
use utils::nodemanagement;
use utils::concurrent;
use Test::More;

sub bdr_part_join_concurrency_tests {

    my $type = shift;

    # Create an upstream node and bring up bdr
    my $node_a = get_new_node('node_a');
    initandstart_bdr_group($node_a);
    my $upstream_node = $node_a;

    # Join two nodes concurrently
    my $node_f = get_new_node('node_f');
    my $node_g = get_new_node('node_g');
    concurrent_joins( $type, \@{ [ $node_f, $upstream_node ] }, \@{ [ $node_g, $upstream_node ] });

    # Part two nodes concurrently
    concurrent_part( \@{ [ $node_f, $upstream_node ] }, \@{ [ $node_g, $upstream_node ] });
    stop_nodes( [ $node_f, $node_g ] );

    # Join three nodes concurrently
    my $node_h = get_new_node('node_h');
    my $node_i = get_new_node('node_i');
    my $node_j = get_new_node('node_j');
    concurrent_joins( $type, \@{ [ $node_h, $upstream_node ] }, \@{ [ $node_i, $upstream_node ] }, \@{ [ $node_j, $upstream_node ] });

    # Three concurent part
    concurrent_part( \@{ [ $node_h, $upstream_node ] }, \@{ [ $node_i, $upstream_node ] }, \@{ [ $node_j, $upstream_node ] });
    stop_nodes( [ $node_h, $node_i, $node_j ] );

    note "Concurrent part and join\n";

    # Concurrent part and join.
    my $node_k = get_new_node('node_k');
    initandstart_join_node( $node_k, $node_a, $type );
    my $node_l = get_new_node('node_l');
    my $node_m = get_new_node('node_m');
    concurrent_join_part( $type, $upstream_node, [ $node_l, $node_m ], [$node_k] );

    note "Clean up\n";
    part_and_check_nodes( [ $node_l, $node_m ], $upstream_node );
    stop_nodes( [$node_k] );
    
    note "Concurrent part from different upstream\n";
    # Join a new node to create a 2 node cluster
    my $node_b = get_new_node('node_b');
    initandstart_join_node( $node_b, $node_a, $type );

    # part nodes concurrently from different upstreams
    # node_P from node_a and node_Q from node_b
    my $node_P = get_new_node('node_P');
    my $node_Q = get_new_node('node_Q');
    initandstart_join_node( $node_P, $node_a, $type );
    initandstart_join_node( $node_Q, $node_b, $type );
    concurrent_part( \@{ [ $node_P, $node_a ] }, \@{ [ $node_Q, $node_b ] } );

    note "Concurrent join to 2+ nodes cluster\n";
    # Concurrent join to an existing two node cluster
    my $node_1 = get_new_node('node_1');
    my $node_2 = get_new_node('node_2');
   concurrent_joins( $type, \@{ [ $node_1, $upstream_node ] }, \@{ [ $node_2, $upstream_node ] });
    
    note "Concurrent join to different upstreams\n";
    # Concurrent join to different upstreams
    # node_3 => node_a  and node_4 => node_b
    my $node_3 = get_new_node('node_3');
    my $node_4 = get_new_node('node_4');
    concurrent_joins( $type, \@{ [ $node_3, $node_a ] }, \@{ [ $node_4, $node_b ] });
    note "done\n";
    # Clean up
    stop_nodes( [ $node_1, $node_2,$node_3,$node_4,$node_b, $node_a ] );
}
1;
