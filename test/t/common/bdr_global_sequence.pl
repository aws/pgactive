#!/usr/bin/perl
#
# Test global sequences 
#
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
use utils::concurrent;

sub global_sequence_tests {
    my $type = shift;

    # Create an upstream node and bring up bdr
    my $node_a = PostgreSQL::Test::Cluster->new('node_a');
    initandstart_bdr_group($node_a);
    my $upstream_node = $node_a;
    my $table_name    = 'test_table_sequence';
    create_table_global_sequence( $node_a, 'test_table_sequence' );
    insert_into_table_sequence( $node_a, 'test_table_sequence', 10 );

    # Join a new node to first node
    # and check insert on table_with_sequence
    my $node_b = PostgreSQL::Test::Cluster->new('node_b');
    check_insert_on_new_joins( $node_a, $type, [$node_b] );

    # Join a multiple nodes to first node
    # and check insert on table_with_sequence
    my $node_c = PostgreSQL::Test::Cluster->new('node_c');
    my $node_d = PostgreSQL::Test::Cluster->new('node_d');
    check_insert_on_new_joins( $node_a, $type, [ $node_c, $node_d ] );

    # Join two nodes concurrently
    note "Two concurrent joins then inserts\n";
    my $node_e = PostgreSQL::Test::Cluster->new('node_e');
    my $node_f = PostgreSQL::Test::Cluster->new('node_f');
  TODO: {
        todo_skip "Concurrent physical joins to an existing 2 node cluster", 4
          if $type eq 'physical';
        check_insert_on_concurrent_joins( $type,
            \@{[ $node_e,$node_a ]},\@{[$node_f,$node_a ]} );
    }

    # Insert on two nodes concurrently
    note "Concurrent insert into table_with_sequence\n";
    my $node_g = PostgreSQL::Test::Cluster->new('node_g');
    initandstart_join_node( $node_g, $upstream_node, $type );
    my $node_h = PostgreSQL::Test::Cluster->new('node_h');
    initandstart_join_node( $node_h, $upstream_node, $type );
    check_concurrent_inserts( $node_a, $table_name, 50, $node_g, $node_h );

    # Node Join under write load to sequence table.
    note "Join node under write load\n";
    join_under_sequence_write_load( $type, $upstream_node, $table_name );

    note "Concurrent physical logical joins\n";
    my $node_k = PostgreSQL::Test::Cluster->new('node_k');
    my $node_l = PostgreSQL::Test::Cluster->new('node_l');
    my $node_m = PostgreSQL::Test::Cluster->new('node_m');
    concurrent_joins_logical_physical(  [\@{[ $node_l,$upstream_node ]}, \@{[ $node_m,$upstream_node ]} ],
            [\@{[ $node_k,$upstream_node ]}] );
    compare_sequence_table_with_upstream(
        "Global Sequence table check after join: ",
        $node_a, $node_k, $node_l, $node_m );

    note "Done, Clean up\n";
    eval {
        part_nodes(
            [
                $node_b, $node_c, $node_d, $node_e, $node_f,
                $node_g, $node_h, $node_k, $node_l, $node_m
            ],
            $upstream_node
        );
    };
}

# Start insert into table_with_sequence
sub start_insert {
    my ( $upstream_node, $table_with_sequence, $no_of_inserts ) = @_;
    my $node_name   = $upstream_node->name();
    my $timeout_exc = 'timed out running psql on node $node_name';
    my $query = "INSERT INTO public.$table_with_sequence(node_name) SELECT '$node_name' FROM generate_series(1,$no_of_inserts);";
    my ( $stdout, $stderr ) = ( '', '' );
    my $handle = IPC::Run::start(
        [
            'psql', '-v', 'ON_ERROR_STOP=1', $upstream_node->connstr($bdr_test_dbname), '-f', '-'
        ],
        '1>', \$stdout, '2>', \$stderr, '<', \$query,
        IPC::Run::timeout( $PostgreSQL::Test::Utils::timeout_default, exception => $timeout_exc )
    );
    return ( $handle, $stdout, $stderr, $query );

}

# Try to join while inserts happeneing on
# sequence table
sub join_under_sequence_write_load {
    my ( $type, $upstream_node, $table_with_sequence ) = @_;
    $upstream_node->safe_psql( $bdr_test_dbname,
        "TRUNCATE TABLE test_table_sequence" );

    # Initiate heavy Inserts on upstream and simultaneously  join new
    # to the cluster
    my ( $h, $stdout, $stderr, $query ) =
      start_insert( $upstream_node, $table_with_sequence, 200 );

    # Start a node to join under write load
    my $node = PostgreSQL::Test::Cluster->new('node_join_under_write_load');

    if ( $type eq 'logical' ) {
        initandstart_logicaljoin_node( $node, $upstream_node );
    }
    elsif ( $type eq 'physical' ) {
        initandstart_physicaljoin_node( $node, $upstream_node );
    }
  TODO: {
        eval { $h->finish; };
        todo_skip "Insert hangs while node join begins", 1 if $@;
        is( $h->full_result(0), 0,
            "psql on node " . $upstream_node->name . " exited normally" );
        compare_sequence_table_with_upstream(
            "New node join while inserts into sequence table: ",
            $upstream_node, $node );
    }
}

# Make concurrent inserts into sequence table
# from two nodes. Also check that global sequence
# ids generated are unique.
sub check_concurrent_inserts {
    my ( $upstream_node, $table_name, $no_of_inserts, @nodes ) = @_;
    $upstream_node->safe_psql( $bdr_test_dbname,
        "TRUNCATE TABLE test_table_sequence" );
    concurrent_inserts( $upstream_node, $table_name, $no_of_inserts, @nodes );
    foreach my $testnode (@nodes) {
        foreach my $node (@nodes) {
            my $entry = $testnode->safe_psql( $bdr_test_dbname, "SELECT COUNT(*) FROM $table_name WHERE node_name like '" . $node->name . "%'" );
            is( $entry, $no_of_inserts, "All inserts on " . $node->name . " OK on " . $testnode->name . "" );
        }
        my $duplicates = $testnode->safe_psql( $bdr_test_dbname, "SELECT COUNT(*) FROM test_table_sequence GROUP BY id HAVING COUNT(*) > 1");
        is( $duplicates, '', "Uniqueness of global sequence " . $testnode->name );
    }
}

# Join nodes to upstream and insert into table_with_sequence
# on each node.
sub check_insert_on_new_joins {
    my ( $upstream_node, $type, $join_nodes ) = @_;
    $upstream_node->safe_psql( $bdr_test_dbname,
        "TRUNCATE TABLE test_table_sequence" );
    foreach my $join_node ( @{$join_nodes} ) {
        if ( $type eq 'logical' ) {
            initandstart_logicaljoin_node( $join_node, $upstream_node );
            insert_into_table_sequence( $join_node, 'test_table_sequence', 10 );
        }
        elsif ( $type eq 'physical' ) {
            initandstart_physicaljoin_node( $join_node, $upstream_node );
            insert_into_table_sequence( $join_node, 'test_table_sequence', 10 );
        }
        compare_sequence_table_with_upstream(
            "Global Sequence table check after join: ",
            $upstream_node, $join_node );
    }
}

# Join multiple nodes concurrently and insert into table_with_sequence
# on each node
sub check_insert_on_concurrent_joins {
    my (  $type, @nodes ) = @_;
    my $upstream_node;
    my @join_nodes;
    foreach my $join_node (@nodes) {
        push @join_nodes,@{$join_node}[0];
        $upstream_node = @{$join_node}[1];
    }
    $upstream_node->safe_psql( $bdr_test_dbname,
        "TRUNCATE TABLE test_table_sequence" );
    if ( $type eq 'logical' ) {
        concurrent_joins( 'logical', @nodes );
    }
    elsif ( $type eq 'physical' ) {
        concurrent_joins( 'physical', @nodes );
    }
    foreach my $join_node ( @join_nodes ) {
        insert_into_table_sequence( $join_node, 'test_table_sequence', 10 );
    }
    compare_sequence_table_with_upstream(
        "Global Sequence table check after join: ",
        $upstream_node, @join_nodes );
}
1;
