#!/usr/bin/env perl
#
# Simple part/join tests for physical (bdr_init_copy) mode
#
use strict;
use warnings;
use lib "t/";
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
require 'common/bdr_part_join.pl';

bdr_part_join_tests('physical');
done_testing();
