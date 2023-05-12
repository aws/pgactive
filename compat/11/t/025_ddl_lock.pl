#!/usr/bin/env perl
#
# Test ddl locking handling of crash/restart, etc.
#
use strict;
use warnings;
use lib 'compat/11/t/';
use Cwd;
use Config;
use PostgresNode;
use TestLib;
use IPC::Run qw(timeout);;
use Test::More;
use utils::nodemanagement;

# Create an upstream node and bring up bdr
my $nodes = make_bdr_group(3,'node_');
my ($node_0, $node_1, $node_2) = @$nodes;

for my $node (@$nodes) {
    $node->append_conf('postgresql.conf', q[
    bdr.bdr_ddl_lock_timeout = '1s'
    ]);
    $node->restart;
}

# Now we have to wait for the nodes to actually join...
for my $node (@$nodes) {
    $node->safe_psql($bdr_test_dbname,
        qq[SELECT bdr.bdr_node_join_wait_for_ready($TestLib::timeout_default)]);
}

# Make sure DDL locking works
my $timedout = 0;
my $ret = $node_0->psql($bdr_test_dbname,
	q[SELECT bdr.acquire_global_lock('ddl_lock');],
	timed_out => \$timedout, timeout => 10);
is($ret, 0, 'DDL lock succeeded with node up');
is($timedout, 0, 'DDL lock acquisition did not time out with node up');

$node_0->safe_psql($bdr_test_dbname, q[
SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE TABLE public.write_me(x integer primary key);
$DDL$)]);

#--------------------------------------------
# Transactions on lock-holding node are read-only
#--------------------------------------------
#
# Per 2ndQuadrant/bdr-private#78, acquisition of the global DDL lock by a node
# forces its local transactions to read-only as well as those of its peers.
#
my $timer = IPC::Run::timeout($TestLib::timeout_default);
my $handle = start_acquire_ddl_lock($node_0, 'write_lock', $timer);
wait_acquire_ddl_lock($handle);

print "attempting insert 0\n";
my ($stdout,$stderr);
($ret,$stdout,$stderr) = $node_0->psql($bdr_test_dbname, 'INSERT INTO write_me(x) VALUES (42)');
is($ret, 0, 'write succeds on lock holder node_0');
is($stderr, '', 'no stderr after write on lock holder');
print "attempting insert 1\n";
($ret,$stdout,$stderr) = $node_1->psql($bdr_test_dbname, 'INSERT INTO write_me(x) VALUES (42)');
is($ret, 3, 'write failed on peer node_1');
like($stderr, qr/canceling statement due to global lock timeout/, 'write on peer failed with global lock timeout');

print "done inserts, releasing ddl lock\n";
release_ddl_lock($handle);

#--------------------------------------------
# DDL lock acquire stalls while node offline
#--------------------------------------------

my @online_nodes = ($node_0, $node_2);
my $offline_index = 1;
my $offline_node = $nodes->[$offline_index];

# Bring a node down
$offline_node->stop;
 
my $lock = start_acquire_ddl_lock($node_0, 'ddl_lock', $timer);
# Not much way around waiting here, since we're trying to show we'll
# time out...
sleep(2);
# We'll always acquire the local ddl lock on peers, it's just the global lock
# we don't acquire. (The local ddl lock is also held on the node that takes the
# global ddl lock, but it's inserted in a row that's in an uncommitted xact so
# we can't see it from queries; see 2ndQuadrant/bdr-private#60)
is($node_2->safe_psql($bdr_test_dbname, q[SELECT state FROM bdr.bdr_global_locks]), 'acquired',
    'local DDL lock acquired on node 2');
# No good way to show if requesting node has replies
# from all peers. Best we can do is see if bdr.bdr_acquire_global_lock(...)
# stmt has finished.
is($node_0->safe_psql($bdr_test_dbname, "SELECT state FROM pg_stat_activity WHERE pid = " . $lock->{backend_pid} . ";"),
    'active', 'still trying to acquire lock on node_0');

cancel_ddl_lock($lock);
ok(!wait_acquire_ddl_lock($lock, undef, 1), 'did not acquire lock');

# After the psql session terminates, we'll send a message to peer nodes
# to release the DDL lock acquisition attempt. This will take a while, so
# poll here.
$node_2->poll_query_until($bdr_test_dbname, "SELECT NOT EXISTS (SELECT 1 FROM bdr.bdr_global_locks WHERE state = 'acquired')");
wait_for_apply($node_0, $node_2);
wait_for_apply($node_2, $node_0);

#--------------------------------
# DDL lock holder goes offline
#--------------------------------
#
# TODO write, see 2ndQuadrant/bdr-private#61

done_testing();
