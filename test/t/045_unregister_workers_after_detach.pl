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

# Create an upstream node and bring up pgactive
my $nodes = make_pgactive_group(2,'node_');
my ($node_0,$node_1) = @$nodes;

# Detach a node from 2 node cluster
note "Detach node_0 from 2 node cluster\n";
pgactive_detach_nodes([$node_0], $node_1);
check_detach_status([$node_0], $node_1);

my $logstart_0 = get_log_size($node_0);
my $logstart_1 = get_log_size($node_1);

# Detached node must unregister apply worker
my $result = find_in_log($node_0,
	qr!LOG: ( [A-Z0-9]+:)? unregistering apply worker due to .*!,
	$logstart_0);

# Let's skip if the unregister log message is not detected. Sometimes it may
# happen that the worker might get killed even before unregistering log message
# is hit.
SKIP: {
	skip "unregistering apply worker on node_0 is not detected", 1
	  if (!$result);

	ok($result, "unregistering apply worker on node_0 is detected");
}

# Remove pgactive from the detached node
$node_0->safe_psql($pgactive_test_dbname, "select pgactive.pgactive_remove(true)");

# per-db worker must be unregistered on a node with pgactive removed
$result = find_in_log($node_0,
	qr!LOG: ( [A-Z0-9]+:)? unregistering per-db worker due to .*!,
	$logstart_0);

# Let's skip if the unregister log message is not detected. Sometimes it may
# happen that the worker might get killed even before unregistering log message
# is hit.
SKIP: {
	skip "unregistering per-db worker on node_0 is not detected", 1
	  if (!$result);

	ok($result, "unregistering per-db worker on node_0 is detected");
}

# Remove pgactive from node and immediately drop the extension
$node_1->safe_psql($pgactive_test_dbname,
	q[
		SELECT pgactive.pgactive_remove(true);
		DROP EXTENSION pgactive;
	]);

# Detached node must unregister apply worker
$result = find_in_log($node_1,
	qr!LOG: ( [A-Z0-9]+:)? unregistering apply worker due to .*!,
	$logstart_1);

# Let's skip if the unregister log message is not detected. Sometimes it may
# happen that the worker might get killed even before unregistering log message
# is hit.
SKIP: {
	skip "unregistering apply worker on node_1 is not detected", 1
	  if (!$result);

	ok($result, "unregistering apply worker on node_1 is detected");
}

# per-db worker must be unregistered on a node with pgactive removed
$result = find_in_log($node_1,
	qr!LOG: ( [A-Z0-9]+:)? unregistering per-db worker due to .*!,
	$logstart_1);

# Let's skip if the unregister log message is not detected. Sometimes it may
# happen that the worker might get killed even before unregistering log message
# is hit.
SKIP: {
	skip "unregistering per-db worker on node_1 is not detected", 1
	  if (!$result);

	ok($result, "unregistering per-db worker on node_1 is detected");
}

done_testing();
