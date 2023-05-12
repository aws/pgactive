#!/usr/bin/env perl
#
# Tests that operate on a single BDR node stand-alone, i.e.
# a BDR group of size 1.
#
use strict;
use warnings;
use lib 'compat/11/t/';
use Cwd;
use Config;
use PostgresNode;
use TestLib;
use Test::More;
use utils::nodemanagement;

my $node_a = get_new_node('node_a');

$node_a->init();
$node_a->append_conf('postgresql.conf', q{
wal_level = logical
track_commit_timestamp = on
shared_preload_libraries = 'bdr'
});
$node_a->start;

$node_a->safe_psql('postgres', qq{CREATE DATABASE $bdr_test_dbname;});
$node_a->safe_psql($bdr_test_dbname, q{CREATE EXTENSION btree_gist;});
$node_a->safe_psql($bdr_test_dbname, q{CREATE EXTENSION bdr;});

is($node_a->safe_psql($bdr_test_dbname, 'SELECT bdr.bdr_is_active_in_db()'), 'f',
	'BDR is not active on node_a after create extension');

# Bring up a single BDR node, stand-alone
create_bdr_group($node_a);

is($node_a->safe_psql($bdr_test_dbname, 'SELECT bdr.bdr_is_active_in_db()'), 't',
	'BDR is active on node_a after group create');

ok(!$node_a->safe_psql($bdr_test_dbname, q{
SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE TABLE public.reptest(
	id integer primary key,
	dummy text
);
$DDL$);
}), 'simple DDL succeeds');

ok(!$node_a->psql($bdr_test_dbname, "INSERT INTO reptest (id, dummy) VALUES (1, '42')"), 'simple DML succeeds');

is($node_a->safe_psql($bdr_test_dbname, 'SELECT dummy FROM reptest WHERE id = 1'), '42', 'simple DDL and insert worked');

is($node_a->safe_psql($bdr_test_dbname, "SELECT node_status FROM bdr.bdr_nodes WHERE node_name = bdr.bdr_get_local_node_name()"), 'r', 'node status is "r"');

ok(!$node_a->psql($bdr_test_dbname, "SELECT bdr.bdr_part_by_node_names(ARRAY['node_a'])"), 'parted without error');

is($node_a->safe_psql($bdr_test_dbname, "SELECT node_status FROM bdr.bdr_nodes WHERE node_name = bdr.bdr_get_local_node_name()"), 'k', 'node status is "k"');

ok($node_a->psql($bdr_test_dbname, "DROP EXTENSION bdr"), 'DROP EXTENSION fails after part');

is($node_a->safe_psql($bdr_test_dbname, 'SELECT bdr.bdr_is_active_in_db();'), 't', 'still active after part');

ok(!$node_a->psql($bdr_test_dbname, 'SELECT bdr.remove_bdr_from_local_node(true, true);'), 'remove_bdr_from_local_node succeeds');

is($node_a->safe_psql($bdr_test_dbname, 'SELECT bdr.bdr_is_active_in_db();'), 'f', 'not active after remove');

ok(!$node_a->psql($bdr_test_dbname, 'DROP EXTENSION bdr;'), 'extension dropped');

$node_a->stop('fast');

done_testing();
