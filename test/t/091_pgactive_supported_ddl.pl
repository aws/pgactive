#!/usr/bin/env perl
use strict;
use warnings;
use lib 'test/t/';
use Cwd;
use Config;
use Carp;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use threads;
use Test::More;
use utils::nodemanagement;

my ($ret, $stdout, $stderr);

# Create an upstream node and bring up pgactive
my $node_a = PostgreSQL::Test::Cluster->new('node_a');
initandstart_pgactive_group($node_a);
my $upstream_node = $node_a;

# Create a test table
my $table_name = "ddl_test";
exec_ddl($node_a,qq{ CREATE TABLE public.$table_name(pk int primary key, dropping_col1 text, dropping_col2 text);});

# Insert some data
$node_a->safe_psql($pgactive_test_dbname,qq{INSERT INTO public.$table_name VALUES (1,'to drop', 'not dropped');});

# Drop a column
exec_ddl($node_a,qq{ ALTER TABLE public.$table_name DROP COLUMN dropping_col1;});

# Join a new node to first node using pgactive_join_group
my $node_b = PostgreSQL::Test::Cluster->new('node_b');
initandstart_logicaljoin_node($node_b,$node_a);

# Check that the data is what we want
is($node_b->safe_psql($pgactive_test_dbname, qq{SELECT * FROM public.$table_name}), '1|not dropped', 'SELECT ok with dropped column');

# Add a column
exec_ddl($node_a,qq{ALTER TABLE public.$table_name ADD COLUMN col1 text;});

# Make sure everything caught up by forcing another lock
$node_a->safe_psql($pgactive_test_dbname, q[SELECT pgactive.pgactive_acquire_global_lock('write_lock')]);

# Check that the data is what we want
is($node_b->safe_psql($pgactive_test_dbname, qq{SELECT * FROM public.$table_name}), '1|not dropped|', 'SELECT ok with added column');

# Remove the data
$node_a->safe_psql($pgactive_test_dbname,qq{DELETE FROM public.$table_name;});

# Alter a column to be not null
exec_ddl($node_a,qq{ ALTER TABLE public.$table_name ALTER COLUMN col1 SET NOT NULL;});

# Make sure everything caught up by forcing another lock
$node_a->safe_psql($pgactive_test_dbname, q[SELECT pgactive.pgactive_acquire_global_lock('write_lock')]);

# Try to insert NULL value
($ret, $stdout, $stderr) = $node_b->psql($pgactive_test_dbname,
qq{ INSERT INTO public.$table_name(pk, dropping_col2) values (1,'not null');});
like($stderr, qr/violates not-null constraint/, "SET NOT NULL works as expected");

# Drop the NOT NULL constraint
exec_ddl($node_a,qq{ ALTER TABLE public.$table_name ALTER COLUMN col1 DROP NOT NULL;});

# Make sure everything caught up by forcing another lock
$node_a->safe_psql($pgactive_test_dbname, q[SELECT pgactive.pgactive_acquire_global_lock('write_lock')]);

# Try to insert NULL value
$node_b->safe_psql($pgactive_test_dbname, qq{ INSERT INTO public.$table_name(pk, dropping_col2) values (1,'not null');});

# Check that the data is what we want
is($node_b->safe_psql($pgactive_test_dbname, qq{SELECT * FROM public.$table_name}), '1|not null|', 'DROP NOT NULL works as expected');

# Remove the data
$node_a->safe_psql($pgactive_test_dbname,qq{DELETE FROM public.$table_name;});

# Set a default value
exec_ddl($node_a,qq{ ALTER TABLE public.$table_name ALTER COLUMN col1 SET DEFAULT 'abc';});

# Make sure everything caught up by forcing another lock
$node_a->safe_psql($pgactive_test_dbname, q[SELECT pgactive.pgactive_acquire_global_lock('write_lock')]);

# Try to insert NULL value
$node_b->safe_psql($pgactive_test_dbname, qq{ INSERT INTO public.$table_name(pk, dropping_col2) values (1,'not null');});

# Check that the data is what we want
is($node_b->safe_psql($pgactive_test_dbname, qq{SELECT * FROM public.$table_name}), '1|not null|abc', 'SET DEFAULT works as expected');

# Remove the data
$node_a->safe_psql($pgactive_test_dbname,qq{DELETE FROM public.$table_name;});

# Drop the default value
exec_ddl($node_a,qq{ ALTER TABLE public.$table_name ALTER COLUMN col1 DROP DEFAULT;});

# Make sure everything caught up by forcing another lock
$node_a->safe_psql($pgactive_test_dbname, q[SELECT pgactive.pgactive_acquire_global_lock('write_lock')]);

# Try to insert NULL value
$node_b->safe_psql($pgactive_test_dbname, qq{ INSERT INTO public.$table_name(pk, dropping_col2) values (1,'not null');});

# Check that the data is what we want
is($node_b->safe_psql($pgactive_test_dbname, qq{SELECT * FROM public.$table_name}), '1|not null|', 'DROP DEFAULT works as expected');

# Remove the data
$node_a->safe_psql($pgactive_test_dbname,qq{DELETE FROM public.$table_name;});

# Add a check constraint
exec_ddl($node_a,qq{ ALTER TABLE public.$table_name ADD CONSTRAINT col1_check CHECK ( col1 in ('abc'));});

# Make sure everything caught up by forcing another lock
$node_a->safe_psql($pgactive_test_dbname, q[SELECT pgactive.pgactive_acquire_global_lock('write_lock')]);

# Try to insert value not satisfying the constraint
($ret, $stdout, $stderr) = $node_b->psql($pgactive_test_dbname,
qq{ INSERT INTO public.$table_name(pk, dropping_col2, col1) values (1,'not null', 'efg');});
like($stderr, qr/violates check constraint/, "CHECK constraint works as expected");

# Drop the check constraint
exec_ddl($node_a,qq{ ALTER TABLE public.$table_name DROP CONSTRAINT col1_check;});

# Make sure everything caught up by forcing another lock
$node_a->safe_psql($pgactive_test_dbname, q[SELECT pgactive.pgactive_acquire_global_lock('write_lock')]);

# Try to insert value not satisfying the previous constraint
$node_b->safe_psql($pgactive_test_dbname, qq{INSERT INTO public.$table_name(pk, dropping_col2, col1) values (1,'not null', 'efg');});

# Check that the data is what we want
is($node_b->safe_psql($pgactive_test_dbname, qq{SELECT * FROM public.$table_name}), '1|not null|efg', 'DROP constraints works as expected');

# Remove the data
$node_a->safe_psql($pgactive_test_dbname,qq{DELETE FROM public.$table_name;});

# Add a REPLICA IDENTITY
exec_ddl($node_a,qq{ ALTER TABLE public.$table_name ADD COLUMN col2 text NOT NULL ;});
exec_ddl($node_a,qq{ CREATE UNIQUE INDEX test_idx1 ON public.$table_name(col2);});
exec_ddl($node_a,qq{ ALTER TABLE public.$table_name REPLICA IDENTITY USING INDEX test_idx1;});

# Make sure everything caught up by forcing another lock
$node_a->safe_psql($pgactive_test_dbname, q[SELECT pgactive.pgactive_acquire_global_lock('write_lock')]);

# Check that the catalog is what we want
is($node_b->safe_psql($pgactive_test_dbname, qq{SELECT indisreplident FROM pg_index WHERE indrelid = 'ddl_test'::regclass AND indexrelid = 'test_idx1'::regclass;}), 't', 'REPLICA IDENTITY works as expected');

# Rename a column
exec_ddl($node_a,qq{ ALTER TABLE public.$table_name RENAME COLUMN dropping_col2 to keep_col2;});

# Make sure everything caught up by forcing another lock
$node_a->safe_psql($pgactive_test_dbname, q[SELECT pgactive.pgactive_acquire_global_lock('write_lock')]);

# Try to insert value
$node_b->safe_psql($pgactive_test_dbname, qq{INSERT INTO public.$table_name(pk, keep_col2, col1, col2) values (1,'not null', 'efg', 'abc');});

# Check that the data is what we want
is($node_b->safe_psql($pgactive_test_dbname, qq{SELECT * FROM public.$table_name}), '1|not null|efg|abc', 'RENAME column works as expected');

done_testing();
