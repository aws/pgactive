#!/usr/bin/env perl
#
# Test if a streaming standby to a BDR node gets BDR node identifier getter
# function from the BDR node.
use strict;
use warnings;
use lib 'test/t/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use IPC::Run;
use Test::More;
use utils::nodemanagement;

# Create an upstream node and bring up bdr
my $nodes = make_bdr_group(2,'node_');
my ($node_0,$node_1) = @$nodes;

# Take backup
my $backup_name = 'my_backup';
$node_0->backup($backup_name);

# Create streaming standby linking to node_0
my $node_0_standby = PostgreSQL::Test::Cluster->new('node_0_standby');
$node_0_standby->init_from_backup($node_0, $backup_name, has_streaming => 1);
$node_0_standby->start;

# Wait for standby catchup
$node_0->wait_for_catchup($node_0_standby);

my $query = qq[SELECT * FROM bdr._bdr_node_identifier_getter_private();];

my $node_0_res = $node_0->safe_psql($bdr_test_dbname, $query);
my $node_0_standby_res = $node_0_standby->safe_psql($bdr_test_dbname, $query);

is($node_0_res, $node_0_standby_res,
   "BDR node identifier getter function is available on standby");

done_testing();
