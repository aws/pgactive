#!/usr/bin/env perl
#
# This executes all pgactive detach and join concurrency 
# related tests for physical joins
#
use strict;
use warnings;
use lib 'test/t/';
use PostgreSQL::Test::Utils;
use PostgreSQL::Test::Cluster;
use Test::More;
require 'common/pgactive_detach_join_concurrency.pl';

pgactive_detach_join_concurrency_tests('physical');
done_testing();
