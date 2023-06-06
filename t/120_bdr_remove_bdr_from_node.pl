#!/usr/bin/perl
use strict;
use warnings;
use lib 't/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use utils::nodemanagement;
use utils::sequence;

# Create an upstream node and bring up bdr
my $node_a = PostgreSQL::Test::Cluster->new('node_a');
initandstart_bdr_group($node_a);
my $upstream_node = $node_a;

# Create and use 2.0 Global sequence
create_table_global_sequence( $node_a, 'test_table_sequence' );

# Join a new node to first node using bdr_group_join
my $node_b = PostgreSQL::Test::Cluster->new('node_b');
initandstart_logicaljoin_node( $node_b, $node_a );

# Part node_b before completely removing BDR
part_nodes( [$node_b], $node_a );
sleep(10);

# Remove BDR from parted node
bdr_remove( $node_b, 1 );

# Join a new node to first node using bdr_group_join
my $node_c = PostgreSQL::Test::Cluster->new('node_c');
initandstart_logicaljoin_node( $node_c, $node_a );

# Remove(force) BDR from node that is not parted
bdr_remove($node_c);

#clean up
stop_nodes( [ $node_c, $node_b, $node_a ] );

# Remove BDR go back to stock postgres
# while 2.0 Global sequences are in use
sub bdr_remove {
    my $node      = shift;
    my $is_parted = shift;
    if ( defined $is_parted && $is_parted ) {
        $node->safe_psql( $bdr_test_dbname, "select bdr.remove_bdr_from_local_node()" );
        is( $node->safe_psql( $bdr_test_dbname, "select bdr.bdr_is_active_in_db()"),
            'f',
            "BDR is active status after BDR remove of parted node"
        );
    }
    else {
        $node->safe_psql( $bdr_test_dbname, "select bdr.remove_bdr_from_local_node(true)" );
        is( $node->safe_psql( $bdr_test_dbname, "select bdr.bdr_is_active_in_db()"),
            'f',
            "BDR is active status after BDR force remove"
        );
    }
    $node->safe_psql( $bdr_test_dbname, "drop extension bdr cascade" );
    $node->safe_psql( $bdr_test_dbname, "drop extension btree_gist" );

    # Alter table to use local sequence
    $node->safe_psql( $bdr_test_dbname,
"ALTER TABLE test_table_sequence ALTER COLUMN id SET DEFAULT nextval('test_table_sequence_id_seq');");
    insert_into_table_sequence( $node, 'test_table_sequence', 5, 'true' );
    is( $node->safe_psql( $bdr_test_dbname, "select count(*) from test_table_sequence"),
        '5',
        "Global sequence converted to local sequence"
    );
}

done_testing();
