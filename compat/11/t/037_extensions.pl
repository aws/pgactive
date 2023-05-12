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
use IPC::Run;
use Test::More;
use utils::nodemanagement;

my $node_a = get_new_node('node_a');
initandstart_bdr_group($node_a);

# This extension does some inserts, which we must permit even when pg_restore
# runs in an otherwise read-only downstream node when we join node b.
exec_ddl($node_a, 'CREATE EXTENSION bdr_test_dummy_extension;');

is($node_a->safe_psql($bdr_test_dbname, q[SELECT 1 FROM pg_catalog.pg_extension WHERE extname = 'bdr_test_dummy_extension']),
    '1', 'bdr_test_dummy_extension got created on upstream');

my $node_b = get_new_node('node_b');
initandstart_logicaljoin_node($node_b, $node_a);

is($node_b->safe_psql($bdr_test_dbname, q[SELECT 1 FROM pg_catalog.pg_extension WHERE extname = 'bdr_test_dummy_extension']),
    '1', 'bdr_test_dummy_extension got restored on downstream');

done_testing();
