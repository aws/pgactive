#!/usr/bin/env perl
#
# Test unregistering per-db/apply worker after detaching.
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

# Create an upstream node and bring up bdr
my $nodes = make_bdr_group(2,'node_');
my ($node_0,$node_1) = @$nodes;

# Detach a node from 2 node cluster
note "Detach node_0 from 2 node cluster\n";
bdr_detach_nodes([$node_0], $node_1);
check_detach_status([$node_0], $node_1);

my $logstart_0 = get_log_size($node_0);
my $logstart_1 = get_log_size($node_1);

# Ensure detached node knows it is actually detached i.e. its node_status in
# bdr.bdr_nodes table is updated as 'k'. This is needed because apply worker on
# a detached node unregisters only upon deteching node_status as 'k'.
my $node_0_name = $node_0->name();
my $query =
	qq[SELECT node_status = 'k' FROM bdr.bdr_nodes WHERE node_name = '$node_0_name';];
$node_0->poll_query_until($bdr_test_dbname, $query)
	or die "timed out waiting for detached node to know it's detached";

# Detached node must unregister apply worker
my $result = find_in_log($node_0,
	qr!LOG: ( [A-Z0-9]+:)? unregistering apply worker due to .*!,
	$logstart_0);
ok($result, "unregistering apply worker on node_0 is detected");

# Remove BDR from the detached node
$node_0->safe_psql($bdr_test_dbname, "select bdr.bdr_remove(true)");

# per-db worker must be unregistered on a node with BDR removed
$result = find_in_log($node_0,
	qr!LOG: ( [A-Z0-9]+:)? unregistering per-db worker due to .*!,
	$logstart_0);
ok($result, "unregistering per-db worker on node_0 is detected");

# Remove BDR from node and immediately drop the extension
$node_1->safe_psql($bdr_test_dbname,
	q[
		SELECT bdr.bdr_remove(true);
		DROP EXTENSION bdr;
	]);

# Detached node must unregister apply worker
$result = find_in_log($node_1,
	qr!LOG: ( [A-Z0-9]+:)? unregistering apply worker due to .*!,
	$logstart_1);
ok($result, "unregistering apply worker on node_1 is detected");

# per-db worker must be unregistered on a node with BDR removed
$result = find_in_log($node_1,
	qr!LOG: ( [A-Z0-9]+:)? unregistering per-db worker due to .*!,
	$logstart_1);
ok($result, "unregistering per-db worker on node_1 is detected");

done_testing();
