#!/usr/bin/env perl
#
# Test interoperability of BDR and postgres logical replication 
use strict;
use warnings;
use lib 't/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use IPC::Run;
use Test::More;
use utils::nodemanagement;

# Create an upstream node and bring up bdr
my $nodes = make_bdr_group(2,'node_');
my ($node_0,$node_1) = @$nodes;

# create a table on node_0, node_1 should get it too
exec_ddl($node_0, q[CREATE TABLE public.sports(id int primary key, name varchar);]);
wait_for_apply($node_0, $node_1);

$node_1->psql($bdr_test_dbname, q[INSERT INTO public.sports VALUES (1, 'Badminton');]);
wait_for_apply($node_1, $node_0);

my $result =
  $node_0->safe_psql($bdr_test_dbname, "SELECT count(*) FROM sports");
is($result, qq(1), 'table data on node_0 exists');

$result =
  $node_1->safe_psql($bdr_test_dbname, "SELECT count(*) FROM sports");
is($result, qq(1), 'table data on node_1 exists');

# Test a case where BDR node is a publisher in postgres logical replication and
# non-BDR node is a subscriber. The subscriber must be able to pull in changes
# that are written on any of the BDR node.
my $node_publisher = $node_1;
exec_ddl($node_publisher, q[CREATE PUBLICATION mypub FOR TABLE public.sports;]);

# Create subscriber node
my $node_subscriber = PostgreSQL::Test::Cluster->new('subscriber');
$node_subscriber->init(allows_streaming => 'logical');
$node_subscriber->start;
$node_subscriber->psql('postgres', q[CREATE TABLE public.sports(id int primary key, name varchar);]);

my $pgport = $node_publisher->port;
my $pghost = $node_publisher->host;
my $publisher_connstr = "port=$pgport host=$pghost dbname=$bdr_test_dbname";
my $appname = 'bdr_with_logicalrep_test';

$node_subscriber->safe_psql('postgres',
    qq{CREATE SUBSCRIPTION mysub CONNECTION '$publisher_connstr application_name=$appname' PUBLICATION mypub;});

# Wait for initial sync to finish
$node_subscriber->wait_for_subscription_sync($node_publisher, $appname);

# Existing data from the publisher must be available on the subscriber
$result =
  $node_subscriber->safe_psql('postgres', q[SELECT count(*) FROM sports;]);
is($result, qq(1), 'table data on subscriber exists');

# Any DML on BDR members, the subscriber must receive them
$node_0->psql($bdr_test_dbname, q[INSERT INTO sports VALUES (2, 'Cricket');]);
$node_0->psql($bdr_test_dbname, q[INSERT INTO sports VALUES (3, 'Football');]);
wait_for_apply($node_0, $node_1);

$node_1->psql($bdr_test_dbname, q[INSERT INTO sports VALUES (4, 'Volleyball');]);
$node_1->psql($bdr_test_dbname, q[DELETE FROM sports WHERE id = 2;]);
wait_for_apply($node_1, $node_0);

# The subscriber must receive changes from any of the BDR members
$result =
  $node_subscriber->safe_psql('postgres', q[SELECT count(*) FROM sports;]);
is($result, qq(3), 'final table data on subscriber exists');

done_testing();
