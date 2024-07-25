#!/usr/bin/env perl
#
# This test creates a 4-node group with two mutual sync-rep pairs.
# 
#    A <===> B
#    ^\     /^
#    | \   / |
#    |   x   |
#    | /   \ |
#    v/     \v
#    C <===> D
#
# then upgrades it to 2-safe using Pg 9.6 features to do A <==> C
# and B <==> D too.
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


#-------------------------------------
# Setup and worker names
#-------------------------------------

my $node_a = PostgreSQL::Test::Cluster->new('node_a');
initandstart_pgactive_group($node_a);

my $node_b = PostgreSQL::Test::Cluster->new('node_b');
initandstart_logicaljoin_node($node_b, $node_a);

my $anid = $node_a->safe_psql($pgactive_test_dbname,
              qq[SELECT pgactive.pgactive_get_node_identifier();]);
my $bnid = $node_b->safe_psql($pgactive_test_dbname,
              qq[SELECT pgactive.pgactive_get_node_identifier();]);
my $aperdb = "pgactive:" . $anid . ":perdb";
my $bperdb = "pgactive:" . $bnid . ":perdb";
my $aapply = "pgactive:" . $anid . ":apply";
my $bapply = "pgactive:" . $bnid . ":apply";
my $asend = "pgactive:" . $anid . ":send";
my $bsend = "pgactive:" . $bnid . ":send";

# application_name should be the same as the node name
is($node_a->safe_psql('postgres', qq[SELECT count(*) >= 4 FROM pg_stat_activity WHERE application_name IN ('pgactive:supervisor', '$aperdb', '$bapply', '$bsend')]),
q[t],
'2-node application_name check');

# Create the other nodes
my $node_c = PostgreSQL::Test::Cluster->new('node_c');
initandstart_logicaljoin_node($node_c, $node_a);

my $node_d = PostgreSQL::Test::Cluster->new('node_d');
initandstart_logicaljoin_node($node_d, $node_c);

my $cnid = $node_c->safe_psql($pgactive_test_dbname,
              qq[SELECT pgactive.pgactive_get_node_identifier();]);
my $dnid = $node_d->safe_psql($pgactive_test_dbname,
              qq[SELECT pgactive.pgactive_get_node_identifier();]);
my $capply = "pgactive:" . $cnid . ":apply";
my $dapply = "pgactive:" . $dnid . ":apply";
my $csend = "pgactive:" . $cnid . ":send";
my $dsend = "pgactive:" . $dnid . ":send";

# other apply workers should be visible now
is($node_a->safe_psql('postgres', qq[SELECT count(*) >= 8 FROM pg_stat_activity WHERE application_name IN ('pgactive:supervisor', '$aperdb', '$bapply', '$bsend', '$capply', '$csend', '$dapply', '$dsend')]),
q[t],
'4-node application_name check');

#-------------------------------------
# no sync rep
#-------------------------------------

# Everything working?
exec_ddl($node_a, q[CREATE TABLE public.t(x text);]);

# Make sure everything caught up by forcing another lock
$node_a->safe_psql($pgactive_test_dbname, q[SELECT pgactive.pgactive_acquire_global_lock('write_lock')]);

my @nodes = ($node_a, $node_b, $node_c, $node_d);
for my $node (@nodes) {
  $node->safe_psql($pgactive_test_dbname, q[INSERT INTO t(x) VALUES (pgactive.pgactive_get_local_node_name())]);
}
$node_a->safe_psql($pgactive_test_dbname, q[SELECT pgactive.pgactive_acquire_global_lock('write_lock')]);

is($node_a->psql($pgactive_test_dbname, q[INSERT INTO t(x) VALUES ('0-0 B2')]), 0, 'A: async B1up');

# With a node down we should still be able to do work
$node_b->stop;
is($node_a->psql($pgactive_test_dbname, q[INSERT INTO t(x) VALUES ('0-0 B1')]), 0, 'A: async B1down');
$node_b->start;

#-------------------------------------
# Reconfigure to 1-safe 1-sync
#-------------------------------------

note "reconfiguring into synchronous pairs A<=>B, C<=>D (1-safe 1-sync)";
$node_a->safe_psql($pgactive_test_dbname, qq[ALTER SYSTEM SET synchronous_standby_names = '"$bsend"']);
$node_b->safe_psql($pgactive_test_dbname, qq[ALTER SYSTEM SET synchronous_standby_names = '"$asend"']);

$node_c->safe_psql($pgactive_test_dbname, qq[ALTER SYSTEM SET synchronous_standby_names = '"$dsend"']);
$node_d->safe_psql($pgactive_test_dbname, qq[ALTER SYSTEM SET synchronous_standby_names = '"$csend"']);

for my $node (@nodes) {
  $node->safe_psql($pgactive_test_dbname, q[ALTER SYSTEM SET pgactive.synchronous_commit = on]);
  $node->restart;
}

# Now we have to wait for the nodes to actually join...
for my $node (@nodes) {
    $node->safe_psql($pgactive_test_dbname,
      qq[SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
}

# Everything should work while the system is all-up
is($node_a->psql($pgactive_test_dbname, q[INSERT INTO t(x) VALUES ('A: 1-1 B2')]), 0, 'A: 1-safe 1-sync B1up');

# but with node B down, node A should refuse to confirm commit
note "stopping B";
$node_b->stop;
my $timed_out;
note "inserting on A when B is down; expect psql timeout in 10s";
$node_a->psql($pgactive_test_dbname, q[INSERT INTO t(x) VALUES ('A: 1-1 B1')], timeout => 10, timed_out => \$timed_out);
ok($timed_out, 'A: 1-safe 1-sync B1down times out');

is($node_a->safe_psql($pgactive_test_dbname, q[SELECT 1 FROM t WHERE x = 'A: 1-1 B1']), '', 'committed xact not visible on A yet');

is($node_c->safe_psql($pgactive_test_dbname, q[SELECT 1 FROM t WHERE x = 'A: 1-1 B1']), '1', 'committed xact visible on C');

# but commiting on C should become immediately visible on both A and D when B is down
is($node_c->psql($pgactive_test_dbname, q[INSERT INTO t(x) VALUES ('C: 1-1 B1')]), 0, 'C: 1-safe 1-sync B1down');

# Make sure node_a and node_d are fully caught up with node_c changes
wait_for_apply($node_c, $node_a);
wait_for_apply($node_c, $node_d);

is($node_c->safe_psql($pgactive_test_dbname, q[SELECT 1 FROM t WHERE x = 'C: 1-1 B1']), '1', 'C xact visible on C');
is($node_a->safe_psql($pgactive_test_dbname, q[SELECT 1 FROM t WHERE x = 'C: 1-1 B1']), '1', 'C xact visible on A');
is($node_d->safe_psql($pgactive_test_dbname, q[SELECT 1 FROM t WHERE x = 'C: 1-1 B1']), '1', 'C xact visible on D');

note "starting B";
$node_b->start;

# Because Pg commits a txn in sync rep before checking sync, once B comes back up
# and catches up, we should see the txn from when it was down.
#
# Make sure node_b is fully caught up with node_a changes after restart
wait_for_apply($node_a, $node_b);

is($node_b->safe_psql($pgactive_test_dbname, q[SELECT 1 FROM t WHERE x = 'A: 1-1 B1']), '1', 'B received xact from A');
is($node_a->safe_psql($pgactive_test_dbname, q[SELECT 1 FROM t WHERE x = 'A: 1-1 B1']), '1', 'committed xact visible on A after B confirms');

#-------------------------------------
# Reconfigure to 2-safe 2-sync
#-------------------------------------

note "reconfiguring into 2-safe 2-sync A[B,C], B[A,D] C[A,D] D[B,C]";
$node_a->safe_psql($pgactive_test_dbname, qq[ALTER SYSTEM SET synchronous_standby_names = '2 ("$bsend", "$csend")']);
$node_b->safe_psql($pgactive_test_dbname, qq[ALTER SYSTEM SET synchronous_standby_names = '2 ("$asend", "$dsend")']);

$node_c->safe_psql($pgactive_test_dbname, qq[ALTER SYSTEM SET synchronous_standby_names = '2 ("$dsend", "$asend")']);
$node_d->safe_psql($pgactive_test_dbname, qq[ALTER SYSTEM SET synchronous_standby_names = '2 ("$csend", "$bsend")']);

for my $node (@nodes) {
  $node->restart;
}

# Now we have to wait for the nodes to actually join...
for my $node (@nodes) {
    $node->safe_psql($pgactive_test_dbname,
      qq[SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
}

# Everything should work while the system is all-up
is($node_a->psql($pgactive_test_dbname, q[INSERT INTO t(x) VALUES ('A: 2-2 B2 C2')]), 0, 'A: 2-safe 2-sync B up C up');

# but with node B down, node A should refuse to confirm commit
note "stopping B";
$node_b->stop;
note "inserting on A when B is down; expect psql timeout in 10s";
$node_a->psql($pgactive_test_dbname, q[INSERT INTO t(x) VALUES ('A: 2-2 B1 C2')], timeout => 10, timed_out => \$timed_out);
ok($timed_out, '2-safe 2-sync on A times out if B is down');
note "starting B";
$node_b->start;

# same with node-C since we're 2-safe
note "stopping C";
$node_c->stop;
note "inserting on A when C is down; expect psql timeout in 10s";
$node_a->psql($pgactive_test_dbname, q[INSERT INTO t(x) VALUES ('A: 2-2 B2 C1')], timeout => 10, timed_out => \$timed_out);
ok($timed_out, '2-safe 2-sync on A times out if C is down');
note "starting C";
$node_c->start;


#-------------------------------------
# Reconfigure to 2-safe 2-sync
#-------------------------------------
#
note "reconfiguring into 1-safe 2-sync A[B,C], B[A,D] C[A,D] D[B,C]";

$node_a->safe_psql($pgactive_test_dbname, qq[ALTER SYSTEM SET synchronous_standby_names = '1 ("$bsend", "$csend")']);
$node_b->safe_psql($pgactive_test_dbname, qq[ALTER SYSTEM SET synchronous_standby_names = '1 ("$asend", "$dsend")']);

$node_c->safe_psql($pgactive_test_dbname, qq[ALTER SYSTEM SET synchronous_standby_names = '1 ("$dsend", "$asend")']);
$node_d->safe_psql($pgactive_test_dbname, qq[ALTER SYSTEM SET synchronous_standby_names = '1 ("$csend", "$bsend")']);

for my $node (@nodes) {
  $node->restart;
}

# Now we have to wait for the nodes to actually join...
for my $node (@nodes) {
    $node->safe_psql($pgactive_test_dbname,
      qq[SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
}

# Everything should work while the system is all-up
is($node_a->psql($pgactive_test_dbname, q[INSERT INTO t(x) VALUES ('A: 2-1 B2 C2')]), 0, '2-sync 1-safe B up C up');

# or when one, but not both, nodes are down
note "stopping B";
$node_b->stop;
is($node_a->psql($pgactive_test_dbname, q[INSERT INTO t(x) VALUES ('A: 2-1 B1 C2')]), 0, '2-sync 1-safe B down C up');

note "stopping C";
$node_c->stop;
$node_a->psql($pgactive_test_dbname, q[INSERT INTO t(x) VALUES ('nA: 2-1 B1 C1')], timeout => 10, timed_out => \$timed_out);
ok($timed_out, '2-sync 1-safe B down C down times out');

note "starting B";
$node_b->start;

is($node_a->psql($pgactive_test_dbname, q[INSERT INTO t(x) VALUES ('A: 2-1 B2 C1')]), 0,'2-sync 1-safe B up C down');

note "starting C";
$node_c->start;

is($node_a->psql($pgactive_test_dbname, q[INSERT INTO t(x) VALUES ('A: 2-1 B2 C2 2')]), 0, '2-sync 1-safe B up C up after');

#-------------------------------------
# Consistent?
#-------------------------------------

note "taking final DDL lock";
$node_a->safe_psql($pgactive_test_dbname, q[SELECT pgactive.pgactive_acquire_global_lock('write_lock')]);
note "done, checking final state";

my $expected = q[node_a|0-0 B1
node_a|0-0 B2
node_a|A: 1-1 B1
node_a|A: 1-1 B2
node_a|A: 2-1 B1 C2
node_a|A: 2-1 B2 C1
node_a|A: 2-1 B2 C2
node_a|A: 2-1 B2 C2 2
node_a|A: 2-2 B1 C2
node_a|A: 2-2 B2 C1
node_a|A: 2-2 B2 C2
node_c|C: 1-1 B1
node_a|nA: 2-1 B1 C1
node_a|node_a
node_b|node_b
node_c|node_c
node_d|node_d];

my $query = q[
select coalesce(node_name, pgactive.pgactive_get_local_node_name()) AS origin_node_name, x
from t
cross join lateral pgactive.pgactive_xact_replication_origin(xmin) ro(originid)
left join pg_replication_origin on (roident = originid)
cross join lateral pgactive.pgactive_parse_replident_name(roname)
left join pgactive.pgactive_nodes on (remote_sysid, remote_timeline, remote_dboid) = (node_sysid, node_timeline, node_dboid)
order by x;
];

is($node_a->safe_psql($pgactive_test_dbname, $query), $expected, 'final results node A');
is($node_b->safe_psql($pgactive_test_dbname, $query), $expected, 'final results node B');
is($node_c->safe_psql($pgactive_test_dbname, $query), $expected, 'final results node C');
is($node_d->safe_psql($pgactive_test_dbname, $query), $expected, 'final results node D');

done_testing();
