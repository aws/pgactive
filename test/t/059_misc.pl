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
    q[CREATE TABLE fruits(id integer, name varchar);]);
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

# The DB name pgactive_supervisordb is reserved by pgactive. None of these
# commands may be permitted.
my $query = qq[CREATE DATABASE pgactive_supervisordb;];
# Must not use safe_psql since we expect an error here
my ($result, $stdout, $stderr) = ('','', '');
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
