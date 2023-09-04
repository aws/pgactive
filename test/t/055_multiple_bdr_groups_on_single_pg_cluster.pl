#!/usr/bin/env perl
#
# Test co-existence of multiple BDR groups on a single postgres cluster
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

# Create BDR group 1 on postgres cluster 1 with database alpha
my $alpha = 'alpha';
my $node_g1_c1 = create_bdr_group_with_db('node_g1_c1', $alpha);

# Join BDR group 1 on postgres cluster 2 with database alpha
my $node_g1_c2 = join_bdr_group_with_db('node_g1_c2', $node_g1_c1, $alpha);

create_and_check_data($node_g1_c1, $node_g1_c2, $alpha);

# Create BDR group 2 on postgres cluster 1 with database bravo
my $bravo = 'bravo';
my $node_g2_c1 = create_bdr_group_with_db('node_g2_c1', $bravo);

# Join BDR group 2 on postgres cluster 2 with database bravo
my $node_g2_c2 = join_bdr_group_with_db('node_g2_c2', $node_g2_c1, $bravo);

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

# Ensure database is empty before joining BDR group
$node_g2_c2->safe_psql($bravo, q[DROP TABLE fruits;]);

$node_g2_c2->safe_psql($bravo, qq{
    SELECT bdr.bdr_join_group(
        local_node_name := 'node_g2_g1_c2',
        node_external_dsn := '$node_g2_c2_connstr',
        join_using_dsn := '$node_g1_c1_connstr');});

my $result = log_contains($node_g2_c2,
	qr!ERROR: ( [A-Z0-9]+:)? replication slot .* already exists!,
	$logstart);
ok($result, "a database part of a BDR group joining another BDR group failure is detected");

done_testing();

sub create_bdr_group_with_db {
    my ($node_name, $db) = @_;

    my $node = PostgreSQL::Test::Cluster->new($node_name);
    initandstart_node($node, $db);

    my $port = $node->port;
    my $host = $node->host;
    my $node_connstr = "port=$port host=$host dbname=$db";

    $node->safe_psql($db, qq{
        SELECT bdr.bdr_create_group(
            local_node_name := '$node_name',
            node_external_dsn := '$node_connstr');});
    $node->safe_psql($db, qq[
        SELECT bdr.bdr_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
    $node->safe_psql($db, 'SELECT bdr.bdr_is_active_in_db()' ) eq 't'
    or BAIL_OUT('!bdr.bdr_is_active_in_db() after bdr_create_group');

    return $node;
}

sub join_bdr_group_with_db {
    my ($node_name, $upstream_node, $db) = @_;

    my $node = PostgreSQL::Test::Cluster->new($node_name);
    initandstart_node($node, $db);

    my $port = $node->port;
    my $host = $node->host;
    my $node_connstr = "port=$port host=$host dbname=$db";

    $port = $upstream_node->port;
    $host = $upstream_node->host;
    my $upstream_node_connstr = "port=$port host=$host dbname=$db";

    $node->safe_psql($db, qq{
        SELECT bdr.bdr_join_group(
            local_node_name := '$node_name',
            node_external_dsn := '$node_connstr',
            join_using_dsn := '$upstream_node_connstr');});
    $node->safe_psql($db, qq[
        SELECT bdr.bdr_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
    $node->safe_psql($db, 'SELECT bdr.bdr_is_active_in_db()' ) eq 't'
    or BAIL_OUT('!bdr.bdr_is_active_in_db() after bdr_join_group');

    return $node;
}

sub create_and_check_data {
    my ($node1, $node2, $db) = @_;

    $node1->safe_psql($db,
        q[CREATE TABLE fruits(id integer, name varchar);]);
    $node1->safe_psql($db,
        q[INSERT INTO fruits VALUES (1, 'Mango');]);
    wait_for_apply($node1, $node2);

    $node2->safe_psql($db,
        q[INSERT INTO fruits VALUES (2, 'Apple');]);
    wait_for_apply($node2, $node1);

    my $query = qq[SELECT COUNT(*) FROM fruits;];
    my $expected = 2;
    my $res1 = $node1->safe_psql($db, $query);
    my $res2 = $node2->safe_psql($db, $query);

    is($res1, $expected, "BDR node " . $node1->name() . "has all the data");
    is($res2, $expected, "BDR node " . $node2->name() . "has all the data");
}

# Return the size of logfile of $node in bytes
sub get_log_size
{
	my ($node) = @_;

	return (stat $node->logfile)[7];
}

# Find $pat in logfile of $node after $off-th byte
sub log_contains
{
	my ($node, $pat, $off) = @_;
	my $max_attempts = $PostgreSQL::Test::Utils::timeout_default * 10;
	my $log;

	while ($max_attempts-- >= 0)
	{
		$log = PostgreSQL::Test::Utils::slurp_file($node->logfile, $off);
		last if ($log =~ m/$pat/);
		usleep(100_000);
	}

	return $log =~ m/$pat/;
}
