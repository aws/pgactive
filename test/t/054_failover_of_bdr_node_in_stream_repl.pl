#!/usr/bin/env perl
#
# Test if a streaming standby to a BDR node gets BDR node identifier getter
# function from the BDR node.
use strict;
use warnings;
use lib 'test/t/';
use Cwd;
use Config;
use File::Copy;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use IPC::Run;
use Test::More;
use utils::nodemanagement;

# Create an upstream node
my $node_0 = PostgreSQL::Test::Cluster->new('node_0');
initandstart_node($node_0, $bdr_test_dbname, extra_init_opts => { allows_streaming => 1, has_archiving => 1 });

# Take backup
my $backup_name = 'my_backup';
$node_0->backup($backup_name);

# Create streaming standby linking to node_0
my $node_0_standby = PostgreSQL::Test::Cluster->new('node_0_standby');
$node_0_standby->init_from_backup($node_0, $backup_name, has_streaming => 1);
$node_0_standby->start;

# Make sure checkpoints don't interfere with the test
is( $node_0_standby->psql(
		$bdr_test_dbname,
		qq[SELECT pg_create_physical_replication_slot('regress_test_slot1', true, false);]),
	0,
	'physical slot created on streaming standby');

# Time to bring up BDR
create_bdr_group($node_0);

# Join a new node to the bdr group
my $node_1 = PostgreSQL::Test::Cluster->new('node_1');
initandstart_physicaljoin_node( $node_1, $node_0 );

# XXX: Can also logically join another node to the same BDR group, but that's
# for another day.

$node_0->safe_psql($bdr_test_dbname,
    q[CREATE TABLE fruits(id integer, name varchar);]);
$node_0->safe_psql($bdr_test_dbname,
    q[INSERT INTO fruits VALUES (1, 'Mango');]);
wait_for_apply($node_0, $node_1);

$node_1->safe_psql($bdr_test_dbname,
    q[INSERT INTO fruits VALUES (2, 'Apple');]);
wait_for_apply($node_1, $node_0);

# Wait for standby catchup
$node_0->wait_for_catchup($node_0_standby);

# Make sure all BDR nodes and standby connected to a BDR node get all changes
# occurred on all of the BDR nodes.
my $query = qq[SELECT COUNT(*) FROM fruits;];
my $expected = 2;
my $node_0_res = $node_0->safe_psql($bdr_test_dbname, $query);
my $node_0_standby_res = $node_0_standby->safe_psql($bdr_test_dbname, $query);
my $node_1_res = $node_1->safe_psql($bdr_test_dbname, $query);

is($node_0_res, $expected, "BDR node node_0 has all the data");
is($node_0_standby_res, $expected, "standby connected to BDR node node_0 has all the data");
is($node_1_res, $expected, "BDR node node_1 has all the data");

my $pgport = $node_0->port;
my $pghost = $node_0->host;
my $node_0_connstr = "port=$pgport host=$pghost dbname=$bdr_test_dbname";

$pgport = $node_0_standby->port;
$pghost = $node_0_standby->host;
my $node_0_standby_connstr = "port=$pgport host=$pghost dbname=$bdr_test_dbname";

$pgport = $node_1->port;
$pghost = $node_1->host;
my $node_1_connstr = "port=$pgport host=$pghost dbname=$bdr_test_dbname";

my $node_0_datadir = $node_0->data_dir;
my $node_0_replslotname = $node_0->safe_psql($bdr_test_dbname,
    qq[SELECT slot_name FROM bdr.bdr_node_slots;]);
my $node_0_replslotdir = "$node_0_datadir/pg_replslot/$node_0_replslotname";

# Stop BDR node a.k.a primary to simulate a failover to standby
$node_0->stop;

# Copy BDR logical replication slots from primary to standby
my $node_0_standby_datadir = $node_0_standby->data_dir;
my $node_0_standby_replslotdir = "$node_0_standby_datadir/pg_replslot/$node_0_replslotname";

mkdir "$node_0_standby_replslotdir";
copy("$node_0_replslotdir/state", "$node_0_standby_replslotdir/state");

# Promote standby
$node_0_standby->promote;

# Update DSNs of new primary a.k.a standby after failover in bdr.bdr_nodes and
# bdr.bdr_connections tables. Note that this step may not be necessary in
# production if standby uses the same DSN as that of the primary after failover.
$node_1->safe_psql($bdr_test_dbname,
    qq[UPDATE bdr.bdr_nodes SET node_local_dsn = '$node_0_standby_connstr'
       WHERE node_local_dsn = '$node_0_connstr' AND node_init_from_dsn IS NULL;]);
$node_1->safe_psql($bdr_test_dbname,
    qq[UPDATE bdr.bdr_nodes SET node_init_from_dsn = '$node_0_standby_connstr'
       WHERE node_init_from_dsn = '$node_0_connstr' AND node_init_from_dsn IS NOT NULL;]);
$node_1->safe_psql($bdr_test_dbname,
    qq[UPDATE bdr.bdr_connections SET conn_dsn = '$node_0_standby_connstr'
       WHERE conn_dsn = '$node_0_connstr';]);

# Restart standby so that the BDR machinary gets started up and we are able to
# update DSNs.
$node_0_standby->restart;

$node_0_standby->safe_psql( $bdr_test_dbname,
        qq[SELECT bdr.bdr_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
$node_0_standby->safe_psql( $bdr_test_dbname, 'SELECT bdr.bdr_is_active_in_db()' ) eq 't'
        or BAIL_OUT('!bdr.bdr_is_active_in_db() after bdr_create_group');

$node_0_standby->safe_psql($bdr_test_dbname,
    qq[UPDATE bdr.bdr_nodes SET node_local_dsn = '$node_0_standby_connstr'
       WHERE node_local_dsn = '$node_0_connstr' AND node_init_from_dsn IS NULL;]);
$node_0_standby->safe_psql($bdr_test_dbname,
    qq[UPDATE bdr.bdr_nodes SET node_init_from_dsn = '$node_0_standby_connstr'
       WHERE node_init_from_dsn = '$node_0_connstr' AND node_init_from_dsn IS NOT NULL;]);
$node_0_standby->safe_psql($bdr_test_dbname,
    qq[UPDATE bdr.bdr_connections SET conn_dsn = '$node_0_standby_connstr'
       WHERE conn_dsn = '$node_0_connstr';]);

$node_0_standby->safe_psql( $bdr_test_dbname,
        qq[SELECT bdr.bdr_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
$node_0_standby->safe_psql( $bdr_test_dbname, 'SELECT bdr.bdr_is_active_in_db()' ) eq 't'
        or BAIL_OUT('!bdr.bdr_is_active_in_db() after bdr_create_group');

# Perform some DML in the BDR group after new primary joined BDR group
# seamlessly.
$node_0_standby->safe_psql($bdr_test_dbname,
    q[INSERT INTO fruits VALUES (4, 'Kiwi');]);
wait_for_apply($node_0_standby, $node_1);

$node_1->safe_psql($bdr_test_dbname,
    q[INSERT INTO fruits VALUES (3, 'Cherry');]);
wait_for_apply_with_peer_name($node_1, 'node_0');

$expected = 4;
$node_0_standby_res = $node_0_standby->safe_psql($bdr_test_dbname, $query);
$node_1_res = $node_1->safe_psql($bdr_test_dbname, $query);

is($node_0_standby_res, $expected, "BDR node a.k.a new primary after failover has all the data");
is($node_1_res, $expected, "BDR node node_1 has all the data after new primary joined BDR group seamlessly");

# Perform some DDL in the BDR group after new primary joined BDR group
# seamlessly.
$node_0_standby->safe_psql($bdr_test_dbname,
    q[CREATE TABLE sports(id integer, name varchar);]);
$node_0_standby->safe_psql($bdr_test_dbname,
    q[INSERT INTO sports VALUES (1, 'Cricket');]);
wait_for_apply($node_0_standby, $node_1);

$node_1->safe_psql($bdr_test_dbname,
    q[INSERT INTO sports VALUES (2, 'Kabaddi');]);
wait_for_apply_with_peer_name($node_1, 'node_0');

$query = qq[SELECT COUNT(*) FROM sports;];
$expected = 2;
$node_0_standby_res = $node_0_standby->safe_psql($bdr_test_dbname, $query);
$node_1_res = $node_1->safe_psql($bdr_test_dbname, $query);

is($node_0_standby_res, $expected, "BDR node a.k.a new primary after failover has all the DDL data");
is($node_1_res, $expected, "BDR node node_1 has all the DDL data after new primary joined BDR group seamlessly");

done_testing();

# Wait until a peer has caught up. Similar to wait_for_apply but peer node name
# is provided as an input. This is because after the failover, the standby uses
# original primary node name.
sub wait_for_apply_with_peer_name {
    my ($self, $peer_node_name) = @_;
    # On node <self>, wait until the send pointer on the replication slot with
    # application_name "<peer>:send" to passes the xlog flush position on node
    # <self> at the time of this call.
    my $lsn = $self->lsn('flush');
    die('no lsn to catch up to') if !defined $lsn;
    $self->wait_for_catchup($peer_node_name . ":send", 'replay', $lsn);
}
