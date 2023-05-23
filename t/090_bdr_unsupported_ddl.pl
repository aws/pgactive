#!/usr/bin/env perl
use strict;
use warnings;
use lib 't/';
use Cwd;
use Config;
use Carp;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use threads;
use Test::More;
use utils::nodemanagement;

# Create an upstream node and bring up bdr
my $node_a = PostgreSQL::Test::Cluster->new('node_a');
initandstart_bdr_group($node_a);
my $upstream_node = $node_a;

# Create a test table 
my $table_name = "ddl_test";
create_table($node_a,$table_name);

# Join a new node to first node using bdr_group_join
my $node_b = PostgreSQL::Test::Cluster->new('node_b');
initandstart_logicaljoin_node($node_b,$node_a);
# Add new column and constraint to test ALTER CONSTRAINT and ALTER COLUMN ..TYPE
$node_a->safe_psql($bdr_test_dbname,qq{SELECT bdr.bdr_replicate_ddl_command(\$DDL\$ ALTER TABLE public.$table_name ADD DateOfBirth date;\$DDL\$);});

# Test showing that bdr.bdr_replicate_ddl_command(...) 
# correctly rejects disallowed commands like 
# ALTER TABLE ... ADD COLUMN ... DEFAULT NOT NULL

my $ddl_statement = qq{SELECT bdr.bdr_replicate_ddl_command(\$DDL\$ ALTER TABLE public.$table_name ADD COLUMN name text DEFAULT 'ABC';\$DDL\$);};
my $error_msg = qq(ALTER TABLE ... ADD COLUMN ... DEFAULT may only affect UNLOGGED or TEMPORARY tables when BDR is active; $table_name is a regular table);
ddl_fail($node_b,$ddl_statement,$error_msg,"ALTER TABLE..ADD COLUMN..DEFAULT");

$ddl_statement = qq{SELECT bdr.bdr_replicate_ddl_command(\$DDL\$ ALTER TABLE public.$table_name ADD COLUMN addr text DEFAULT 'ABC' NOT NULL;\$DDL\$);};
$error_msg = qq(ALTER TABLE ... ADD COLUMN ... DEFAULT may only affect UNLOGGED or TEMPORARY tables when BDR is active; $table_name is a regular table);
ddl_fail($node_b,$ddl_statement,$error_msg,"ALTER TABLE..ADD COLUMN..DEFAULT.. NOT NULL");

$ddl_statement = qq{SELECT bdr.bdr_replicate_ddl_command(\$DDL\$ ALTER TABLE public.$table_name ADD CONSTRAINT test EXCLUDE USING gist(id with =);\$DDL\$);};
$error_msg = "ALTER TABLE ... ADD CONSTRAINT ... EXCLUDE may only affect UNLOGGED or TEMPORARY tables when BDR is active";
ddl_fail($node_b,$ddl_statement,$error_msg,"ALTER TABLE..ADD CONSTRAINT..EXCLUDE");

$ddl_statement = qq{SELECT bdr.bdr_replicate_ddl_command(\$DDL\$ CREATE TABLE public.test(id integer, EXCLUDE USING gist(id with =));\$DDL\$);};
$error_msg = "EXCLUDE constraints are unsafe with BDR active";
ddl_fail($node_b,$ddl_statement,$error_msg,"CREATE TABLE...EXCLUDE");

$ddl_statement = qq{SELECT bdr.bdr_replicate_ddl_command(\$DDL\$ ALTER TABLE public.$table_name ALTER COLUMN DateOfBirth TYPE year;\$DDL\$);};
$error_msg = "ALTER TABLE ... ALTER COLUMN TYPE may only affect UNLOGGED or TEMPORARY tables when BDR is active; $table_name is a regular table";
ddl_fail($node_b,$ddl_statement,$error_msg,"ALTER TABLE...ALTER COLUMN TYPE");

$ddl_statement = qq{SELECT bdr.bdr_replicate_ddl_command(\$DDL\$ BEGIN; CREATE TABLE public.test_commit(id integer);COMMIT; \$DDL\$);};
$error_msg = "cannot COMMIT, ROLLBACK or PREPARE TRANSACTION in bdr_replicate_ddl_command";
ddl_fail($node_b,$ddl_statement,$error_msg,"Multistatement ddl with embedded COMMIT");

$ddl_statement = qq{SELECT bdr.bdr_replicate_ddl_command(\$DDL\$ ROLLBACK; \$DDL\$);};
$error_msg = "cannot COMMIT, ROLLBACK or PREPARE TRANSACTION in bdr_replicate_ddl_command";
ddl_fail($node_b,$ddl_statement,$error_msg,"Multistatement ddl with embedded ROLLBACK");

$ddl_statement = qq{SELECT bdr.bdr_replicate_ddl_command(\$DDL\$ PREPARE TRANSACTION 'x'; \$DDL\$);};
$error_msg = "cannot COMMIT, ROLLBACK or PREPARE TRANSACTION in bdr_replicate_ddl_command";
ddl_fail($node_b,$ddl_statement,$error_msg,"Multistatement ddl with embedded PREPARE TRANSACTION");

sub ddl_fail {
    my ($node, $ddl_statement, $error_msg, $test_case) = @_;
	my ($ret, $stdout, $stderr) = $node->psql($bdr_test_dbname,$ddl_statement);
    is($ret, 3, 'psql exited with error state');
	like($stderr,qr/$error_msg/, "psql error message $test_case");
}

done_testing();
