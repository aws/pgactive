#!/usr/bin/env perl
#
# This executes all bdr part and join concurrency 
# related tests for physical joins
#
use strict;
use warnings;
use lib 't/';
use TestLib;
use PostgresNode;
use Test::More;
require 'common/bdr_part_join_concurrency.pl';

plan skip_all => "test temporarily disabled due to a sporadic yet-to-be fixed issue found in concurrent_joins_physical()'s bdr_node_join_wait_for_ready()";
bdr_part_join_concurrency_tests('physical');
done_testing();
