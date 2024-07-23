#!/usr/bin/env perl
#
# This test verifies that the instance restored from backup of pgactive node
# doesn't try to connect to upstream node, in other words, join pgactive group.
#
use strict;
use warnings;
use File::Path qw(rmtree);
use lib 'test/t/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use IPC::Run;
use Test::More;
use utils::nodemanagement;

# Create an upstream node and bring up pgactive
my $node_0 = PostgreSQL::Test::Cluster->new('node_0');
initandstart_node($node_0);
create_pgactive_group($node_0);

# Join a new node to the pgactive group
my $node_1 = PostgreSQL::Test::Cluster->new('node_1');
initandstart_node($node_1, $pgactive_test_dbname, extra_init_opts => { has_archiving => 1 });
pgactive_logical_join( $node_1, $node_0 );
check_join_status( $node_1, $node_0);

# Let's take a backup of pgactive node
my $backup_name = 'mybackup';
$node_1->backup($backup_name);

my $node_2 = PostgreSQL::Test::Cluster->new('node_2');
$node_2->init_from_backup($node_1, $backup_name);
$node_2->start;

my $logstart_2 = get_log_size($node_2);

# Detached node must unregister apply worker
my $result = find_in_log($node_2,
	qr!LOG: ( [A-Z0-9]+:)? unregistering per-db worker on node .* due to failure when connecting to ourself!,
	$logstart_2);
ok($result, "unregistering per-db worker due to failure when connecting to ourself is detected");

# There mustn't be any pgactive workers on restored instance
$result = $node_2->safe_psql($pgactive_test_dbname, qq[SELECT COUNT(*) FROM pgactive.pgactive_get_workers_info();]);
is($result, '0', "restored node " . $node_2->name() . "doesn't have pgactive workers");

# Let's get rid of pgactive completely on restored instance
$node_2->safe_psql($pgactive_test_dbname, qq[SELECT pgactive.pgactive_remove(true);]);
$node_2->safe_psql($pgactive_test_dbname, qq[DROP EXTENSION pgactive;]);

# Stop a node and remove the replication slot
my $logstart_1 = get_log_size($node_1);
my $datadir           = $node_1->data_dir;
my $slot_name = $node_1->safe_psql('postgres',
	"SELECT slot_name from pg_replication_slots;"
);
my $pgactive_replslotdir = "$datadir/pg_replslot/$slot_name";
$node_1->stop;
rmtree($pgactive_replslotdir);
$node_1->start;

# apply worker should not be started
$result = find_in_log($node_1,
	qr!LOG: ( [A-Z0-9]+:)? slot .* does not exist for node .*, skipping related apply worker start!,
	$logstart_1);
ok($result, "skipping related apply worker start due to missing replication slot");

done_testing();
