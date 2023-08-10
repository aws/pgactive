#!/usr/bin/env perl
#
# Simple detach/join tests for physical (bdr_init_copy) mode
#
use strict;
use warnings;
use lib 'test/t/';
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
require 'common/bdr_detach_join.pl';

bdr_detach_join_tests('physical');
done_testing();
