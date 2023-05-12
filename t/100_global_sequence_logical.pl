#!/usr/bin/perl
use strict;
use warnings;
use lib 't/';
use PostgreSQL::Test::Cluster;
use Test::More;
use PostgreSQL::Test::Utils;
require 'common/bdr_global_sequence.pl';

# This executes all the global sequence related tests
# for logical joins
global_sequence_tests('logical');

done_testing();
