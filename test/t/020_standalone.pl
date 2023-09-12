#!/usr/bin/env perl
#
# Tests that operate on a single pgactive node stand-alone, i.e.
# a pgactive group of size 1.
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

my $node_a = PostgreSQL::Test::Cluster->new('node_a');

$node_a->init();
pgactive_update_postgresql_conf($node_a);
$node_a->start;

$node_a->safe_psql('postgres', qq{CREATE DATABASE $pgactive_test_dbname;});
$node_a->safe_psql($pgactive_test_dbname, q{CREATE EXTENSION pgactive;});

is($node_a->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()'), 'f',
	'pgactive is not active on node_a after create extension');

# Bring up a single pgactive node, stand-alone
create_pgactive_group($node_a);

is($node_a->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()'), 't',
	'pgactive is active on node_a after group create');

exec_ddl($node_a, q[CREATE TABLE public.reptest(id integer primary key, dummy text);]);

ok(!$node_a->psql($pgactive_test_dbname, "INSERT INTO reptest (id, dummy) VALUES (1, '42')"), 'simple DML succeeds');

is($node_a->safe_psql($pgactive_test_dbname, 'SELECT dummy FROM reptest WHERE id = 1'), '42', 'simple DDL and insert worked');

is($node_a->safe_psql($pgactive_test_dbname, "SELECT node_status FROM pgactive.pgactive_nodes WHERE node_name = pgactive.pgactive_get_local_node_name()"), 'r', 'node status is "r"');

ok(!$node_a->psql($pgactive_test_dbname, "SELECT pgactive.pgactive_detach_nodes(ARRAY['node_a'])"), 'detached without error');

is($node_a->safe_psql($pgactive_test_dbname, "SELECT node_status FROM pgactive.pgactive_nodes WHERE node_name = pgactive.pgactive_get_local_node_name()"), 'k', 'node status is "k"');

ok($node_a->psql($pgactive_test_dbname, "DROP EXTENSION pgactive"), 'DROP EXTENSION fails after detach');

is($node_a->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db();'), 't', 'still active after detach');

ok(!$node_a->psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_remove(true);'), 'pgactive_remove succeeds');

is($node_a->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db();'), 'f', 'not active after remove');

ok(!$node_a->psql($pgactive_test_dbname, 'DROP EXTENSION pgactive;'), 'extension dropped');

$node_a->stop('fast');

done_testing();
