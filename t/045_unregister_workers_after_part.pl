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

my $logstart_0 = get_log_size($node_0);
my $logstart_1 = get_log_size($node_1);

# Parted node must unregister apply worker
my $result = wait_for_worker_to_unregister($node_0,
	qr!LOG: ( [A-Z0-9]+:)? unregistering apply worker due to .*!,
	$logstart_0);
ok($result, "unregistering apply worker on node_0 is detected");

# Remove BDR from the parted node
$node_0->safe_psql($bdr_test_dbname, "select bdr.bdr_remove(true)");

# per-db worker must be unregistered on a node with BDR removed
$result = wait_for_worker_to_unregister($node_0,
	qr!LOG: ( [A-Z0-9]+:)? unregistering per-db worker due to .*!,
	$logstart_0);
ok($result, "unregistering per-db worker on node_0 is detected");

# Remove BDR from node and immediately drop the extension
$node_1->safe_psql($bdr_test_dbname,
	q[
		SELECT bdr.bdr_remove(true);
		DROP EXTENSION bdr;
	]);

# Parted node must unregister apply worker
$result = wait_for_worker_to_unregister($node_1,
	qr!LOG: ( [A-Z0-9]+:)? unregistering apply worker due to .*!,
	$logstart_1);
ok($result, "unregistering apply worker on node_1 is detected");

# per-db worker must be unregistered on a node with BDR removed
$result = wait_for_worker_to_unregister($node_1,
	qr!LOG: ( [A-Z0-9]+:)? unregistering per-db worker due to .*!,
	$logstart_1);
ok($result, "unregistering per-db worker on node_1 is detected");

done_testing();

# Return the size of logfile of $node in bytes
sub get_log_size
{
	my ($node) = @_;

	return (stat $node->logfile)[7];
}

# Find $pat in logfile of $node after $off-th byte
sub wait_for_worker_to_unregister
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
