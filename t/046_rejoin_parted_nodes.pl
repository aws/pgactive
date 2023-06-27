#!/usr/bin/env perl
#
# Test unregistering per-db/apply worker after parting.
use strict;
use warnings;
use lib 't/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Time::HiRes qw(usleep);
use IPC::Run;
use Test::More;
use utils::nodemanagement;

# Create an upstream node and bring up bdr
my $nodes = make_bdr_group(3,'node_');
my ($node_0,$node_1,$node_2) = @$nodes;

# Part a node from 3 node cluster
note "Part node_0 from 3 node cluster\n";
part_nodes([$node_0], $node_1);
check_part_statuses([$node_0], $node_1);

# Remove BDR from the parted node
$node_0->safe_psql($bdr_test_dbname, "select bdr.remove_bdr_from_local_node()");

#
# Use case 1: a parted node without relations that already exist on the other 
# nodes is able to rejoin.
# Such relation(s) (if any) are not replicated during the re-join.
#

# create a table on the parted and removed node
$node_0->safe_psql($bdr_test_dbname, "create table db_not_empty(a int primary key)");

# re-join the parted node
bdr_logical_join($node_0, $node_1);
check_join_status($node_0, $node_1);

# Must not use safe_psql since we expect an error here
my ($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');
($psql_ret, $psql_stdout, $psql_stderr) = $node_1->psql(
    $bdr_test_dbname,
    "SELECT count(*) from db_not_empty;");
like($psql_stderr, qr/relation "db_not_empty" does not exist/, "db_not_empty not replicated during re-join");

#
# Use case 2: a parted node with relations that already exist on the other 
# nodes is failing to rejoin.
#

# create a table on node_0 (now that it re-joined)
exec_ddl($node_0, q[CREATE TABLE public.test_dup(a int primary key);]);

# Make sure everything caught up by forcing another lock
$node_0->safe_psql($bdr_test_dbname, q[SELECT bdr.acquire_global_lock('write_lock')]);

# Part node0 from 3 node cluster
note "Part node_0 from 3 node cluster\n";
part_nodes([$node_0], $node_1);
check_part_statuses([$node_0], $node_1);

# Remove BDR from the parted node
$node_0->safe_psql($bdr_test_dbname, "select bdr.remove_bdr_from_local_node()");

# re-join the parted node
my $logstart_0 = get_log_size($node_0);
bdr_logical_join($node_0, $node_1, nowait => 1);

# re-join should complain about test_dup already exists
my $result = wait_for_re_join_to_fail($node_0,
    qr!.*pg_restore:.*relation "test_dup" already exists!,
    $logstart_0);

ok($result, "re-join node_0 is failing");

done_testing();

# Return the size of logfile of $node in bytes
sub get_log_size
{
	my ($node) = @_;

	return (stat $node->logfile)[7];
}

# Find $pat in logfile of $node after $off-th byte
sub wait_for_re_join_to_fail
{
	my ($node, $pat, $off) = @_;
	my $max_attempts = $PostgreSQL::Test::Utils::timeout_default * 10;
	my $log;

	while ($max_attempts-- >= 0)
	{
		$log = PostgreSQL::Test::Utils::slurp_file($node->logfile, $off);
		last if ($log =~ m/$pat/);
		usleep(100_000);
	}

	return $log =~ m/$pat/;
}
