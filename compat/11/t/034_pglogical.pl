#!/usr/bin/env perl
#
# Use pglogical as a subscriber on a bdr node, or as a provider fed
# by bdr. In either case we must verify that writes go from/to the
# peer that isn't attached to pglogical too.
#
# This currently has a number of limitations:
#
# * Manual schema clone required at setup time, can't
#   schema-sync.
#
# * DDL requires two layers of wrapper functions. The pglogical function MUST
#   be the outer of the two, e.g.
#
#		SELECT pglogical.replicate_ddl_command($PGLDDL$
#		SELECT bdr.bdr_replicate_ddl_command($BDRDDL$
#		CREATE TABLE public.my_table(id integer primary key, dummy text);
#		$BDRDDL$);
#		$PGLDDL$);
#
# * No failover supported on upstream or downstream. This replicates from
#   one SPECIFIC bdr node to one other SPECIFIC bdr node. But it replicates
#   writes from all upstream bdr node to all downstream bdr nodes, you just
#   can't change out which node is the pgl provider and subscriber.
#
# * Initial data sync untested as yet.
#
# * Table resync untested as yet
#
use strict;
use warnings;
use lib 'compat/11/t/';
use Cwd;
use Config;
use PostgresNode;
use TestLib;
use IPC::Run qw(timeout);;
use Test::More;
use utils::nodemanagement;
use File::Temp qw(tempfile);
use Carp;

$SIG{__DIE__} = sub { Carp::confess @_ };
$SIG{INT}  = sub { die("interupted by SIGINT"); };

# Sanity check: is the pglogical extension present? If not, there's no point continuing this test.
my $compat_check = get_new_node('compat_check');
$compat_check->init;
$compat_check->start;

my $pgl_version = $compat_check->safe_psql('postgres',
	"SELECT default_version FROM pg_available_extensions WHERE name = 'pglogical'");
if ($pgl_version)
{
	note "Detected pglogical $pgl_version";
}
else
{
	plan skip_all => 'no pglogical in pg_available_extensions';
}

$compat_check->stop;


# Create an upstream node and bring up bdr
my $providers = make_bdr_group(2,'provider_');
my ($provider_0, $provider_1) = @$providers;

$provider_0->safe_psql($bdr_test_dbname, q[
SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE TABLE public.preseed_in(id integer primary key, blah text);
$DDL$);
]);

$provider_0->safe_psql($bdr_test_dbname, q[
SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE TABLE public.preseed_ex(id integer primary key, blah text);
$DDL$);
]);

$provider_0->safe_psql($bdr_test_dbname, q[ INSERT INTO preseed_in(id, blah) VALUES (1, 'provider_0'); ]);
$provider_0->safe_psql($bdr_test_dbname, q[ INSERT INTO preseed_ex(id, blah) VALUES (1, 'provider_0'); ]);
$provider_0->safe_psql($bdr_test_dbname, q[SELECT bdr.wait_slot_confirm_lsn(NULL, NULL);]);

$provider_1->safe_psql($bdr_test_dbname, q[ INSERT INTO preseed_in(id, blah) VALUES (2, 'provider_1'); ]);
$provider_1->safe_psql($bdr_test_dbname, q[ INSERT INTO preseed_ex(id, blah) VALUES (2, 'provider_1'); ]);
$provider_1->safe_psql($bdr_test_dbname, q[SELECT bdr.wait_slot_confirm_lsn(NULL, NULL);]);

my $subscribers = make_bdr_group(2,'subscriber_');
my ($subscriber_0, $subscriber_1) = @$subscribers;

# We must add pglogical to s_p_l on subscriber1 and provider1
# even though they're not using pglogical directly because pgl
# won't let us CREATE EXTENSION pglogical without it.
#
for my $node (@$providers, @$subscribers) {
    $node->append_conf('postgresql.conf', q[
shared_preload_libraries = 'bdr,pglogical'
    ]);
    $node->restart;
}


# On provider we'll replicate the extension creation
$provider_0->safe_psql($bdr_test_dbname, q[
SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE EXTENSION pglogical;
$DDL$);]);

$provider_0->safe_psql($bdr_test_dbname,
	q[SELECT * FROM pglogical.create_node(node_name := 'bdr_provider', dsn := '] . $provider_0->connstr($bdr_test_dbname) . q[');]);

$provider_0->safe_psql($bdr_test_dbname, q[SELECT * FROM pglogical.replication_set_add_table('default', 'preseed_in')]);

$provider_0->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);

# Same deal on the subscriber
$subscriber_0->safe_psql($bdr_test_dbname, q[
SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE EXTENSION pglogical;
$DDL$);]);

note "created extension on subscriber";

# BDR replicates everything by default, even in extensions. This gets exciting when
# the extension in question manages replication. If we replicate the pglogical
# subscriber-side catalogs to the other BDR nodes on the subscriber group, the other
# nodes will try to connect too! Bad Things Will Happen if we disconnect and one of them
# gets in, since pglogical creates a nonexistent local replication origin and starts
# replaying merrily.
#
# We must prevent this, so exclude pglogical from replication. Maybe we should have
# a helper func to do this per-extension or per-schema, like pgl does. (TODO)
#
$subscriber_0->safe_psql($bdr_test_dbname, q[
SELECT *
FROM pg_class c
INNER JOIN LATERAL bdr.table_set_replication_sets('pglogical.'||c.relname, '{skip_pgl_catalogs}') skipped ON (c.relnamespace = 'pglogical'::regnamespace AND c.relkind = 'r' AND c.relpersistence = 'p' );
]);

note "removed pgl from repsets on subscriber";

$subscriber_0->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);
note("bdr caught up subscriber");

$subscriber_0->safe_psql($bdr_test_dbname,
	q[SELECT * FROM pglogical.create_node(node_name := 'bdr_subscriber_0', dsn := '] . $subscriber_0->connstr($bdr_test_dbname) . q[');]);

note("created pgl node on subscriber");

$subscriber_0->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);
note("bdr caught up subscriber");

# Before we subscribe to the provider, lets take a dump of our BDR node catalog
# state on the subscriber. It's important that this NOT be copied by the
# initial sync from the provider, after all.
my $presubscribe_bdr_nodes = $subscriber_0->safe_psql($bdr_test_dbname, q[SELECT node_sysid, node_timeline, node_dboid, node_name FROM bdr.bdr_nodes ORDER BY 1,2,3,4]);

# We should be able to use synchronize_structure := true in our subscription,
# but we can't because pgl's pg_restore invocation won't wrap the commands in
# bdr.bdr_replicate_ddl_command. There's no pg_dump option to generate a prewrapped
# dump either.
#
# Since we don't do transparent DDL rep that means we'll fail with DDL errors.
#
# Work around by applying the schema to each downstream with replication off.
# Of course this can only work if you don't make concurrent schema changes. To
# prevent concurrent changes we could take a global DDL lock in 'ddl_lock' mode;
# this won't block concurrent writes.

my ($stdin, $stdout, $stderr);
my $lock_handle = IPC::Run::start(['psql', '-qAtX', '-d', $provider_0->connstr($bdr_test_dbname)],
	'<', \$stdin,
	'>', \$stdout,
	'2>', \$stderr);

$stdin .= q[
BEGIN;
SELECT bdr.acquire_global_lock('ddl_lock');
SELECT 1;
];

do { $lock_handle->pump; print $stdout; } until $stdout =~ /1[\r\n]$/;

ok($provider_0->safe_psql($bdr_test_dbname, q[SELECT 1 FROM bdr.bdr_locks WHERE owner_node_name = '] . $provider_0->name . q[' AND lock_mode = 'ddl_lock' AND lock_state = 'acquire_acquired'],),
   'DDL lock acquired on provider0')
	or diag "no lock; " . $provider_0->safe_psql($bdr_test_dbname, q[SELECT * FROM bdr.bdr_locks]);

ok($provider_1->safe_psql($bdr_test_dbname, q[SELECT 1 FROM bdr.bdr_locks WHERE owner_node_name = '] . $provider_0->name . q[' AND lock_mode = 'ddl_lock' AND lock_state = 'peer_confirmed'],),
   'DDL lock acquired on provider1')
	or diag "no lock; " . $provider_1->safe_psql($bdr_test_dbname, q[SELECT * FROM bdr.bdr_locks]);

# An alternative is to bring up pglogical first, then bdr on the downstream
# once pglogical is synced up. (We don't test that here yet, but it's the simpler/safer
# of the two anyway).

my ($dumpfh, $dumpfile) = tempfile('pgl_dump_XXXX', UNLINK => 1);

IPC::Run::run([
		'pg_dump',
		'-Fc',
		'-d', $provider_0->connstr($bdr_test_dbname),
		'--schema-only',
		'-N', 'pglogical',
		'-N', 'bdr',
		'-f', $dumpfile
	])
	or BAIL_OUT('error running manual schema dump');

for my $node (@$subscribers)
{
	IPC::Run::run([
		'pg_restore',
		'-d', $node->connstr($bdr_test_dbname) . q[ options='-c bdr.do_not_replicate=on -c bdr.skip_ddl_locking=on -c bdr.skip_ddl_replication=on -c bdr.permit_unsafe_ddl_commands=on'],
		$dumpfile
	])
	or BAIL_OUT('error running manual schema restore on ' . $node->name);
}

close($dumpfh);
unlink($dumpfile);

# We can release the DDL lock now
$stdin .= q[
ROLLBACK;
];
$lock_handle->finish;

# Establish a subscription with origin-forwarding enabled. pglogical2 doesn't
# implement origin selective forwarding, it's all or nothing.
#
# We cannot use pglogical structural synchronization because BDR doesn't support
# raw DDL replication and pglogical doesn't know how to do it. TODO. Gotta fix
# that otherwise they won't be able to sync up existing instance states.
#
# Workaround in the mean time will be to apply the schema like BDR does, to
# each node with do_not_replicate. Then sync individual tables. But we didn't
# pre-seed, so for now we can disable sync entirely.
#
# (We should probably offer a pg_dump option to wrap in
# bdr.replicate_ddl_command, or y'know just add ddl replication).
$subscriber_0->safe_psql($bdr_test_dbname,
	q[SELECT * FROM pglogical.create_subscription(
	  subscription_name := 'bdr_subscription',
	  provider_dsn := '] . $provider_0->connstr($bdr_test_dbname) . q[',
	  synchronize_structure := false,
	  synchronize_data := true,
	  forward_origins := '{all}');]);

note("created subscription, waiting for bdr apply");

# Ideally we'd like to make both subscribers bdr.bdr_node_set_read_only here.
# Handily, pglogical applies work at a lower level than BDR enforces read-only
# mode, so we can do just that.

for my $node (@$subscribers) {
	$node->safe_psql($bdr_test_dbname, q[SELECT bdr.bdr_node_set_read_only('] . $node->name . q[', true);]);
}

$subscriber_0->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);
note("bdr caught up subscriber");

is($subscriber_1->safe_psql($bdr_test_dbname, q[SELECT 1 FROM pglogical.node;]), '', 'pglogical node did not replicate to subscriber_1');

# These were synced by our manual schema sync, since synchronize_structure was off.
is($subscriber_0->safe_psql($bdr_test_dbname, q[ SELECT 1 FROM pg_class WHERE relname = 'preseed_in';]),
   '1', 'preseed table found on subscriber_0');
is($subscriber_1->safe_psql($bdr_test_dbname, q[ SELECT 1 FROM pg_class WHERE relname = 'preseed_in';]),
   '1', 'preseed table found on subscriber_1');
is($subscriber_0->safe_psql($bdr_test_dbname, q[ SELECT 1 FROM pg_class WHERE relname = 'preseed_ex';]),
   '1', 'preseed table found on subscriber_0');
is($subscriber_1->safe_psql($bdr_test_dbname, q[ SELECT 1 FROM pg_class WHERE relname = 'preseed_ex';]),
   '1', 'preseed table found on subscriber_1');

my $preseed_expected = "1|provider_0\n2|provider_1";
for my $node (@$providers, @$subscribers) {
	is($node->safe_psql($bdr_test_dbname, q[ SELECT * FROM preseed_in ORDER BY id ]),
	   $preseed_expected, 'preseed_in table contents synced on ' . $node->name);
}

for my $node (@$providers) {
	is($node->safe_psql($bdr_test_dbname, q[ SELECT * FROM preseed_ex ORDER BY id ]),
	   $preseed_expected, 'preseed_ex table contents correct on ' . $node->name);
}
# preseed_ex should be empty on downstream since it's not in a repset
for my $node (@$subscribers) {
	is($node->safe_psql($bdr_test_dbname, q[ SELECT * FROM preseed_ex ORDER BY id ]),
	   '', 'preseed_ex table contents empty on ' . $node->name);
}

# Add preseed_ex to repset and manually resync it
$provider_0->safe_psql($bdr_test_dbname, q[SELECT * FROM pglogical.replication_set_add_table('default', 'preseed_ex')]);
$subscriber_0->safe_psql($bdr_test_dbname, q[SELECT * FROM pglogical.alter_subscription_resynchronize_table('bdr_subscription', 'preseed_ex')]);
note "waiting for resync to complete";
$subscriber_0->poll_query_until($bdr_test_dbname, q[SELECT EXISTS (SELECT 1 FROM pglogical.local_sync_status WHERE sync_status IN ('y', 'r') AND sync_relname = 'preseed_ex')])
	or diag "resync of preseed_ex failed";

for my $node (@$subscribers) {
	is($node->safe_psql($bdr_test_dbname, q[ SELECT * FROM preseed_ex ORDER BY id ]),
	   $preseed_expected, 'preseed_ex table contents correct on ' . $node->name . " after resync");
}

# Initial subscribe and table sync
note "waiting for pgl status replicating";
$subscriber_0->poll_query_until($bdr_test_dbname, q[SELECT EXISTS (SELECT 1 FROM pglogical.show_subscription_status() WHERE status = 'replicating')])
	or BAIL_OUT('failed to achieve status=replicating');
note "waiting for data sync";
$subscriber_0->poll_query_until($bdr_test_dbname, q[SELECT EXISTS (SELECT 1 FROM pglogical.local_sync_status WHERE sync_status IN ('y', 'r') AND sync_relname IS NULL AND sync_nspname IS NULL)])
	or BAIL_OUT('failed to achieve data sync');

is($subscriber_0->safe_psql($bdr_test_dbname, q[SELECT node_sysid, node_timeline, node_dboid, node_name FROM bdr.bdr_nodes ORDER BY 1,2,3,4]),
   $presubscribe_bdr_nodes,
   "bdr.bdr_nodes unchanged on subscriber after pgl subscribe");

$provider_0->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);

# Make sure DDL locking works on upstream and on downstream
my $timedout = 0;
my $ret = $provider_0->psql($bdr_test_dbname,
	q[SELECT bdr.acquire_global_lock('ddl_lock');],
	timed_out => \$timedout, timeout => 10);
is($ret, 0, 'DDL lock succeeded on provider with node up');
is($timedout, 0, 'DDL lock acquisition on provider did not time out with node up');

$ret = $subscriber_0->psql($bdr_test_dbname,
	q[SELECT bdr.acquire_global_lock('ddl_lock');],
	timed_out => \$timedout, timeout => 10);
is($ret, 0, 'DDL lock succeeded on subscriber with node up');
is($timedout, 0, 'DDL lock acquisition on subscriber did not time out with node up');

my $tbl_ddl = q[
SELECT pglogical.replicate_ddl_command($PGLDDL$
SELECT bdr.bdr_replicate_ddl_command($DDL$

CREATE SEQUENCE public.pgl_bdr_test_bdr_seq_test_seq;

CREATE TABLE public.pgl_bdr_test(
	id integer primary key,
	dummy text,
	bdr_seq_test bigint not null default bdr.global_seq_nextval('public.pgl_bdr_test_bdr_seq_test_seq'::regclass),
	local_seq_test bigserial not null
);

ALTER SEQUENCE public.pgl_bdr_test_bdr_seq_test_seq OWNED BY public.pgl_bdr_test.bdr_seq_test;

$DDL$);
$PGLDDL$);
];

$provider_0->safe_psql($bdr_test_dbname, $tbl_ddl);

$provider_0->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);

is($provider_0->safe_psql($bdr_test_dbname, q[ SELECT 1 FROM pg_class WHERE relname = 'pgl_bdr_test';]),
   1, 'pgl_bdr_test exists on provider_0');

is($provider_1->safe_psql($bdr_test_dbname, q[ SELECT 1 FROM pg_class WHERE relname = 'pgl_bdr_test';]),
   1, 'pgl_bdr_test exists on provider_1');

is($subscriber_0->safe_psql($bdr_test_dbname, q[ SELECT 1 FROM pg_class WHERE relname = 'pgl_bdr_test';]),
   '1', 'pgl_bdr_test exists on subscriber_0');

# Add a table to default provider repset locally; we don't try to replicate
# this to our peers. We probably could do so with pglogical.replicate_ddl_command
# but ... why?
$provider_0->safe_psql($bdr_test_dbname, q[SELECT * FROM pglogical.replication_set_add_table('default', 'pgl_bdr_test')]);

# OK, we should have tables on both, test writes. Lets make sure everything
# lands ok. We'll temporarily allow writes on the subscribers for the purpose.
for my $node (@$subscribers)
{
	$node->safe_psql($bdr_test_dbname, q[SELECT bdr.bdr_node_set_read_only('] . $node->name . q[', false);]);
}

$subscriber_0->safe_psql($bdr_test_dbname, q[INSERT INTO pgl_bdr_test(id, dummy) VALUES (3, 'subscriber_0');]);
$subscriber_1->safe_psql($bdr_test_dbname, q[INSERT INTO pgl_bdr_test(id, dummy) VALUES (4, 'subscriber_1');]);

for my $node (@$subscribers)
{
	$node->safe_psql($bdr_test_dbname, q[SELECT bdr.bdr_node_set_read_only('] . $node->name . q[', true);]);
}

$provider_0->safe_psql($bdr_test_dbname, q[INSERT INTO pgl_bdr_test(id, dummy) VALUES (1, 'provider_0');]);
$provider_1->safe_psql($bdr_test_dbname, q[INSERT INTO pgl_bdr_test(id, dummy) VALUES (2, 'provider_1');]);

$provider_0->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);
$provider_1->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);
$subscriber_0->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);
$subscriber_1->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);

is($provider_0->safe_psql($bdr_test_dbname, q[SELECT id, dummy FROM pgl_bdr_test ORDER BY id]),
	qq[1|provider_0\n2|provider_1],
	'provider0 has provider-inserted rows');
is($provider_1->safe_psql($bdr_test_dbname, q[SELECT id, dummy FROM pgl_bdr_test ORDER BY id]),
	qq[1|provider_0\n2|provider_1],
	'provider1 has provider-inserted rows');
is($subscriber_0->safe_psql($bdr_test_dbname, q[SELECT id, dummy FROM pgl_bdr_test ORDER BY id]),
	qq[1|provider_0\n2|provider_1\n3|subscriber_0\n4|subscriber_1],
	'subscriber0 has provider- and subscriber-inserted rows');
is($subscriber_1->safe_psql($bdr_test_dbname, q[SELECT id, dummy FROM pgl_bdr_test ORDER BY id]),
	qq[1|provider_0\n2|provider_1\n3|subscriber_0\n4|subscriber_1],
	'subscriber1 has provider- and subscriber-inserted rows');

# Sequence checks
my $bdr_seq_vals = $provider_0->safe_psql($bdr_test_dbname, q[SELECT bdr_seq_test FROM pgl_bdr_test ORDER BY id]);
my $local_seq_vals = $provider_0->safe_psql($bdr_test_dbname, q[SELECT local_seq_test FROM pgl_bdr_test ORDER BY id]);

for my $node (@$providers, @$subscribers) {
	is($node->safe_psql($bdr_test_dbname, q[SELECT bdr_seq_test FROM pgl_bdr_test WHERE dummy LIKE 'provider%' ORDER BY id]),
	   $bdr_seq_vals, 'bdr sequence generated values match on ' . $node->name);
	is($node->safe_psql($bdr_test_dbname, q[SELECT bdr_seq_test FROM pgl_bdr_test WHERE dummy LIKE 'provider%' ORDER BY id]),
	   $bdr_seq_vals, 'local sequence generated values match on ' . $node->name);
}

# Can we wrap bdr.replicate_ddl_command in pglogical.replicate_ddl_command?

$provider_0->safe_psql($bdr_test_dbname, q[
SELECT pglogical.replicate_ddl_command($PGLDDL$
SELECT bdr.bdr_replicate_ddl_command($BDRDDL$
CREATE TABLE public.multiddl(id integer primary key);
$BDRDDL$);
$PGLDDL$);
]);

$provider_0->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);
$subscriber_0->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);

#diag $provider_0->safe_psql($bdr_test_dbname, "SELECT * FROM bdr.bdr_queued_commands ORDER BY queued_at DESC, lsn DESC LIMIT 10");
#diag $provider_0->safe_psql($bdr_test_dbname, "SELECT * FROM pglogical.queue ORDER BY queued_at DESC LIMIT 10");
#diag $subscriber_0->safe_psql($bdr_test_dbname, "SELECT * FROM bdr.bdr_queued_commands ORDER BY queued_at DESC, lsn DESC LIMIT 10");
#diag $subscriber_0->safe_psql($bdr_test_dbname, "SELECT * FROM pglogical.queue ORDER BY queued_at DESC LIMIT 10");

for my $node (@$providers, @$subscribers) {
	is($node->safe_psql($bdr_test_dbname, q[ SELECT 1 FROM pg_class WHERE relname = 'multiddl';]),
	   1, 'multiddl exists on ' . $node->name);
}

# Woah, how cool is that?

# But it won't work if you do them in that order on provider1:
$provider_1->safe_psql($bdr_test_dbname, q[
SELECT pglogical.replicate_ddl_command($PGLDDL$
SELECT bdr.bdr_replicate_ddl_command($BDRDDL$
CREATE TABLE public.multiddl2(id integer primary key);
$BDRDDL$);
$PGLDDL$);
]);

$provider_0->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);
$subscriber_0->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);

for my $node (@$providers, @$subscribers) {
	is($node->safe_psql($bdr_test_dbname, q[ SELECT 1 FROM pg_class WHERE relname = 'multiddl2';]),
	   1, 'multiddl2 exists on ' . $node->name);
}

# Note that you can't nest the other way around, or stuff breaks. It doesn't
# matter which upstream node you queue the ddl on though, because
# pglogical.queue is replicated by BDR on the upstream side.

# But what if we make pgl replicate the bdr ddl queue instead? After all, it
# doesn't care if it knows about the origin nodes, does it?
#
$provider_0->safe_psql($bdr_test_dbname, q[SELECT * FROM pglogical.replication_set_add_table('default_insert_only', 'bdr.bdr_queued_commands')]);
$provider_0->safe_psql($bdr_test_dbname, q[SELECT * FROM pglogical.replication_set_add_table('default_insert_only', 'bdr.bdr_queued_drops')]);

$provider_0->safe_psql($bdr_test_dbname, q[
SELECT bdr.bdr_replicate_ddl_command($BDRDDL$
CREATE TABLE public.replicate_bdr_ddl(id integer primary key);
$BDRDDL$);
]);

$provider_0->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);
$subscriber_0->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);

TODO: {
	local $TODO = 'known to fail on subscriber0 because pgl does not examine bdr queue';

	for my $node (@$providers, @$subscribers) {
		is($node->safe_psql($bdr_test_dbname, q[ SELECT 1 FROM pg_class WHERE relname = 'replicate_bdr_ddl';]),
		   1, 'replicate_bdr_ddl exists on ' . $node->name);
	}
}

# Even better. Or it would be if it worked. But pglogical apply doesn't know
# about the bdr.bdr_queued_commands, and bdr only examines that during apply.
# We'd have to expose the bdr callbacks to pgl.

# Fixup for desync caused above. Permit unsafe to override read-only mode.
$subscriber_0->safe_psql($bdr_test_dbname, q[
BEGIN;
SET LOCAL bdr.skip_ddl_replication = on;
SET LOCAL bdr.permit_unsafe_ddl_commands = on;
CREATE TABLE public.replicate_bdr_ddl(id integer primary key);
COMMIT;
]);

$subscriber_0->safe_psql($bdr_test_dbname, q[SELECT pglogical.wait_slot_confirm_lsn(NULL, NULL);]);

# Now we want to promote the subscriber and tear down the provider. Doing it consistently is important,
# we don't want to lose commits, so we'll
#
# * Take the ddl lock in 'write' mode on provider, ensuring all provider queues are flushed to provider0
#   and no new txns can commit;
# * Wait for provider0 to flush to subscriber0
# * halt providers
# * clear read-only mode on subscribers

# Take provider ddl lock
$lock_handle = IPC::Run::start(['psql', '-qAtX', '-d', $provider_0->connstr($bdr_test_dbname)],
	'<', \$stdin,
	'>', \$stdout,
	'2>', \$stderr);

$stdin .= q[
BEGIN;
SELECT bdr.acquire_global_lock('write_lock');
SELECT 1;
];

do { $lock_handle->pump; print $stdout; } until $stdout =~ /1[\r\n]$/;

# Wait for flush to subscribers
$provider_0->safe_psql($bdr_test_dbname, q[SELECT bdr.wait_slot_confirm_lsn(NULL, NULL);]);

# Stop providers, terminating the one holding the ddl lock last
$provider_1->stop;
$provider_0->stop;

# and enable read/write on subscribers
for my $node (@$subscribers) {
	$node->safe_psql($bdr_test_dbname, q[SELECT bdr.bdr_node_set_read_only('] . $node->name . q[', false);]);
}

# locker should've died, but make sure
$lock_handle->kill_kill;

# Promoted!
pass("promoted subscriber");

$subscriber_0->safe_psql($bdr_test_dbname, q[INSERT INTO pgl_bdr_test(id, dummy) VALUES (10, 'promoted_subscriber_0');]);
$subscriber_1->safe_psql($bdr_test_dbname, q[INSERT INTO pgl_bdr_test(id, dummy) VALUES (11, 'promoted_subscriber_1');]);

is($subscriber_0->safe_psql($bdr_test_dbname, q[SELECT id, dummy FROM pgl_bdr_test ORDER BY id]),
	qq[1|provider_0\n2|provider_1\n3|subscriber_0\n4|subscriber_1\n10|promoted_subscriber_0\n11|promoted_subscriber_1],
	'subscriber0 has provider- and subscriber-inserted rows including promoted');
is($subscriber_1->safe_psql($bdr_test_dbname, q[SELECT id, dummy FROM pgl_bdr_test ORDER BY id]),
	qq[1|provider_0\n2|provider_1\n3|subscriber_0\n4|subscriber_1\n10|promoted_subscriber_0\n11|promoted_subscriber_1],
	'subscriber1 has provider- and subscriber-inserted rows including promoted');

done_testing();
