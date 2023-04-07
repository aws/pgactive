#!/usr/bin/perl
use strict;
use warnings;
use lib 't/';
use PostgresNode;
use Test::More;
use TestLib;
require 'common/bdr_global_sequence.pl';

# This executes all the global sequence related tests
# for logical joins
global_sequence_tests('logical');

done_testing();
