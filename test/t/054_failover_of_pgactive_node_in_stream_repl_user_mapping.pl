#!/usr/bin/env perl
#
# Test if a streaming standby to a pgactive node gets pgactive node identifier getter
# function from the pgactive node.
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
initandstart_node($node_0, $pgactive_test_dbname, extra_init_opts => { allows_streaming => 1, has_archiving => 1 });

# Take backup
my $backup_name = 'my_backup';
$node_0->backup($backup_name);

# Create streaming standby linking to node_0
my $node_0_standby = PostgreSQL::Test::Cluster->new('node_0_standby');
$node_0_standby->init_from_backup($node_0, $backup_name, has_streaming => 1);
$node_0_standby->start;

# Make sure checkpoints don't interfere with the test
is( $node_0_standby->psql(
		$pgactive_test_dbname,
		qq[SELECT pg_create_physical_replication_slot('regress_test_slot1', true, false);]),
	0,
	'physical slot created on streaming standby');

# Time to bring up pgactive
my $pgport_0 = $node_0->port;
my $pghost_0 = $node_0->host;
my $node_0_fs = "node_0_fs";
my $node_0_user = $ENV{USERNAME} || $ENV{USERNAME} || $ENV{USER};

# Create user mapping machinery for self
$node_0->safe_psql($pgactive_test_dbname, qq{
    CREATE SERVER $node_0_fs FOREIGN DATA WRAPPER pgactive_fdw
        OPTIONS (port '$pgport_0', dbname '$pgactive_test_dbname', host '$pghost_0');});
$node_0->safe_psql($pgactive_test_dbname, qq{
    CREATE USER MAPPING FOR $node_0_user  SERVER $node_0_fs
        OPTIONS (user '$node_0_user');});

# Create pgactive group with user mapping
$node_0->safe_psql($pgactive_test_dbname, qq{
	SELECT pgactive.pgactive_create_group(
		node_name := 'node_0',
		node_dsn := 'user_mapping=$node_0_user pgactive_foreign_server=$node_0_fs');});
$node_0->safe_psql($pgactive_test_dbname, qq[
    SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
$node_0->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()' ) eq 't'
or BAIL_OUT('!pgactive.pgactive_is_active_in_db() after pgactive_create_group');

# Join a new node to the pgactive group
my $node_1 = PostgreSQL::Test::Cluster->new('node_1');
initandstart_node($node_1, $pgactive_test_dbname, extra_init_opts => { allows_streaming => 1, has_archiving => 1 });

# Create user mapping machinery for self
my $pgport_1 = $node_1->port;
my $pghost_1 = $node_1->host;
my $node_1_fs = "node_1_fs";
my $node_1_user = $ENV{USERNAME} || $ENV{USERNAME} || $ENV{USER};
$node_1->safe_psql($pgactive_test_dbname, qq{
    CREATE SERVER $node_1_fs FOREIGN DATA WRAPPER pgactive_fdw
        OPTIONS (port '$pgport_1', dbname '$pgactive_test_dbname', host '$pghost_1');});
$node_1->safe_psql($pgactive_test_dbname, qq{
    CREATE USER MAPPING FOR $node_1_user  SERVER $node_1_fs
        OPTIONS (user '$node_1_user');});

# Create user mapping machinery for node_0 on node_1
$node_1->safe_psql($pgactive_test_dbname, qq{
    CREATE SERVER $node_0_fs FOREIGN DATA WRAPPER pgactive_fdw
        OPTIONS (port '$pgport_0', dbname '$pgactive_test_dbname', host '$pghost_0');});
$node_1->safe_psql($pgactive_test_dbname, qq{
    CREATE USER MAPPING FOR $node_0_user  SERVER $node_0_fs
        OPTIONS (user '$node_0_user');});

# Create user mapping machinery for node_1 on node_0
$node_0->safe_psql($pgactive_test_dbname, qq{
    CREATE SERVER $node_1_fs FOREIGN DATA WRAPPER pgactive_fdw
        OPTIONS (port '$pgport_1', dbname '$pgactive_test_dbname', host '$pghost_1');});
$node_0->safe_psql($pgactive_test_dbname, qq{
    CREATE USER MAPPING FOR $node_1_user  SERVER $node_1_fs
        OPTIONS (user '$node_1_user');});

# Join pgactive group with user mapping
$node_1->safe_psql($pgactive_test_dbname, qq{
	SELECT pgactive.pgactive_join_group(
		node_name := 'node_1',
		node_dsn := 'user_mapping=$node_1_user pgactive_foreign_server=$node_1_fs',
        join_using_dsn := 'pgactive_foreign_server=$node_0_fs user_mapping=$node_0_user');});
$node_1->safe_psql($pgactive_test_dbname, qq[
    SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
$node_1->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()' ) eq 't'
or BAIL_OUT('!pgactive.pgactive_is_active_in_db() after pgactive_create_group');

# Create some data
$node_0->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE fruits(id integer, name varchar);]);
$node_0->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (1, 'Mango');]);
wait_for_apply($node_0, $node_1);

$node_1->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (2, 'Apple');]);
wait_for_apply($node_1, $node_0);

# Wait for standby catchup
$node_0->wait_for_catchup($node_0_standby, 'replay', $node_0->lsn('flush'));

# Make sure all pgactive nodes and standby connected to a pgactive node get all changes
# occurred on all of the pgactive nodes.
my $query = qq[SELECT COUNT(*) FROM fruits;];
my $expected = 2;
my $node_0_res = $node_0->safe_psql($pgactive_test_dbname, $query);
my $node_1_res = $node_1->safe_psql($pgactive_test_dbname, $query);

is($node_0_res, $expected, "pgactive node node_0 has all the data");
is($node_1_res, $expected, "pgactive node node_1 has all the data");

$node_0_standby->poll_query_until($pgactive_test_dbname,
  qq[SELECT COUNT(*) = 2 FROM fruits;])
  or die "timed out waitinf for standby to receive all the data from primary";

my $node_0_datadir = $node_0->data_dir;
my $node_0_replslotname = $node_0->safe_psql($pgactive_test_dbname,
    qq[SELECT slot_name FROM pgactive.pgactive_node_slots;]);
my $node_0_replslotdir = "$node_0_datadir/pg_replslot/$node_0_replslotname";

# Stop pgactive node a.k.a primary to simulate a failover to standby
$node_0->stop;

# Copy pgactive logical replication slots from primary to standby
my $node_0_standby_datadir = $node_0_standby->data_dir;
my $node_0_standby_replslotdir = "$node_0_standby_datadir/pg_replslot/$node_0_replslotname";

mkdir "$node_0_standby_replslotdir";
copy("$node_0_replslotdir/state", "$node_0_standby_replslotdir/state");

# Promote standby
$node_0_standby->promote;

# Update user mapping machinery of new primary a.k.a standby after failover.
# Note that this step may not be necessary in production if standby uses the
# same conninfo as that of the primary after failover.
my $pgport_0_sb = $node_0_standby->port;
my $pghost_0_sb = $node_0_standby->host;

# Update new primary info in foreign server object created with old primary
# info. Do this on all pgactive members.
$node_0_standby->safe_psql($pgactive_test_dbname,
    qq[ALTER SERVER $node_0_fs OPTIONS (SET port '$pgport_0_sb', SET host '$pghost_0_sb');]);
$node_1->safe_psql($pgactive_test_dbname,
    qq[ALTER SERVER $node_0_fs OPTIONS (SET port '$pgport_0_sb', SET host '$pghost_0_sb');]);

# Restart standby so that the pgactive machinary gets started up and we are able to
# update DSNs.
$node_0_standby->restart;

$node_0_standby->safe_psql( $pgactive_test_dbname,
        qq[SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
$node_0_standby->safe_psql( $pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()' ) eq 't'
        or BAIL_OUT('!pgactive.pgactive_is_active_in_db() after pgactive_create_group');

# Perform some DML in the pgactive group after new primary joined pgactive group
# seamlessly.
$node_0_standby->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (4, 'Kiwi');]);
wait_for_apply($node_0_standby, $node_1);

$node_1->safe_psql($pgactive_test_dbname,
    q[INSERT INTO fruits VALUES (3, 'Cherry');]);
wait_for_apply_with_peer_name($node_1, 'node_0');

$expected = 4;
my $node_0_standby_res = $node_0_standby->safe_psql($pgactive_test_dbname, $query);
$node_1_res = $node_1->safe_psql($pgactive_test_dbname, $query);

is($node_0_standby_res, $expected, "pgactive node a.k.a new primary after failover has all the data");
is($node_1_res, $expected, "pgactive node node_1 has all the data after new primary joined pgactive group seamlessly");

# Perform some DDL in the pgactive group after new primary joined pgactive group
# seamlessly.
$node_0_standby->safe_psql($pgactive_test_dbname,
    q[CREATE TABLE sports(id integer, name varchar);]);
$node_0_standby->safe_psql($pgactive_test_dbname,
    q[INSERT INTO sports VALUES (1, 'Cricket');]);
wait_for_apply($node_0_standby, $node_1);

$node_1->safe_psql($pgactive_test_dbname,
    q[INSERT INTO sports VALUES (2, 'Kabaddi');]);
wait_for_apply_with_peer_name($node_1, 'node_0');

$query = qq[SELECT COUNT(*) FROM sports;];
$expected = 2;
$node_0_standby_res = $node_0_standby->safe_psql($pgactive_test_dbname, $query);
$node_1_res = $node_1->safe_psql($pgactive_test_dbname, $query);

is($node_0_standby_res, $expected, "pgactive node a.k.a new primary after failover has all the DDL data");
is($node_1_res, $expected, "pgactive node node_1 has all the DDL data after new primary joined pgactive group seamlessly");

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
