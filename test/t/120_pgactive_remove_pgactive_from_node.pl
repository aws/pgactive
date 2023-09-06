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

# Create an upstream node and bring up pgactive
my $node_a = PostgreSQL::Test::Cluster->new('node_a');
initandstart_pgactive_group($node_a);
my $upstream_node = $node_a;

# Create and use 2.0 Global sequence
create_table_global_sequence( $node_a, 'test_table_sequence' );

# Join a new node to first node using pgactive_join_group
my $node_b = PostgreSQL::Test::Cluster->new('node_b');
initandstart_logicaljoin_node( $node_b, $node_a );

# Create a table foo
$node_b->safe_psql( $pgactive_test_dbname, "create table foo (a int primary key)" );

# Detach node_b from node_a before completely removing pgactive
pgactive_detach_nodes( [$node_b], $node_a );
check_detach_status([$node_b], $node_a);

# Remove pgactive from detached node
pgactive_remove_and_localize_seqs( $node_b, 1 );

# Remove the table foo
$node_b->safe_psql( $pgactive_test_dbname, "drop table foo" );

# Re-create the table foo
$node_b->safe_psql( $pgactive_test_dbname, "create table foo (a int primary key)" );

# Join a new node to first node using pgactive_join_group
my $node_c = PostgreSQL::Test::Cluster->new('node_c');
initandstart_logicaljoin_node( $node_c, $node_a );

# Remove(force) pgactive from node that is not detached
pgactive_remove_and_localize_seqs( $node_c );

#clean up
stop_nodes( [ $node_c, $node_b, $node_a ] );

# Remove pgactive go back to stock postgres and localize global sequences
sub pgactive_remove_and_localize_seqs {
    my $node      = shift;
    my $is_detached = shift;
    if ( defined $is_detached && $is_detached ) {
        # Ensure detached node knows it is actually detached i.e. its
        # node_status in pgactive.pgactive_nodes table is updated as 'k'. Otherwise,
        # pgactive.pgactive_remove() fails with exception:
        # 'this pgactive node might still be active, not removing'.
        my $node_name = $node->name();
	    my $query =
	        qq[SELECT node_status = 'k' FROM pgactive.pgactive_nodes WHERE node_name = '$node_name';];
	    $node->poll_query_until($pgactive_test_dbname, $query)
	        or die "timed out waiting for detached node to know it's detached";

        $node->safe_psql( $pgactive_test_dbname, "select pgactive.pgactive_remove()" );
        is( $node->safe_psql( $pgactive_test_dbname, "select pgactive.pgactive_is_active_in_db()"),
            'f',
            "pgactive is inactive after pgactive.pgactive_remove()"
        );
    }
    else {
        $node->safe_psql( $pgactive_test_dbname, "select pgactive.pgactive_remove(true)" );
        is( $node->safe_psql( $pgactive_test_dbname, "select pgactive.pgactive_is_active_in_db()"),
            'f',
            "pgactive is inactive after pgactive.pgactive_remove(force := true)"
        );
    }
    $node->safe_psql( $pgactive_test_dbname, "drop extension pgactive cascade" );

    # Alter table to use local sequence
    $node->safe_psql( $pgactive_test_dbname,
        "ALTER TABLE test_table_sequence ALTER COLUMN id SET DEFAULT nextval('test_table_sequence_id_seq');");
    insert_into_table_sequence( $node, 'test_table_sequence', 5, 'true' );
    is( $node->safe_psql( $pgactive_test_dbname, "select count(*) from test_table_sequence"),
        '5',
        "Global sequence converted to local sequence"
    );
}

done_testing();
