#!/usr/bin/env perl
#
# Test miscellaneous use-cases
use strict;
use warnings;
use lib 'test/t/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Time::HiRes qw(usleep);
use IPC::Run;
use Test::More;
use utils::nodemanagement;

# Create an upstream node and bring up pgactive
my $nodes = make_pgactive_group(2,'node_');
my ($node_0,$node_1) = @$nodes;

$node_0->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE fruits(id integer PRIMARY KEY, name varchar);]);
$node_0->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (1, 'Mango');]);
wait_for_apply($node_0, $node_1);

# Kill pgactive workers, verify if they come up again and replication works
$node_0->safe_psql($pgactive_test_dbname,
    "SELECT pgactive.pgactive_terminate_workers(node_sysid, node_timeline, node_dboid, 'apply')
     FROM pgactive.pgactive_nodes;");
$node_0->safe_psql($pgactive_test_dbname,
    "SELECT pgactive.pgactive_terminate_workers(node_sysid, node_timeline, node_dboid, 'walsender')
     FROM pgactive.pgactive_nodes;");
$node_0->safe_psql($pgactive_test_dbname,
    "SELECT pgactive.pgactive_terminate_workers(node_sysid, node_timeline, node_dboid, 'per-db')
     FROM pgactive.pgactive_nodes;");

# Let the killed pgactive workers come up
$node_0->poll_query_until($pgactive_test_dbname,
  qq[SELECT COUNT(*) = 1 AS ok FROM pgactive.pgactive_get_workers_info() WHERE worker_type = 'apply';]);
$node_0->poll_query_until($pgactive_test_dbname,
  qq[SELECT COUNT(*) = 1 AS ok FROM pgactive.pgactive_get_workers_info() WHERE worker_type = 'walsender';]);
$node_0->poll_query_until($pgactive_test_dbname,
  qq[SELECT COUNT(*) = 1 AS ok FROM pgactive.pgactive_get_workers_info() WHERE worker_type = 'per-db';]);

$node_0->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (2, 'Apple');]);
wait_for_apply($node_0, $node_1);

$node_0->poll_query_until($pgactive_test_dbname,
  qq[SELECT COUNT(*) = 2 FROM fruits;]);

# Test the capability to set all pgactive nodes read-only
# Set all nodes read-only
$node_0->safe_psql($pgactive_test_dbname,
  qq[SELECT pgactive.pgactive_set_node_read_only(node_name, true) FROM pgactive.pgactive_nodes;]);
$node_0->poll_query_until($pgactive_test_dbname,
  qq[SELECT node_read_only IS true FROM pgactive.pgactive_nodes WHERE node_name = 'node_0';]);
$node_1->poll_query_until($pgactive_test_dbname,
  qq[SELECT node_read_only IS true FROM pgactive.pgactive_nodes WHERE node_name = 'node_1';]);

my $query = qq[CREATE TABLE readonly_test_shoulderror(a int);];
my ($result, $stdout, $stderr) = ('','', '');
($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    $query);
like($stderr, qr/.*ERROR.*cannot run CREATE TABLE on read-only pgactive node/,
     "creation of table on node set to read-only fails");

$query = qq[INSERT INTO fruits VALUES (3, 'Cherry');];
($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    $query);
like($stderr, qr/.*ERROR.*INSERT may only affect UNLOGGED or TEMPORARY tables on read-only pgactive node; fruits is a regular table/,
     "insertion into a table on node set to read-only fails");

$query = qq[UPDATE fruits SET name = 'Berry' WHERE id = 1;];
($result, $stdout, $stderr) = $node_1->psql(
    $pgactive_test_dbname,
    $query);
like($stderr, qr/.*ERROR.*UPDATE may only affect UNLOGGED or TEMPORARY tables on read-only pgactive node; fruits is a regular table/,
     "update of a table on node set to read-only fails");

$query = qq[DELETE FROM fruits WHERE id = 1;];
($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    $query);
like($stderr, qr/.*ERROR.*DELETE may only affect UNLOGGED or TEMPORARY tables on read-only pgactive node; fruits is a regular table/,
     "delete from a table on node set to read-only fails");

$query = qq[COPY public.test_read_only FROM '/tmp/nosuch.csv';];
($result, $stdout, $stderr) = $node_1->psql(
    $pgactive_test_dbname,
    $query);
like($stderr, qr/.*ERROR.*cannot run COPY FROM on read-only pgactive node/,
     "COPY FROM on a table on node set to read-only fails");

$query = qq[WITH cte AS (
	INSERT INTO fruits VALUES (3, 'Cherry') RETURNING *
)
SELECT * FROM cte;];
($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    $query);
like($stderr, qr/.*ERROR.*(DML|SELECT INTO) may only affect UNLOGGED or TEMPORARY tables on read-only pgactive node; fruits is a regular table/,
     "CTE command on node set to read-only fails");

# Temporary tables succeed even when node is set read-only
$result = $node_0->safe_psql($pgactive_test_dbname,
    q[CREATE TEMP TABLE test_read_only_temp(data text);
      INSERT INTO test_read_only_temp VALUES('foo');
      UPDATE test_read_only_temp SET data = 'foo';
      DELETE FROM test_read_only_temp;
      SELECT 'finished';
    ]);
is($result, 'finished', 'check if commands on temporary tables works even when node is set read-only');

# Set all nodes read-write
$node_0->safe_psql($pgactive_test_dbname,
  qq[SELECT pgactive.pgactive_set_node_read_only(node_name, false) FROM pgactive.pgactive_nodes;]);
$node_0->poll_query_until($pgactive_test_dbname,
  qq[SELECT node_read_only IS false FROM pgactive.pgactive_nodes WHERE node_name = 'node_0';]);
$node_1->poll_query_until($pgactive_test_dbname,
  qq[SELECT node_read_only IS false FROM pgactive.pgactive_nodes WHERE node_name = 'node_1';]);

$node_0->safe_psql($pgactive_test_dbname, q[DELETE FROM fruits;]);
wait_for_apply($node_0, $node_1);

$node_0->poll_query_until($pgactive_test_dbname,
  qq[SELECT COUNT(*) = 0 FROM fruits;]);

# The DB name pgactive_supervisordb is reserved by pgactive. None of these
# commands may be permitted.
$query = qq[CREATE DATABASE pgactive_supervisordb;];
# Must not use safe_psql since we expect an error here
($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    $query);
like($stderr, qr/.*ERROR.*pgactive extension reserves the database name pgactive_supervisordb for its own use/,
     "creation of database with a name reserved by pgactive fails");

$query = qq[DROP DATABASE pgactive_supervisordb;];
($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    $query);
like($stderr, qr/.*ERROR.*pgactive extension reserves the database name pgactive_supervisordb for its own use/,
     "dropping of database with a name reserved by pgactive fails");

$query = qq[ALTER DATABASE pgactive_supervisordb RENAME TO someothername;];
($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    $query);
like($stderr, qr/.*ERROR.*pgactive extension reserves the database name pgactive_supervisordb for its own use/,
     "renaming of database with a name reserved by pgactive to other fails");

$query = qq[ALTER DATABASE postgres RENAME TO pgactive_supervisordb;];
($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    $query);
like($stderr, qr/.*ERROR.*pgactive extension reserves the database name pgactive_supervisordb for its own use/,
     "renaming of other database to database with a name reserved by pgactive fails");

# We can connect to the supervisor db; but can only run read-only commands, not
# all, exception is VACUUM command.
$query = qq[SET log_statement = 'all';];
($result, $stdout, $stderr) = $node_0->psql(
    'pgactive_supervisordb',
    $query);
like($stderr, qr/.*ERROR.*no commands may be run on the pgactive supervisor database/,
     "running of write commands (SET) fail on pgactive_supervisordb");

$query = qq[CREATE TABLE create_fails(id integer);];
($result, $stdout, $stderr) = $node_0->psql(
    'pgactive_supervisordb',
    $query);
like($stderr, qr/.*ERROR.*no commands may be run on the pgactive supervisor database/,
     "running of write commands fail on pgactive_supervisordb");

is($node_0->safe_psql('pgactive_supervisordb', "SELECT 1;"),
	1, 'read-only query on pgactive_supervisordb works');

$node_0->safe_psql('pgactive_supervisordb', q[VACUUM;]);

done_testing();
