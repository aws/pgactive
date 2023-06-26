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
my $nodes = make_bdr_group(2,'node_');
my ($node_0,$node_1) = @$nodes;

# Part a node from 2 node cluster
note "Part node_0 from 2 node cluster\n";
part_nodes([$node_0], $node_1);
check_part_statuses([$node_0], $node_1);

# Remove BDR from the parted node
$node_0->safe_psql($bdr_test_dbname, "select bdr.remove_bdr_from_local_node()");

# re-join the parted node
bdr_logical_join($node_0, $node_1);
check_join_status($node_0, $node_1);

done_testing();
