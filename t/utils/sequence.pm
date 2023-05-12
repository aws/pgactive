#!/usr/bin/env perl
#
# Shared test code that doesn't relate directly to simple
# BDR node management.
#
package utils::sequence;

use strict;
use warnings;
use 5.8.0;
use Exporter;
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use utils::nodemanagement;
use utils::concurrent;

use vars qw(@ISA @EXPORT @EXPORT_OK);
@ISA    = qw(Exporter);
@EXPORT = qw(
  create_table_global_sequence
  insert_into_table_sequence
  compare_sequence_table_with_upstream
);
@EXPORT_OK = qw();

# Create a bigint column table with default sequence using a BDR 2.0 global sequence
sub create_table_global_sequence {
    my ( $node, $table_name ) = @_;

    exec_ddl( $node, qq{CREATE SEQUENCE public.${table_name}_id_seq;} );
    exec_ddl( $node, qq{ CREATE TABLE public.$table_name (
                        id bigint NOT NULL DEFAULT bdr.global_seq_nextval('public.${table_name}_id_seq'), node_name text); });
    exec_ddl( $node, qq{ALTER SEQUENCE public.${table_name}_id_seq OWNED BY public.$table_name.id;});
}

# Insert into table_sequence
sub insert_into_table_sequence {
    my ( $node, $table_name, $no_of_inserts, $no_node_join_check) = @_;

    my $node_name = $node->name();

    if (not defined $no_node_join_check) {
        $node->safe_psql( $bdr_test_dbname,
            qq[SELECT bdr.bdr_node_join_wait_for_ready($PostgreSQL::Test::Utils::timeout_default)]);
    }

    if ( not defined $no_of_inserts ) {
        $no_of_inserts = 1;
    }

    for ( my $i = 1 ; $i <= $no_of_inserts ; $i++ ) {
        $node->safe_psql( $bdr_test_dbname, " INSERT INTO public.$table_name(node_name) VALUES('$node_name')" );
    }
}

# Compare sequence table records on nodes
# with that on upstream node
sub compare_sequence_table_with_upstream {
    my ( $message, $upstream_node, @nodes ) = @_;

    my $upstream_record = $upstream_node->safe_psql( $bdr_test_dbname, "SELECT * FROM public.test_table_sequence" );
    foreach my $node (@nodes) {
        my $node_record = $node->safe_psql( $bdr_test_dbname, "SELECT * FROM public.test_table_sequence" );
        is( $upstream_record, $node_record, $message . $node->name . "" );
    }
}

sub join_under_sequence_write_load {
    my ( $type, $upstream_node, $table_with_sequence ) = @_;
    $upstream_node->safe_psql( $bdr_test_dbname,
        "TRUNCATE TABLE test_table_sequence" );

    my ( $h, $stdout, $stderr, $query ) = start_insert( $upstream_node, $table_with_sequence, 200 );

    my $node = PostgreSQL::Test::Cluster->new('node_join_under_write_load');

    if ( $type eq 'logical' ) {
        initandstart_logicaljoin_node( $node, $upstream_node );
    }
    elsif ( $type eq 'physical' ) {
        initandstart_physicaljoin_node( $node, $upstream_node );
    }
  SKIP: {
        eval { $h->finish; };
        skip "Insert hangs while node join begins", 1 if $@;
        is( $h->full_result(0), 0, "psql on node " . $upstream_node->name . " exited normally" );
        compare_sequence_table_with_upstream( "New node join while inserts into sequence table: ", $upstream_node, $node );
    }
}
1;
