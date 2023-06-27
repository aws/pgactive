#!/usr/bin/env perl
#
# This executes all bdr detach and join concurrency 
# related tests for physical joins
#
use strict;
use warnings;
use lib 't/';
use PostgreSQL::Test::Utils;
use PostgreSQL::Test::Cluster;
use Test::More;
require 'common/bdr_detach_join_concurrency.pl';

bdr_detach_join_concurrency_tests('physical');
done_testing();
