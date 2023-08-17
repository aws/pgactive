#!/usr/bin/perl
use strict;
use warnings;
use lib 'test/t/';
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

# Join a new node to first node using bdr_join_group
my $node_b = PostgreSQL::Test::Cluster->new('node_b');
initandstart_logicaljoin_node( $node_b, $node_a );

# Create a table foo
$node_b->safe_psql( $bdr_test_dbname, "create table foo (a int primary key)" );

# Detach node_b from node_a before completely removing BDR
bdr_detach_nodes( [$node_b], $node_a );
check_detach_status([$node_b], $node_a);

# Remove BDR from detached node
bdr_remove_and_localize_seqs( $node_b, 1 );

# Remove the table foo
$node_b->safe_psql( $bdr_test_dbname, "drop table foo" );

# Re-create the table foo
$node_b->safe_psql( $bdr_test_dbname, "create table foo (a int primary key)" );

# Join a new node to first node using bdr_join_group
my $node_c = PostgreSQL::Test::Cluster->new('node_c');
initandstart_logicaljoin_node( $node_c, $node_a );

# Remove(force) BDR from node that is not detached
bdr_remove_and_localize_seqs( $node_c );

#clean up
stop_nodes( [ $node_c, $node_b, $node_a ] );

# Remove BDR go back to stock postgres and localize global sequences
sub bdr_remove_and_localize_seqs {
    my $node      = shift;
    my $is_detached = shift;
    if ( defined $is_detached && $is_detached ) {
        # Ensure detached node knows it is actually detached i.e. its
        # node_status in bdr.bdr_nodes table is updated as 'k'. Otherwise,
        # bdr.bdr_remove() fails with exception:
        # 'this BDR node might still be active, not removing'.
        my $node_name = $node->name();
	    my $query =
	        qq[SELECT node_status = 'k' FROM bdr.bdr_nodes WHERE node_name = '$node_name';];
	    $node->poll_query_until($bdr_test_dbname, $query)
	        or die "timed out waiting for detached node to know it's detached";

        $node->safe_psql( $bdr_test_dbname, "select bdr.bdr_remove()" );
        is( $node->safe_psql( $bdr_test_dbname, "select bdr.bdr_is_active_in_db()"),
            'f',
            "BDR is inactive after bdr.bdr_remove()"
        );
    }
    else {
        $node->safe_psql( $bdr_test_dbname, "select bdr.bdr_remove(true)" );
        is( $node->safe_psql( $bdr_test_dbname, "select bdr.bdr_is_active_in_db()"),
            'f',
            "BDR is inactive after bdr.bdr_remove(force := true)"
        );
    }
    $node->safe_psql( $bdr_test_dbname, "drop extension bdr cascade" );

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
