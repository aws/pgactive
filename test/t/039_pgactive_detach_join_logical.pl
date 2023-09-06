#!/usr/bin/env perl
#
# Simple detach/join tests for logical (pgactive_join_group) mode
#
use strict;
use warnings;
use lib 'test/t/';
use PostgreSQL::Test::Utils;
use PostgreSQL::Test::Cluster;
use Test::More;
require 'common/pgactive_detach_join.pl';

pgactive_detach_join_tests('logical');
done_testing();
