#!/usr/bin/env perl
#
# This executes all bdr part and join concurrency 
# related tests for logical joins
#
use strict;
use warnings;
use lib 't/';
use PostgreSQL::Test::Utils;
use PostgreSQL::Test::Cluster;
use Test::More;
require 'common/bdr_part_join_concurrency.pl';

bdr_part_join_concurrency_tests('logical');
done_testing();
