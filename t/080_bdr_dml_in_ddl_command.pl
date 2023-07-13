#!/usr/bin/env perl
#
# Tests of mixed DDL and DML.
#
# BDR 2.0 has significant limiations when mixing DDL and DML. The lack of full
# DDL deparse means we have to do statement-based replication for DDL. If there's
# DML mixed in with DDL in a bdr.bdr_replicate_ddl_command or multi-statement,
# we'll queue both for replay. But since we'd also replay the DML directly,
# we refuse.
#
# https://github.com/2ndQuadrant/bdr-private/issues/23
#
# TODO: PostgreSQL 10: use the new ProcessUtility_hook statement range capture
# to improve this.
#
# Alternately, we could possibly allow mixed DML and DDL if we were run with
# bdr.do_not_replicate=true and used WAL messages to log the DDL, but it seems
# fragile and unsafe. We'd also lose the handy log of DDL actions.
#
use strict;
use warnings;
use lib 't/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use threads;
use Test::More;
use utils::nodemanagement;

# Create an upstream node and bring up bdr
my $node_a = PostgreSQL::Test::Cluster->new('node_a');
initandstart_bdr_group($node_a);
my $upstream_node = $node_a;

# Create a table with some data
my $table_name = "dml_test";
create_table($node_a,$table_name);
$node_a->safe_psql($bdr_test_dbname,"INSERT INTO $table_name VALUES(1),(2)");

# Join a new node to first node using bdr_join_group
my $node_b = PostgreSQL::Test::Cluster->new('node_b');
initandstart_logicaljoin_node($node_b,$node_a);

#  bdr.bdr_replicate_ddl_command(...) disallows DML (INSERT/UPDATE/DELETE).
#  ... or it should. TODO.

TODO: {
    local $TODO = 'bdr_replicate_ddl_command needs to filter out DML?';

    my ($dml_statement, $error_msg);

    # INSERT test
    $dml_statement = qq{SELECT bdr.bdr_replicate_ddl_command(\$DDL\$ INSERT INTO public.$table_name VALUES(3);\$DDL\$);};
    $error_msg = "Direct DML statements are not supported";
    dml_fail($node_b,$dml_statement,$error_msg);
    check_table($table_name,$node_a,$node_b);

    # UPDATE Test
    $dml_statement = qq{SELECT bdr.bdr_replicate_ddl_command(\$DDL\$ UPDATE public.$table_name SET id = 4 WHERE id = 1;\$DDL\$);};
    $error_msg = "Direct DML statements are not supported";
    dml_fail($node_b,$dml_statement,$error_msg);
    check_table($table_name,$node_a,$node_b);

    # DELETE Test
    $dml_statement = qq{SELECT bdr.bdr_replicate_ddl_command(\$DDL\$ DELETE FROM public.$table_name WHERE id = 2;\$DDL\$);};
    $error_msg = "Direct DML statements are not supported";
    dml_fail($node_b,$dml_statement,$error_msg);
    check_table($table_name,$node_a,$node_b);
};

sub dml_fail {
    my ($node, $dml_statement, $error_msg) = @_;
	my ($ret, $stdout, $stderr) = $node->psql($bdr_test_dbname,$dml_statement);
    is($ret, 3, 'psql returned with expected error code');
	like($stderr, qr/$error_msg/, "psql error message for dml statement in DDL command");
}

sub check_table {
	my ($table_name, @nodes) = @_;
	foreach my $node (@nodes) {
		my $node_name = $node->name();
		my $table_values = $node->safe_psql($bdr_test_dbname,"SELECT * FROM $table_name");
		is($table_values,"1\n2","Data should not be modified in $table_name on $node_name"); 
	}
}

done_testing();
