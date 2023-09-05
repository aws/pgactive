#!/usr/bin/env perl
#
# Test logical join failure cases.
use strict;
use warnings;
use lib 'test/t/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use utils::nodemanagement;

my $node_a = PostgreSQL::Test::Cluster->new('node_a');
my $node_b = PostgreSQL::Test::Cluster->new('node_b');

$node_a->init();
bdr_update_postgresql_conf($node_a);
$node_a->start;

# Test logical join failure if joining node and remote node have different
# database collation settings.
$node_a->safe_psql('postgres', qq{CREATE DATABASE $bdr_test_dbname WITH ENCODING 'UTF8' LC_COLLATE 'en_US.UTF-8' LC_CTYPE 'en_US.UTF-8' TEMPLATE template0;});
$node_a->safe_psql($bdr_test_dbname, q{CREATE EXTENSION bdr;});

$node_b->init();
bdr_update_postgresql_conf($node_b);
$node_b->start;

$node_b->safe_psql('postgres', qq{CREATE DATABASE $bdr_test_dbname WITH ENCODING 'UTF8' LC_COLLATE 'C.utf8' LC_CTYPE 'C.utf8' TEMPLATE template0;});
$node_b->safe_psql($bdr_test_dbname, q{CREATE EXTENSION bdr;});

# Bring up a single BDR node, stand-alone
create_bdr_group($node_a);

my $join_query = generate_bdr_logical_join_query($node_b, $node_a);

# Must not use safe_psql since we expect an error here
my ($psql_ret, $psql_stdout, $psql_stderr) = ('','', '');
($psql_ret, $psql_stdout, $psql_stderr) = $node_b->psql(
    $bdr_test_dbname,
    $join_query);
like($psql_stderr, qr/.*ERROR.*joining node and remote node have different database collation settings/,
     "joining of a node failed due to different different database collation settings");

# Test logical join failure if joining node has existing user tables in the
# database.
$node_b->safe_psql('postgres', qq[DROP DATABASE $bdr_test_dbname;]);
$node_b->safe_psql('postgres', qq{CREATE DATABASE $bdr_test_dbname WITH ENCODING 'UTF8' LC_COLLATE 'en_US.UTF-8' LC_CTYPE 'en_US.UTF-8' TEMPLATE template0;});
$node_b->safe_psql($bdr_test_dbname, q{CREATE EXTENSION bdr;});

# Create a pre-existing user table.
$node_b->safe_psql($bdr_test_dbname,
    q[CREATE TABLE fruits(id integer, name varchar);]);
$node_b->safe_psql($bdr_test_dbname,
    q[INSERT INTO fruits VALUES (1, 'Cherry');]);

# Must not use safe_psql since we expect an error here
($psql_ret, $psql_stdout, $psql_stderr) = $node_b->psql(
    $bdr_test_dbname,
    $join_query);
like($psql_stderr, qr/.*ERROR.*database joining BDR group has existing user tables/,
     "joining of a node failed due to existing user tables in database");

done_testing();
