#!/usr/bin/env perl
#
# This test tries to ensure that joining a node while another is offline will
# hang until the offline node comes back.
#
use strict;
use warnings;
use lib 'test/t/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use IPC::Run qw(timeout);;
use Test::More;
use utils::nodemanagement qw(
		:DEFAULT
		generate_bdr_logical_join_query
		copy_transform_postgresqlconf
		start_bdr_init_copy
		);

my $timedout = 0;

# Create an upstream node and bring up bdr
my $nodes = make_bdr_group(3,'node_');
my ($node_0,$offline_node,$node_2) = @$nodes;
my $offline_index = 1;
my @online_nodes = ($node_0, $node_2);

exec_ddl($node_0, q[CREATE TABLE public.testinsert(x integer primary key);]);
wait_for_apply($node_0, $offline_node);
wait_for_apply($node_0, $node_2);

# bring down a node and try to do a node join of a new node.
$offline_node->stop;

# TODO: do some work on the offline node, verify that it replicates to the
# joining node. We'll need a better way to bring it offline than shutting it
# down, especially since we must suppress outbound connections as well as
# inbound.

my $new_node_name = 'new_logical_join_node';
my $new_node = PostgreSQL::Test::Cluster->new($new_node_name);
initandstart_node($new_node);
my $join_query = generate_bdr_logical_join_query($new_node, $node_0);
# The join query will always complete immediately, we need to look at the
# progress of join completion to determine whether it gets anywhere.
$new_node->safe_psql($bdr_test_dbname, $join_query);

# TODO: do some more work on offline node here

# We should never become ready since we'll be stuck at catchup
$new_node->psql($bdr_test_dbname,
	qq[SELECT bdr.bdr_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)],
	timed_out => \$timedout, timeout => 10);
is($timedout, 1, 'Logical node join timed out while node down');

# Node join will proceed until it gets to catchup mode, where it can't create
# slots on its peers. At this point it'll get stuck until the peer comes up.
is($new_node->safe_psql($bdr_test_dbname, "SELECT node_status FROM bdr.bdr_nodes WHERE node_name = '$new_node_name'"), 'c',
    "bdr.bdr_nodes still in status 'c'");

# If we bring the offline node back online, join should be able to proceed
$offline_node->start;
is($new_node->psql($bdr_test_dbname,
	qq[SELECT bdr.bdr_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]), 0,
    'join succeeded once offline node came back');
foreach my $node (@{$nodes}) {
    check_join_status($new_node, $node);
}
push @$nodes, $new_node;
push @online_nodes, $new_node;

# Everything is happy?
$new_node->safe_psql($bdr_test_dbname, q[SELECT bdr.bdr_acquire_global_lock('ddl_lock')]);

# TODO: check offline work is sync'd



# A physical join should run bdr_init_copy fine when a node is offline, then get
# stuck in catchup mode, just like logical join.
$offline_node->stop;

# TODO: do some work on offline node here

my $new_physical_join_node = PostgreSQL::Test::Cluster->new('new_physical_join_node');
my $new_conf_file = copy_transform_postgresqlconf( $new_physical_join_node, $node_0 );
my $timeout = IPC::Run::timeout(my $to=10, exception=>"Timed out");
my $handle = start_bdr_init_copy($new_physical_join_node, $node_0, $new_conf_file,[$timeout]);
ok($handle->finish, 'bdr_init_copy finished without error');

$timedout = 0;
$new_physical_join_node->psql($bdr_test_dbname,
	qq[SELECT bdr.bdr_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)],
	timed_out => \$timedout, timeout => 10);
is($timedout, 1, 'Physical node join timed out while node down');

# PostgresNode doesn't know we started the node since we didn't
# use any of its methods, so we'd better tell it to check. Otherwise
# it'll ignore the node for things like pg_ctl stop.
$new_physical_join_node->_update_pid(1);

is($new_physical_join_node->safe_psql($bdr_test_dbname, "SELECT node_status FROM bdr.bdr_nodes WHERE node_name = '" . $new_physical_join_node->name . "'"), 'c',
    "bdr.bdr_nodes still in status 'c'");

# TODO: do more work on offline node here

# If we bring up the new node, join should proceed?
$offline_node->start;

is($new_physical_join_node->psql($bdr_test_dbname,
	qq[SELECT bdr.bdr_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]), 0,
    'physical join succeeded once offline node came back');

foreach my $node (@{$nodes}) {
    check_join_status($new_physical_join_node, $node);
}

# TODO: check offline work is sync'd

done_testing();
