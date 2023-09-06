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
use IPC::Run;
use Test::More;
use utils::nodemanagement;

# Create an upstream node and bring up pgactive
my $alpha = 'alpha';
my $node_0 = create_pgactive_group_with_db('node_0', $alpha);

# Join pgactive group 1 on postgres cluster 2 with database alpha
my $node_1 = join_pgactive_group_with_db('node_1', $node_0, $alpha);

create_and_check_data($node_0, $node_1, $alpha);

my $pg_version = $node_1->safe_psql($alpha,
    q[select setting::int/10000 from pg_settings where name = 'server_version_num';]);

my $backupdir = $node_0->backup_dir;
my $plain = "$backupdir/plain.sql";

# Take pg_dump of a pgactive node.
#
# pgactive uses security labels, so we will have to generate dump without security
# labels, otherwise restored database will have command attaching pgactive security
# label.
#
# We'll need to exclude pgactive schema from dumping to not get pgactive catalogs.
#
# We'll need to specify only the needed extensions so that pgactive extension can be
# excluded from dump. Otherwise, pgactive extension needs to be dropped immediately
# after restore. pg_dump option to selectively dump extensions (--extension) is
# introduced in PG 14, prior to that one has to delete create extension pgactive
# statement from the dump manually.
if ($pg_version >= 14)
{
    $node_0->command_ok(
        [
            'pg_dump', '--file', $plain, '--no-security-labels',
            '--exclude-schema', 'pgactive', '--create',
            '--dbname', $alpha,
            '--extension', 'plpgsql'
        ],
        'pg_dump of a pgactive node');
}
else
{
    $node_0->command_ok(
        [
            'pg_dump', '--file', $plain, '--no-security-labels',
            '--exclude-schema', 'pgactive', '--create',
            '--dbname', $alpha
        ],
        'pg_dump of a pgactive node');

    # Delete all lines that contain "pgactive"
    system("sed -i.bak '/pgactive/d' $plain");
}

my $node_2 = PostgreSQL::Test::Cluster->new('node_2');
$node_2->init;
$node_2->start;

$node_2->command_ok(['psql', '-f', $plain],
	'pg_restore from a pg_dump of pgactive node succeeds');

# pgactive extension mustn't exist on restored node
my $res = $node_2->safe_psql($alpha,
    qq[SELECT COUNT(*) = 0 FROM pg_extension WHERE extname = 'pgactive';]);
is($res, 't', "pgactive extension doesn't exist on pg_restore-d node " . $node_2->name() ."");

$res = $node_2->safe_psql($alpha, qq[SELECT COUNT(*) FROM fruits;]);
is($res, '2', "pg_restore-d node " . $node_2->name() . "has all the data");

done_testing();
