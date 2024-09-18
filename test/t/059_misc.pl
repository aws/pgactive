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

# Simulate a write from some unknown peer node by defining a replication
# origin and using it in our session. We must not forward the writes generated
# after replication origin is setup.
$node_0->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE origin_filter(id integer primary key not null, n1 integer not null);]);
$node_0->safe_psql($pgactive_test_dbname,
    q[INSERT INTO origin_filter VALUES (1, 1);]);
wait_for_apply($node_0, $node_1);

$node_0->safe_psql($pgactive_test_dbname,
  q[SELECT pg_replication_origin_create('demo_origin');
    INSERT INTO origin_filter(id, n1) VALUES (2, 2);
    SELECT pg_replication_origin_session_setup('demo_origin');
    INSERT INTO origin_filter(id, n1) VALUES (3, 3);
    BEGIN;
    SELECT pg_replication_origin_xact_setup('1/1', current_timestamp);
    INSERT INTO public.origin_filter(id, n1) values (4, 4);
    COMMIT;
  ]);
wait_for_apply($node_0, $node_1);

# Writes generated (i.e., rows (3,3) and (4,4)) after the replication origin is
# setup won't be forwarded to the other node.
my $result_expected = '1|1
2|2';
$result = $node_1->safe_psql($pgactive_test_dbname, q[SELECT * FROM origin_filter;]);
is($result, $result_expected, 'check if writes generated after the replication origin is set up are not forwarded');

# Test node identifier related things
# No real way to test the sysid, so ignore it
$result = $node_1->safe_psql($pgactive_test_dbname,
  q[SELECT timeline = 0, dboid = (SELECT oid FROM pg_database WHERE datname = current_database())
    FROM pgactive.pgactive_get_local_nodeid();]);
is($result, 't|t', 'check if TLI and dboid matches with that of local node id');

my $pgport = $node_0->port;
my $pghost = $node_0->host;
my $node_0_connstr = "port=$pgport host=$pghost dbname=$pgactive_test_dbname";

$query = qq[SELECT
	r.sysid != l.sysid,
	r.timeline = l.timeline,
	variant = pgactive.pgactive_variant(),
	version = pgactive.pgactive_version(),
	version_num = pgactive.pgactive_version_num(),
	min_remote_version_num = pgactive.pgactive_min_remote_version_num(),
	has_required_privs = 't'
FROM pgactive._pgactive_get_node_info_private('$node_0_connstr') r,
     pgactive.pgactive_get_local_nodeid() l;];

$result = $node_1->safe_psql($pgactive_test_dbname, $query);
is($result, 't|t|t|t|t|t|t', 'check if remote node info matches with that of local node');

$pgport = $node_1->port;
$pghost = $node_1->host;
my $node_1_connstr = "port=$pgport host=$pghost dbname=$pgactive_test_dbname";

$query = qq[SELECT
    r.dboid = (SELECT oid FROM pg_database WHERE datname = current_database())
FROM pgactive._pgactive_get_node_info_private('$node_1_connstr') r;];

$result = $node_1->safe_psql($pgactive_test_dbname, $query);
is($result, 't', 'check if local node info matches when connected to itself');

# Verify that parsing slot names then formatting them again produces round-trip
# output.
$result = $node_0->safe_psql($pgactive_test_dbname,
    q[WITH namepairs(orig, remote_sysid, remote_timeline, remote_dboid, local_dboid, replication_name, formatted)
        AS (
          SELECT
            s.slot_name, p.*, pgactive.pgactive_format_slot_name(p.remote_sysid, p.remote_timeline, p.remote_dboid, p.local_dboid, '')
          FROM pg_catalog.pg_replication_slots s,
            LATERAL pgactive.pgactive_parse_slot_name(s.slot_name) p
        )
        SELECT orig, formatted
        FROM namepairs
        WHERE orig <> formatted;
      SELECT 'finished';
    ]);
is($result, 'finished', 'check if parsing slot names is successful');

# Check the view mapping slot names to pgactive nodes. We can't really examine
# the slot name in the tests, because it changes every run, so make sure we at
# least find the expected nodes.
# Not required as function already had node names
#$result = $node_0->safe_psql($pgactive_test_dbname,
#  q[SELECT count(1) FROM (
#    SELECT ns.node_name
#      FROM pgactive.pgactive_nodes LEFT JOIN pgactive.pgactive_get_replication_lag_info() ns USING (node_name)
#      ) q
#    WHERE node_name IS NULL;]);
#is($result, 1, 'check if view mapping slot names to pgactive nodes');

# Check to see if we can get the local node name
$result = $node_0->safe_psql($pgactive_test_dbname,
  q[SELECT pgactive.pgactive_get_local_node_name() = 'node_0';]);
is($result, 't', 'check if we can get the local node name');

# Verify that creating/altering/dropping of pgactive node identifier getter
# function is disallowed.
($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    q(CREATE OR REPLACE FUNCTION pgactive._pgactive_node_identifier_getter_private()
      RETURNS numeric AS $$ SELECT '123456'::numeric $$
      LANGUAGE SQL;));
like($stderr, qr/.*ERROR.*creation of pgactive node identifier getter function is not allowed/,
     "creation of pgactive node identifier getter function is not allowed");

($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    q(ALTER FUNCTION pgactive._pgactive_node_identifier_getter_private STABLE;));
like($stderr, qr/.*ERROR.*altering of pgactive node identifier getter function is not allowed/,
     "altering of pgactive node identifier getter function is not allowed");

($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    q(ALTER FUNCTION pgactive._pgactive_node_identifier_getter_private OWNER TO CURRENT_USER;));
like($stderr, qr/.*ERROR.*altering of pgactive node identifier getter function is not allowed/,
     "altering of pgactive node identifier getter function is not allowed");

($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    q(ALTER FUNCTION pgactive._pgactive_node_identifier_getter_private RENAME TO alice;));
like($stderr, qr/.*ERROR.*altering of pgactive node identifier getter function is not allowed/,
     "altering of pgactive node identifier getter function is not allowed");

($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    q(DROP FUNCTION pgactive._pgactive_node_identifier_getter_private();));
like($stderr, qr/.*ERROR.*dropping of pgactive node identifier getter function is not allowed/,
     "dropping of pgactive node identifier getter function is not allowed");

# Test skipping pgactive changes
($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    q[SELECT pgactive.pgactive_skip_changes(n.node_sysid, n.node_timeline, n.node_dboid, '0/0')
        FROM pgactive.pgactive_nodes n
        WHERE (n.node_sysid, n.node_timeline, n.node_dboid) != pgactive.pgactive_get_local_nodeid();]);
like($stderr, qr/.*ERROR.*skipping changes is unsafe and will cause replicas to be out of sync/,
     "check if skipping pgactive changes errors out");

# Let's try to skip the changes anyway
$node_0->append_conf('postgresql.conf', qq(pgactive.skip_ddl_replication = true));
$node_0->restart;

($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    q[SELECT pgactive.pgactive_skip_changes(n.node_sysid, n.node_timeline, n.node_dboid, '0/0')
        FROM pgactive.pgactive_nodes n
        WHERE (n.node_sysid, n.node_timeline, n.node_dboid) != pgactive.pgactive_get_local_nodeid();]);
like($stderr, qr/.*ERROR.*target LSN must be nonzero/,
     "check if skipping pgactive changes with bogus LSN errors out");

# Access a bogus node.
($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    q[SELECT pgactive.pgactive_skip_changes('0', 0, 1234, '0/1');]);
like($stderr, qr/.*ERROR.* replication origin "pgactive_0_0_.* does not exist/,
     "check if skipping pgactive changes with bogus node errors out");

($result, $stdout, $stderr) = $node_0->psql(
    $pgactive_test_dbname,
    q[SELECT pgactive.pgactive_skip_changes(n.node_sysid, n.node_timeline, n.node_dboid, '0/1')
        FROM pgactive.pgactive_nodes n
        WHERE (n.node_sysid, n.node_timeline, n.node_dboid) = pgactive.pgactive_get_local_nodeid();]);
like($stderr, qr/.*ERROR.*passed ID is for the local node, can't skip changes from self/,
     "check if skipping pgactive changes for local node errors out");

# Skipping the past must do nothing. The LSN isn't exposed in
# pg_replication_identifier so this'll just produce no visible result, but not
# break anything.
$node_0->psql($pgactive_test_dbname,
  q[SELECT pgactive.pgactive_skip_changes(n.node_sysid, n.node_timeline, n.node_dboid, '0/1')
    FROM pgactive.pgactive_nodes n
    WHERE (n.node_sysid, n.node_timeline, n.node_dboid) != pgactive.pgactive_get_local_nodeid();]);

$node_0->stop;
$node_1->stop;

# Test data-only logical join of a node
my $node_2 = PostgreSQL::Test::Cluster->new('node_2');
initandstart_pgactive_group($node_2);

$node_2->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE fruits(id integer PRIMARY KEY, name varchar);]);
$node_2->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (1, 'Mango');]);

my $node_3 = PostgreSQL::Test::Cluster->new('node_3');
initandstart_node($node_3);

# Create schema on the joining node first
$node_3->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE fruits(id integer PRIMARY KEY, name varchar);]);

# Now, join the node with data-only option set
my $join_query = generate_pgactive_logical_join_query($node_3, $node_2, data_only_node_init => 'true');
$node_3->safe_psql($pgactive_test_dbname, $join_query);

$node_3->safe_psql($pgactive_test_dbname,
    qq[SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);

check_join_status($node_3, $node_2);

wait_for_apply($node_2, $node_3);

# Check data is available on all pgactive nodes after logical join
$query = qq[SELECT COUNT(*) FROM fruits;];
my $expected = 1;
my $node_2_res = $node_2->safe_psql($pgactive_test_dbname, $query);
my $node_3_res = $node_3->safe_psql($pgactive_test_dbname, $query);

is($node_2_res, $expected, "pgactive node node_2 has all the data");
is($node_3_res, $expected, "pgactive node node_3 has all the data");

$node_2->stop;
$node_3->stop;

done_testing();
