#!/usr/bin/env perl
#
# This executes all bdr part and join concurrency 
# related tests for physical joins
#
use strict;
use warnings;
use lib 'compat/11/t/';
use TestLib;
use PostgresNode;
use Test::More;
require 'common/bdr_part_join_concurrency.pl';

bdr_part_join_concurrency_tests('physical');
done_testing();
