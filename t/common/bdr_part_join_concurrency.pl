#!/usr/bin/env perl
use strict;
use warnings;
use lib "t/";
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use utils::nodemanagement;
use utils::concurrent;
use Test::More;

sub bdr_part_join_concurrency_tests {

    my $type = shift;

    # Create an upstream node and bring up bdr
    my $node_a = PostgreSQL::Test::Cluster->new('node_a');
    initandstart_bdr_group($node_a);
    my $upstream_node = $node_a;

    # Join two nodes concurrently
    my $node_f = PostgreSQL::Test::Cluster->new('node_f');
    my $node_g = PostgreSQL::Test::Cluster->new('node_g');
    concurrent_joins( $type, \@{ [ $node_f, $upstream_node ] }, \@{ [ $node_g, $upstream_node ] });

    # Part two nodes concurrently
    concurrent_part( \@{ [ $node_f, $upstream_node ] }, \@{ [ $node_g, $upstream_node ] });
    stop_nodes( [ $node_f, $node_g ] );

    # Join three nodes concurrently
    my $node_h = PostgreSQL::Test::Cluster->new('node_h');
    my $node_i = PostgreSQL::Test::Cluster->new('node_i');
    my $node_j = PostgreSQL::Test::Cluster->new('node_j');
    concurrent_joins( $type, \@{ [ $node_h, $upstream_node ] }, \@{ [ $node_i, $upstream_node ] }, \@{ [ $node_j, $upstream_node ] });

    # Three concurent part
    concurrent_part( \@{ [ $node_h, $upstream_node ] }, \@{ [ $node_i, $upstream_node ] }, \@{ [ $node_j, $upstream_node ] });
    stop_nodes( [ $node_h, $node_i, $node_j ] );

    note "Concurrent part and join\n";

    # Concurrent part and join.
    my $node_k = PostgreSQL::Test::Cluster->new('node_k');
    initandstart_join_node( $node_k, $node_a, $type );
    my $node_l = PostgreSQL::Test::Cluster->new('node_l');
    my $node_m = PostgreSQL::Test::Cluster->new('node_m');
    concurrent_join_part( $type, $upstream_node, [ $node_l, $node_m ], [$node_k] );

    note "Clean up\n";
    part_and_check_nodes( [ $node_l, $node_m ], $upstream_node );
    stop_nodes( [$node_k] );
    
    note "Concurrent part from different upstream\n";
    # Join a new node to create a 2 node cluster
    my $node_b = PostgreSQL::Test::Cluster->new('node_b');
    initandstart_join_node( $node_b, $node_a, $type );

    # part nodes concurrently from different upstreams
    # node_P from node_a and node_Q from node_b
    my $node_P = PostgreSQL::Test::Cluster->new('node_P');
    my $node_Q = PostgreSQL::Test::Cluster->new('node_Q');
    initandstart_join_node( $node_P, $node_a, $type );
    initandstart_join_node( $node_Q, $node_b, $type );
    concurrent_part( \@{ [ $node_P, $node_a ] }, \@{ [ $node_Q, $node_b ] } );

    note "Concurrent join to 2+ nodes cluster\n";
    # Concurrent join to an existing two node cluster
    my $node_1 = PostgreSQL::Test::Cluster->new('node_1');
    my $node_2 = PostgreSQL::Test::Cluster->new('node_2');
   concurrent_joins( $type, \@{ [ $node_1, $upstream_node ] }, \@{ [ $node_2, $upstream_node ] });
    
    note "Concurrent join to different upstreams\n";
    # Concurrent join to different upstreams
    # node_3 => node_a  and node_4 => node_b
    my $node_3 = PostgreSQL::Test::Cluster->new('node_3');
    my $node_4 = PostgreSQL::Test::Cluster->new('node_4');
    concurrent_joins( $type, \@{ [ $node_3, $node_a ] }, \@{ [ $node_4, $node_b ] });
    note "done\n";
    # Clean up
    stop_nodes( [ $node_1, $node_2,$node_3,$node_4,$node_b, $node_a ] );
}
1;
