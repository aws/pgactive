#!/usr/bin/env perl
#
# Test co-existence of multiple pgactive groups on a single postgres cluster
use strict;
use warnings;
use lib 'test/t/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Time::HiRes qw(usleep);
use IPC::Run;
use Test::More;
use utils::nodemanagement;

# Create pgactive group 1 on postgres cluster 1 with database alpha
my $alpha = 'alpha';
my $node_g1_c1 = create_pgactive_group_with_db('node_g1_c1', $alpha);

# Join pgactive group 1 on postgres cluster 2 with database alpha
my $node_g1_c2 = join_pgactive_group_with_db('node_g1_c2', $node_g1_c1, $alpha);

create_and_check_data($node_g1_c1, $node_g1_c2, $alpha);

# Create pgactive group 2 on postgres cluster 1 with database bravo
my $bravo = 'bravo';
my $node_g2_c1 = create_pgactive_group_with_db('node_g2_c1', $bravo);

# Join pgactive group 2 on postgres cluster 2 with database bravo
my $node_g2_c2 = join_pgactive_group_with_db('node_g2_c2', $node_g2_c1, $bravo);

create_and_check_data($node_g2_c1, $node_g2_c2, $bravo);

# Try joining database bravo which is already a part of group 2 on postgres
# cluster 2 to group 1 on postgres cluster 1. This must fail.
my $port = $node_g2_c2->port;
my $host = $node_g2_c2->host;
my $node_g2_c2_connstr = "port=$port host=$host dbname=$bravo";

$port = $node_g1_c1->port;
$host = $node_g1_c1->host;
my $node_g1_c1_connstr = "port=$port host=$host dbname=$alpha";
my $logstart = get_log_size($node_g2_c2);

# Ensure database is empty before joining pgactive group
$node_g2_c2->safe_psql($bravo, q[DROP TABLE fruits;]);

$node_g2_c2->safe_psql($bravo, qq{
    SELECT pgactive.pgactive_join_group(
        local_node_name := 'node_g2_g1_c2',
        node_external_dsn := '$node_g2_c2_connstr',
        join_using_dsn := '$node_g1_c1_connstr');});

my $result = find_in_log($node_g2_c2,
	qr!ERROR: ( [A-Z0-9]+:)? replication slot .* already exists!,
	$logstart);
ok($result, "a database part of a pgactive group joining another pgactive group failure is detected");

done_testing();
