#!/usr/bin/env perl
#
# Test interoperability of pgactive and postgres logical replication 
use strict;
use warnings;
use lib 'test/t/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use IPC::Run;
use Test::More;
use utils::nodemanagement;

# Create an upstream node and bring up pgactive
my $nodes = make_pgactive_group(2,'node_',undef,1);
my ($node_0,$node_1) = @$nodes;

# create a table on node_0, node_1 should get it too
exec_ddl($node_0, q[CREATE TABLE public.sports(id int primary key, name varchar);]);
wait_for_apply($node_0, $node_1);

$node_1->psql($pgactive_test_dbname, q[INSERT INTO public.sports VALUES (1, 'Badminton');]);
wait_for_apply($node_1, $node_0);

my $result =
  $node_0->safe_psql($pgactive_test_dbname, "SELECT count(*) FROM sports");
is($result, qq(1), 'table data on node_0 exists');

$result =
  $node_1->safe_psql($pgactive_test_dbname, "SELECT count(*) FROM sports");
is($result, qq(1), 'table data on node_1 exists');

# Test a case where pgactive node is a publisher in postgres logical replication and
# non-pgactive node is a subscriber. The subscriber must be able to pull in changes
# that are written on any of the pgactive node.
my $node_publisher = $node_1;
exec_ddl($node_publisher, q[CREATE PUBLICATION mypub FOR TABLE public.sports;]);

# Create subscriber node
my $node_subscriber = PostgreSQL::Test::Cluster->new('subscriber');
$node_subscriber->init(allows_streaming => 'logical');
$node_subscriber->start;
$node_subscriber->psql('postgres', q[CREATE TABLE public.sports(id int primary key, name varchar);]);

my $pgport = $node_publisher->port;
my $pghost = $node_publisher->host;
my $publisher_connstr = "port=$pgport host=$pghost dbname=$pgactive_test_dbname";
my $appname = 'pgactive_with_logicalrep_test';

$node_subscriber->safe_psql('postgres',
    qq{CREATE SUBSCRIPTION mysub CONNECTION '$publisher_connstr application_name=$appname' PUBLICATION mypub;});

# Wait for initial sync to finish
$node_subscriber->wait_for_subscription_sync($node_publisher, $appname);

# Existing data from the publisher must be available on the subscriber
$result =
  $node_subscriber->safe_psql('postgres', q[SELECT count(*) FROM sports;]);
is($result, qq(1), 'table data on subscriber exists');

# Any DML on pgactive members, the subscriber must receive them
$node_0->psql($pgactive_test_dbname, q[INSERT INTO sports VALUES (2, 'Cricket');]);
$node_0->psql($pgactive_test_dbname, q[INSERT INTO sports VALUES (3, 'Football');]);
wait_for_apply($node_0, $node_1);

$node_1->psql($pgactive_test_dbname, q[INSERT INTO sports VALUES (4, 'Volleyball');]);
$node_1->psql($pgactive_test_dbname, q[DELETE FROM sports WHERE id = 2;]);
wait_for_apply($node_1, $node_0);

# The subscriber must receive changes from any of the pgactive members
$result =
  $node_subscriber->safe_psql('postgres', q[SELECT count(*) FROM sports;]);
is($result, qq(3), 'final table data on subscriber exists');

$node_0->stop;
$node_1->stop;
$node_subscriber->stop;

# Testing when PUBLICATION/SUBSCRIPTION is created before pgactive is active
my $node_a = PostgreSQL::Test::Cluster->new('node_a');
my $node_b = PostgreSQL::Test::Cluster->new('node_b');

for my $node ($node_a, $node_b) {
    $node->init();
    pgactive_update_postgresql_conf($node);
    $node->start;

    $node->safe_psql('postgres', qq{CREATE DATABASE $pgactive_test_dbname;});
    $node->safe_psql($pgactive_test_dbname, q{CREATE EXTENSION pgactive;});
}

# Create publication
$node_a->safe_psql($pgactive_test_dbname, qq{CREATE PUBLICATION mypub FOR ALL TABLES;;});

# Create subscription
$pgport = $node_a->port;
$pghost = $node_a->host;
$publisher_connstr = "port=$pgport host=$pghost dbname=$pgactive_test_dbname";
$appname = 'pgactive_with_logicalrep_test';

$node_b->safe_psql($pgactive_test_dbname,
    qq{CREATE SUBSCRIPTION mysub CONNECTION '$publisher_connstr application_name=$appname' PUBLICATION mypub;});

# Wait for initial sync to finish
$node_b->wait_for_subscription_sync($node_a, $appname);

# No problem to create a pgactive group on the publisher
create_pgactive_group($node_a);

# join the subscriber
my $join_query = generate_pgactive_logical_join_query($node_b, $node_a);

# Must not use safe_psql since we expect an error here
my ($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');
($psql_ret, $psql_stdout, $psql_stderr) = $node_b->psql(
    $pgactive_test_dbname,
    $join_query);
like($psql_stderr, qr/be enabled because a logical replication subscription is created/,
     "joining of a node that has subscription fails");

# create should fail too

$pgport = $node_b->port;
$pghost = $node_b->host;
my $node_connstr = "port=$pgport host=$pghost dbname=$pgactive_test_dbname";

($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');
($psql_ret, $psql_stdout, $psql_stderr) = $node_b->psql(
    $pgactive_test_dbname,
    qq{ SELECT pgactive.pgactive_create_group(local_node_name := '@{[ $node_b->name ]}',
                                    node_external_dsn := '$node_connstr');
      }
);

like($psql_stderr, qr/be enabled because a logical replication subscription is created/,
     "group creation of a node that has subscription fails");

done_testing();
