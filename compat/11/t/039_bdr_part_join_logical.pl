#!/usr/bin/env perl
#
# Simple part/join tests for logical (bdr_group_join) mode
#
use strict;
use warnings;
use lib 'compat/11/t/';
use TestLib;
use PostgresNode;
use Test::More;
require 'common/bdr_part_join.pl';

bdr_part_join_tests('logical');
done_testing();
