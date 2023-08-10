#!/usr/bin/env perl
#
# Simple detach/join tests for logical (bdr_join_group) mode
#
use strict;
use warnings;
use lib 'test/t/';
use PostgreSQL::Test::Utils;
use PostgreSQL::Test::Cluster;
use Test::More;
require 'common/bdr_detach_join.pl';

bdr_detach_join_tests('logical');
done_testing();
