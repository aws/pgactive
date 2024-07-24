#!/usr/bin/env perl
#
# Tests that replication sets behave as expected.
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

# Create a table
$node_a->safe_psql($pgactive_test_dbname, q{create table settest_1(a int);});

# Check that we can not exclude if no nodes added yet
# Must not use safe_psql since we expect an error here
my ($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');

($psql_ret, $psql_stdout, $psql_stderr) = $node_a->psql(
    $pgactive_test_dbname,
    "select pgactive.pgactive_exclude_table_replication_set('settest_1');");

like($psql_stderr, qr/replication set exclude or include can only be performed/,
     "can not exclude when no node is part of the cluster");

# Same for include
($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');

($psql_ret, $psql_stdout, $psql_stderr) = $node_a->psql(
    $pgactive_test_dbname,
    "select pgactive.pgactive_include_table_replication_set('settest_1');");

like($psql_stderr, qr/replication set exclude or include can only be performed/,
     "can not include when no node is part of the cluster");

# Bring up a single pgactive node, stand-alone
create_pgactive_group($node_a);

is($node_a->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()'), 't',
	'pgactive is active on node_a after group create');

# Check that exclude is possible
ok(!$node_a->safe_psql($pgactive_test_dbname, q{select pgactive.pgactive_exclude_table_replication_set('settest_1');}), "exclude is possible after group creation");

# And include is not possible as exclude has already be done
($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');

($psql_ret, $psql_stdout, $psql_stderr) = $node_a->psql(
    $pgactive_test_dbname,
    "select pgactive.pgactive_include_table_replication_set('settest_1');");

like($psql_stderr, qr/allow an include set setup when exclude set has already been used/,
     "can not include when exclude has already been used");

# Check that set connection replication sets to default is possible
ok(!$node_a->safe_psql($pgactive_test_dbname, q{select pgactive.pgactive_set_connection_replication_sets('{default}','node_a');}), "set connection replication sets to default is possible");

# And to non default is not possible
($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');

($psql_ret, $psql_stdout, $psql_stderr) = $node_a->psql(
    $pgactive_test_dbname,
    "select pgactive.pgactive_set_connection_replication_sets('{myrs}','node_a');");

like($psql_stderr, qr/allow to set connection replication sets but \{default\}/,
     "can not set non default connection replication sets");

# Create another table
$node_a->safe_psql($pgactive_test_dbname, q{create table settest_2(a int);});

# Check that exclude is possible
ok(!$node_a->safe_psql($pgactive_test_dbname, q{select pgactive.pgactive_exclude_table_replication_set('settest_2');}), "exclude is possible after another exclude");

# drop the first excluded table
$node_a->safe_psql($pgactive_test_dbname, q{drop table settest_1;});

# create a table
$node_a->safe_psql($pgactive_test_dbname, q{create table settest_3(a int);});

# Check that include is still not possible
($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');

($psql_ret, $psql_stdout, $psql_stderr) = $node_a->psql(
    $pgactive_test_dbname,
    "select pgactive.pgactive_include_table_replication_set('settest_3');");

like($psql_stderr, qr/allow an include set setup when exclude set has already been used/,
     "can not include when exclude is remaining");

# drop the second excluded table
$node_a->safe_psql($pgactive_test_dbname, q{drop table settest_2;});

# Check that include is now possible
ok(!$node_a->safe_psql($pgactive_test_dbname, q{select pgactive.pgactive_include_table_replication_set('settest_3');}), "include is possible when no exclude is left");

# So that exclude is not possible anymore
$node_a->safe_psql($pgactive_test_dbname, q{create table settest_1(a int);});

($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');

($psql_ret, $psql_stdout, $psql_stderr) = $node_a->psql(
    $pgactive_test_dbname,
    "select pgactive.pgactive_exclude_table_replication_set('settest_1');");

like($psql_stderr, qr/allow exclude set setup when an include set has already been used/,
     "can not exclude when include has already been used");

$node_a->stop;

# check that with more than one node exclude or include are not possible
my $nodes = make_pgactive_group(2,'node_');
my ($node_0,$node_1) = @$nodes;

$node_0->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE test(id integer PRIMARY KEY, name varchar);]);
$node_0->safe_psql($pgactive_test_dbname,
    q[INSERT INTO test VALUES (1, '1');]);
wait_for_apply($node_0, $node_1);

# exclude not possible
($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');

($psql_ret, $psql_stdout, $psql_stderr) = $node_0->psql(
    $pgactive_test_dbname,
    "select pgactive.pgactive_exclude_table_replication_set('test');");

like($psql_stderr, qr/replication set exclude or include can only be performed/,
     "can not exclude when more than one node is part of the cluster");

# include not possible
($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');

($psql_ret, $psql_stdout, $psql_stderr) = $node_0->psql(
    $pgactive_test_dbname,
    "select pgactive.pgactive_include_table_replication_set('test');");

like($psql_stderr, qr/replication set exclude or include can only be performed/,
     "can not include when more than one node is part of the cluster");

# Also check that setting connection replication sets is not possible
($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');

($psql_ret, $psql_stdout, $psql_stderr) = $node_0->psql(
    $pgactive_test_dbname,
    "select pgactive.pgactive_set_connection_replication_sets('{default}','node_0');");

like($psql_stderr, qr/set connection replication sets can only be performed/,
     "can not set connection replication sets when more than one node is part of the cluster");

$node_0->stop;
$node_1->stop;

# Now check that replication sets are working

# The exclude part
my $test_exclude_node_a = PostgreSQL::Test::Cluster->new('test_exclude_node_a');

$test_exclude_node_a->init();
pgactive_update_postgresql_conf($test_exclude_node_a);
$test_exclude_node_a->start;

$test_exclude_node_a->safe_psql('postgres', qq{CREATE DATABASE $pgactive_test_dbname;});
$test_exclude_node_a->safe_psql($pgactive_test_dbname, q{CREATE EXTENSION pgactive;});

create_pgactive_group($test_exclude_node_a);

is($test_exclude_node_a->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()'), 't',
	'pgactive is active on test_exclude_node_a after group create');

# Create two tables
$test_exclude_node_a->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE test_exclude(id integer PRIMARY KEY, b integer);]);
$test_exclude_node_a->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE test(id integer PRIMARY KEY, b integer);]);

# Exclude one
$test_exclude_node_a->safe_psql($pgactive_test_dbname, q{select pgactive.pgactive_exclude_table_replication_set('test_exclude');});

# Create the second node
my $test_exclude_node_b = PostgreSQL::Test::Cluster->new('test_exclude_node_b');

$test_exclude_node_b->init();
pgactive_update_postgresql_conf($test_exclude_node_b);
$test_exclude_node_b->start;

$test_exclude_node_b->safe_psql('postgres', qq{CREATE DATABASE $pgactive_test_dbname;});
$test_exclude_node_b->safe_psql($pgactive_test_dbname, q{CREATE EXTENSION pgactive;});

# join the pgactive group
pgactive_logical_join($test_exclude_node_b, $test_exclude_node_a);
check_join_status($test_exclude_node_b, $test_exclude_node_a);

# Insert and ensure it is replicated
$test_exclude_node_a->safe_psql($pgactive_test_dbname, q{insert into test values(1,1)});
wait_for_apply($test_exclude_node_a, $test_exclude_node_b);

is($test_exclude_node_b->safe_psql($pgactive_test_dbname, 'SELECT id,b FROM test;'),
    '1|1', "non exclude insert successfully replicated");

# Other way around
$test_exclude_node_b->safe_psql($pgactive_test_dbname, q{insert into test values(2,2)});
wait_for_apply($test_exclude_node_b, $test_exclude_node_a);

is($test_exclude_node_a->safe_psql($pgactive_test_dbname, 'SELECT id,b FROM test where id = 2;'),
    '2|2', "non exclude insert successfully replicated (other way around)");

# Insert and ensure it is not replicated
$test_exclude_node_a->safe_psql($pgactive_test_dbname, q{insert into test_exclude values(1,1)});
wait_for_apply($test_exclude_node_a, $test_exclude_node_b);

is( $test_exclude_node_b->safe_psql(
        $pgactive_test_dbname, 'SELECT count(*) FROM test_exclude where id = 1;'),
    '0',
    "exclude insert successfully not replicated");

# other way around
$test_exclude_node_b->safe_psql($pgactive_test_dbname, q{insert into test_exclude values(2,2)});
wait_for_apply($test_exclude_node_b, $test_exclude_node_a);

is( $test_exclude_node_a->safe_psql(
        $pgactive_test_dbname, 'SELECT count(*) FROM test_exclude where id = 2;'),
    '0',
    "exclude insert successfully not replicated (other way around)");

# A newly created table is replicated
$test_exclude_node_a->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE test_new(id integer PRIMARY KEY, b integer);]);
$test_exclude_node_a->safe_psql($pgactive_test_dbname, q{insert into test_new values(1,1)});
wait_for_apply($test_exclude_node_a, $test_exclude_node_b);

is($test_exclude_node_b->safe_psql($pgactive_test_dbname, 'SELECT id,b FROM test_new;'),
    '1|1', "newly created is successfully replicated");

$test_exclude_node_a->stop;
$test_exclude_node_b->stop;

# The include part
my $test_include_node_a = PostgreSQL::Test::Cluster->new('test_include_node_a');

$test_include_node_a->init();
pgactive_update_postgresql_conf($test_include_node_a);
$test_include_node_a->start;

$test_include_node_a->safe_psql('postgres', qq{CREATE DATABASE $pgactive_test_dbname;});
$test_include_node_a->safe_psql($pgactive_test_dbname, q{CREATE EXTENSION pgactive;});

create_pgactive_group($test_include_node_a);

is($test_include_node_a->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()'), 't',
	'pgactive is active on test_include_node_a after group create');

# Create two tables
$test_include_node_a->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE test_include(id integer PRIMARY KEY, b integer);]);
$test_include_node_a->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE test(id integer PRIMARY KEY, b integer);]);

# Include one
$test_include_node_a->safe_psql($pgactive_test_dbname, q{select pgactive.pgactive_include_table_replication_set('test_include');});

# Create the second node
my $test_include_node_b = PostgreSQL::Test::Cluster->new('test_include_node_b');

$test_include_node_b->init();
pgactive_update_postgresql_conf($test_include_node_b);
$test_include_node_b->start;

$test_include_node_b->safe_psql('postgres', qq{CREATE DATABASE $pgactive_test_dbname;});
$test_include_node_b->safe_psql($pgactive_test_dbname, q{CREATE EXTENSION pgactive;});

# join the pgactive group
pgactive_logical_join($test_include_node_b, $test_include_node_a);
check_join_status($test_include_node_b, $test_include_node_a);

# Insert and ensure it is replicated
$test_include_node_a->safe_psql($pgactive_test_dbname, q{insert into test_include values(1,1)});
wait_for_apply($test_include_node_a, $test_include_node_b);

is($test_include_node_b->safe_psql($pgactive_test_dbname, 'SELECT id,b FROM test_include;'),
    '1|1', "include insert successfully replicated");

# Other way around
$test_include_node_b->safe_psql($pgactive_test_dbname, q{insert into test_include values(2,2)});
wait_for_apply($test_include_node_b, $test_include_node_a);

is($test_include_node_a->safe_psql($pgactive_test_dbname, 'SELECT id,b FROM test_include where id = 2;'),
    '2|2', "include insert successfully replicated (other way around)");

# Insert and ensure it is not replicated
$test_include_node_a->safe_psql($pgactive_test_dbname, q{insert into test values(1,1)});
wait_for_apply($test_include_node_a, $test_include_node_b);

is( $test_include_node_b->safe_psql(
        $pgactive_test_dbname, 'SELECT count(*) FROM test where id = 1;'),
    '0',
    "non include insert successfully not replicated");

# other way around
$test_include_node_b->safe_psql($pgactive_test_dbname, q{insert into test values(2,2)});
wait_for_apply($test_include_node_b, $test_include_node_a);

is( $test_include_node_a->safe_psql(
        $pgactive_test_dbname, 'SELECT count(*) FROM test where id = 2;'),
    '0',
    "non include insert successfully not replicated (other way around)");

# A newly created table is not replicated
$test_include_node_a->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE test_new(id integer PRIMARY KEY, b integer);]);
$test_include_node_a->safe_psql($pgactive_test_dbname, q{insert into test_new values(1,1)});
wait_for_apply($test_include_node_a, $test_include_node_b);

is( $test_include_node_b->safe_psql(
        $pgactive_test_dbname, 'SELECT count(*) FROM test_new where id = 1;'),
    '0',
    "newly created is not replicated");

$test_include_node_a->stop;
$test_include_node_b->stop;

done_testing();
