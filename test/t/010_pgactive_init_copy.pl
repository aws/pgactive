#!/usr/bin/env perl
#
# This test exercises pgactive's physical copy mechanism for node joins,
# pgactive_init_copy .
#
use strict;
use warnings;
use lib 'test/t/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use utils::nodemanagement;

my $tempdir = PostgreSQL::Test::Utils::tempdir;

my $node_a = PostgreSQL::Test::Cluster->new('node-a');
initandstart_node($node_a, $pgactive_test_dbname, extra_init_opts => { has_archiving => 1 });

my $node_b = PostgreSQL::Test::Cluster->new('node-b');

command_fails(['pgactive_init_copy'],
	'pgactive_init_copy needs target directory specified');
command_fails(
	[ 'pgactive_init_copy', '-D', "$tempdir/backup" ],
	'pgactive_init_copy fails because of missing node name');
command_fails(
	[ 'pgactive_init_copy', '-D', "$tempdir/backup", "-n", "newnode"],
	'pgactive_init_copy fails because of missing remote conninfo');
command_fails(
	[ 'pgactive_init_copy', '-D', "$tempdir/backup", "-n", "newnode", '-d', $node_a->connstr('postgres')],
	'pgactive_init_copy fails because of missing local conninfo');
command_fails(
	[ 'pgactive_init_copy', '-D', "$tempdir/backup", "-n", "newnode", '-d', $node_a->connstr('postgres'), '--local-dbname', 'postgres', '--local-port', $node_b->port],
	'pgactive_init_copy fails when there is no pgactive database');

# Time to bring up pgactive
create_pgactive_group($node_a);

$node_a->safe_psql($pgactive_test_dbname,
	qq[SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);

# PostgresNode doesn't know we started the node since we didn't
# use any of its methods, so we'd better tell it to check. Otherwise
# it'll ignore the node for things like pg_ctl stop.
$node_a->_update_pid(1);

is($node_a->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()'), 't',
	'pgactive is active on node_a');

# The postgresql.conf copied by pgactive_init_copy's pg_basebackup invocation will
# use the same port as node_a . We can't have that, so template a new config file.
open(my $conf_a, "<", $node_a->data_dir . '/postgresql.conf')
	or die ("can't open node_a conf file for reading: $!");

open(my $conf_b, ">", "$tempdir/postgresql.conf.b")
	or die ("can't open node_b conf file for writing: $!");

while (<$conf_a>)
{
	if ($_ =~ "^port")
	{
		print $conf_b "port = " . $node_b->port . "\n";
	}
	else
	{
		print $conf_b $_;
	}
}
close($conf_a) or die ("failed to close old postgresql.conf: $!");
close($conf_b) or die ("failed to close new postgresql.conf: $!");


command_ok(
    [
        'pgactive_init_copy', '-v',
        '-D', $node_b->data_dir,
        "-n", 'node-b',
        '-d', $node_a->connstr($pgactive_test_dbname),
        '--local-dbname', $pgactive_test_dbname,
        '--local-port', $node_b->port,
        '--postgresql-conf', "$tempdir/postgresql.conf.b",
        '--log-file', $node_b->logfile . "_initcopy",
        '--apply-delay', 1000
    ],
	'pgactive_init_copy succeeds');

# ... but does replication actually work? Is this a live, working cluster?
my $pgactive_version = $node_b->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_version()');
note "pgactive version $pgactive_version";

$node_a->safe_psql($pgactive_test_dbname,
	qq[SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
$node_b->safe_psql($pgactive_test_dbname,
	qq[SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);

# PostgresNode doesn't know we started the node since we didn't
# use any of its methods, so we'd better tell it to check. Otherwise
# it'll ignore the node for things like pg_ctl stop.
$node_b->_update_pid(1);

is($node_a->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()'), 't',
	'pgactive is active on node_a');
is($node_b->safe_psql($pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()'), 't',
	'pgactive is active on node_b');

my $status_a = $node_a->safe_psql($pgactive_test_dbname, 'SELECT node_name, pgactive.pgactive_node_status_from_char(node_status) FROM pgactive.pgactive_nodes ORDER BY node_name');
my $status_b = $node_b->safe_psql($pgactive_test_dbname, 'SELECT node_name, pgactive.pgactive_node_status_from_char(node_status) FROM pgactive.pgactive_nodes ORDER BY node_name');

is($status_a, "node-a|pgactive_NODE_STATUS_READY\nnode-b|pgactive_NODE_STATUS_READY", 'node A sees both nodes as ready');
is($status_b, "node-a|pgactive_NODE_STATUS_READY\nnode-b|pgactive_NODE_STATUS_READY", 'node B sees both nodes as ready');

note "Taking ddl lock manually";

$node_a->safe_psql($pgactive_test_dbname, "SELECT pgactive.pgactive_acquire_global_lock('write_lock')");

note "Creating a table...";

exec_ddl($node_b, q[CREATE TABLE public.reptest(id integer primary key, dummy text);]);
$node_a->poll_query_until($pgactive_test_dbname, q{
SELECT EXISTS (
  SELECT 1 FROM pg_class c INNER JOIN pg_namespace n ON n.nspname = 'public' AND c.relname = 'reptest'
);
});

ok($node_b->safe_psql($pgactive_test_dbname, "SELECT 'reptest'::regclass"), "reptest table creation replicated");

$node_a->safe_psql($pgactive_test_dbname, "INSERT INTO reptest (id, dummy) VALUES (1, '42')");

$node_b->poll_query_until($pgactive_test_dbname, q{
SELECT EXISTS (
  SELECT 1 FROM reptest
);
});

is($node_b->safe_psql($pgactive_test_dbname, 'SELECT id, dummy FROM reptest;'), '1|42', "reptest insert successfully replicated");

my $seqid_a = $node_a->safe_psql($pgactive_test_dbname, 'SELECT node_seq_id FROM pgactive.pgactive_nodes WHERE node_name = pgactive.pgactive_get_local_node_name()');
my $seqid_b = $node_b->safe_psql($pgactive_test_dbname, 'SELECT node_seq_id FROM pgactive.pgactive_nodes WHERE node_name = pgactive.pgactive_get_local_node_name()');

is($seqid_a, 1, 'first node got global sequence ID 1');
is($seqid_b, 2, 'second node got global sequence ID 2');

done_testing();
