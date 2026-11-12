#!/usr/bin/env perl
#
# Test that pgactive_wait_for_node_ready() reports a joining node whose
# initialization from the join_using_dsn node failed, and says which server
# logs have the error, instead of waiting for the node to become ready.
use strict;
use warnings;
use lib 'test/t/';
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use utils::nodemanagement;

# Run pgactive_wait_for_node_ready() with its default timeout of 0, which
# waits for as long as it takes, on a node whose initialization failed. It
# must fail without timing out. Returns its stderr.
sub wait_for_ready_stderr
{
	my ($node) = @_;
	my ($ret, $stdout, $stderr, $timed_out);

	($ret, $stdout, $stderr) = $node->psql(
		$pgactive_test_dbname,
		q[SELECT pgactive.pgactive_wait_for_node_ready()],
		timeout => $PostgreSQL::Test::Utils::timeout_default,
		timed_out => \$timed_out);
	ok(!$timed_out && $ret != 0,
		'pgactive_wait_for_node_ready() fails on ' . $node->name);
	return $stderr;
}

# The last error the per-db worker of the test database recorded.
sub per_db_last_error
{
	my ($node) = @_;

	return $node->safe_psql(
		$pgactive_test_dbname, q[
		SELECT last_error FROM pgactive.pgactive_get_workers_info()
		WHERE worker_type = 'per-db'
		  AND dboid = (SELECT oid FROM pg_database
					   WHERE datname = current_database())]);
}

# The cleanup that the errors ask for names this node's slot on the origin,
# and the replication origin for the origin on this node. Check that both
# exist, and return their names.
sub check_cleanup_hint
{
	my ($stderr, $origin, $joiner) = @_;
	my ($slot, $node_name, $rorigin) = $stderr =~
	  /Then drop replication slot "(\w+)" on node (\w+) and replication origin "(\w+)" on this node, drop and recreate this database, and try again\./;

	ok(defined($slot), 'the hint names a slot and a replication origin');
	$slot //= '';
	$rorigin //= '';
	is($node_name, $origin->name, 'the hint names the origin as the slot\'s node');
	is( $origin->safe_psql(
			$pgactive_test_dbname,
			qq[SELECT count(*) FROM pg_replication_slots WHERE slot_name = '$slot']),
		'1',
		'the slot exists on ' . $origin->name);
	is( $joiner->safe_psql(
			$pgactive_test_dbname,
			qq[SELECT count(*) FROM pg_replication_origin WHERE roname = '$rorigin']),
		'1',
		'the replication origin exists on ' . $joiner->name);
	return ($slot, $rorigin);
}

# A dump failure. An enum value whose pg_enum row is gone makes COPY fail on
# the origin when pgactive_dump copies the table that has it.
my $origin_a = PostgreSQL::Test::Cluster->new('origin_a');
initandstart_pgactive_group($origin_a);
$origin_a->safe_psql(
	$pgactive_test_dbname, q{
	CREATE TYPE public.mood AS ENUM ('ok', 'gone');
	CREATE TABLE public.dump_fail (id integer PRIMARY KEY, m public.mood);
	INSERT INTO public.dump_fail VALUES (1, 'gone');
	DELETE FROM pg_enum
	  WHERE enumtypid = 'public.mood'::regtype AND enumlabel = 'gone';});

my $joiner_a = PostgreSQL::Test::Cluster->new('joiner_a');
initandstart_node($joiner_a);
my $origin_a_logstart = get_log_size($origin_a);
my $joiner_a_logstart = get_log_size($joiner_a);
pgactive_logical_join($joiner_a, $origin_a, nowait => 1);

my $stderr = wait_for_ready_stderr($joiner_a);
like(
	$stderr,
	qr/ERROR:  node joiner_a could not dump database 'pgactive_test' from node origin_a\n.*DETAIL:  pgactive_dump failed\. Initialization of a node is not retried after a failure\.\n.*HINT:  Check the server logs of this node and of node origin_a for the error\. Then drop replication slot /,
	'the dump failure is reported, with both nodes\' server logs');
check_cleanup_hint($stderr, $origin_a, $joiner_a);
ok( find_in_log(
		$origin_a, qr/ERROR:  invalid internal value for enum/,
		$origin_a_logstart),
	'origin_a\'s server log has the error pgactive_dump got');
ok( find_in_log(
		$joiner_a, qr/pgactive_dump: error: .*"dump_fail"/,
		$joiner_a_logstart),
	'joiner_a\'s server log has pgactive_dump\'s own error');

# The per-db worker restarts, finds the earlier attempt failed, and keeps the
# dump failure as its last error.
ok( find_in_log(
		$joiner_a,
		qr/ERROR:  previous init failed, manual cleanup is required/,
		$joiner_a_logstart),
	'joiner_a\'s per-db worker restarted after the dump failure');
is(per_db_last_error($joiner_a),
	'pgactive_init_dump_failure',
	'the restarted per-db worker keeps the dump failure as its last error');

# A restore failure. The joining node has a view with the name of a table on
# the origin, so pg_restore can't create the table.
my $origin_b = PostgreSQL::Test::Cluster->new('origin_b');
initandstart_pgactive_group($origin_b);
$origin_b->safe_psql($pgactive_test_dbname,
	q[CREATE TABLE public.restore_fail (id integer PRIMARY KEY);]);

my $joiner_b = PostgreSQL::Test::Cluster->new('joiner_b');
initandstart_node($joiner_b);
$joiner_b->safe_psql($pgactive_test_dbname,
	q[CREATE VIEW public.restore_fail AS SELECT 1 AS id;]);
my $joiner_b_logstart = get_log_size($joiner_b);
pgactive_logical_join($joiner_b, $origin_b, nowait => 1);

$stderr = wait_for_ready_stderr($joiner_b);
like(
	$stderr,
	qr/ERROR:  node joiner_b could not restore the dump of database 'pgactive_test' from node origin_b\n.*DETAIL:  pg_restore failed\. Initialization of a node is not retried after a failure\.\n.*HINT:  Check the server log of this node for the error\. Then drop replication slot /,
	'the restore failure is reported, with this node\'s server log');
check_cleanup_hint($stderr, $origin_b, $joiner_b);
ok( find_in_log(
		$joiner_b, qr/ERROR:  relation "restore_fail" already exists/,
		$joiner_b_logstart),
	'joiner_b\'s server log has the error pg_restore got');
is(per_db_last_error($joiner_b),
	'pgactive_init_restore_failure',
	'the per-db worker\'s last error is the restore failure');

# A server restart clears the restore failure from shared memory. The
# restarted per-db worker finds that an earlier attempt failed, and records
# that instead.
$joiner_b->restart;

$stderr = wait_for_ready_stderr($joiner_b);
like(
	$stderr,
	qr/ERROR:  node joiner_b could not be initialized from node origin_b\n.*DETAIL:  An earlier initialization attempt failed\. Initialization of a node is not retried after a failure\.\n.*HINT:  Check the server log of this node from that attempt for the error, and the server log of node origin_b if pgactive_dump failed\. Then drop replication slot /,
	'the earlier failure is reported after a server restart');
is(per_db_last_error($joiner_b),
	'pgactive_previous_init_failure',
	'the per-db worker\'s last error is the earlier failure');
my ($slot_b, $rorigin_b) = check_cleanup_hint($stderr, $origin_b, $joiner_b);

# Do the cleanup the hint asks for, and join again.
$origin_b->safe_psql($pgactive_test_dbname,
	qq[SELECT pg_drop_replication_slot('$slot_b')]);
$joiner_b->safe_psql('postgres',
	qq[SELECT pg_replication_origin_drop('$rorigin_b')]);
$joiner_b->safe_psql('postgres', qq[DROP DATABASE $pgactive_test_dbname]);
$joiner_b->safe_psql('postgres', qq[CREATE DATABASE $pgactive_test_dbname]);
$joiner_b->safe_psql($pgactive_test_dbname, q[CREATE EXTENSION pgactive]);
$origin_b->safe_psql($pgactive_test_dbname,
	q[INSERT INTO public.restore_fail VALUES (1);]);
pgactive_logical_join($joiner_b, $origin_b);
is($joiner_b->safe_psql($pgactive_test_dbname, q[SELECT id FROM public.restore_fail]),
	'1', 'joiner_b joins after the cleanup that the hint asks for');

$_->stop for ($origin_a, $joiner_a, $origin_b, $joiner_b);

done_testing();
