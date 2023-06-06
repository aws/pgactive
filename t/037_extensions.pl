#!/usr/bin/env perl
#
# Test ddl locking handling of crash/restart, etc.
#
use strict;
use warnings;
use lib 't/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use IPC::Run;
use Test::More;
use utils::nodemanagement;

my $node_a = PostgreSQL::Test::Cluster->new('node_a');
initandstart_bdr_group($node_a);

# This extension does some inserts, which we must permit even when pg_restore
# runs in an otherwise read-only downstream node when we join node b.
exec_ddl($node_a, 'CREATE EXTENSION bdr_toy;');

is($node_a->safe_psql($bdr_test_dbname, q[SELECT 1 FROM pg_catalog.pg_extension WHERE extname = 'bdr_toy']),
    '1', 'bdr_toy got created on upstream');

my $node_b = PostgreSQL::Test::Cluster->new('node_b');
initandstart_logicaljoin_node($node_b, $node_a);

is($node_b->safe_psql($bdr_test_dbname, q[SELECT 1 FROM pg_catalog.pg_extension WHERE extname = 'bdr_toy']),
    '1', 'bdr_toy got restored on downstream');

done_testing();
