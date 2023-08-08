#!/usr/bin/env perl
#
# Test triggers and truncate when DDL replication is off
use strict;
use warnings;
use lib 't/';
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use IPC::Run;
use Test::More;
use utils::nodemanagement qw(
  :DEFAULT
  generate_bdr_logical_join_query
  bdr_update_postgresql_conf
);

# Create an upstream node and bring up bdr
my $node_a = PostgreSQL::Test::Cluster->new('node_a');
my $node_b = PostgreSQL::Test::Cluster->new('node_b');

for my $node ($node_a, $node_b)
{
	$node->init();
	bdr_update_postgresql_conf($node);
	$node->append_conf('postgresql.conf', q{bdr.skip_ddl_replication = true});
	$node->start;

	$node->safe_psql('postgres', qq{CREATE DATABASE $bdr_test_dbname;});
	$node->safe_psql($bdr_test_dbname, q{CREATE EXTENSION bdr;});
}

# create a bdr group
create_bdr_group($node_a);

# join the bdr group
bdr_logical_join($node_b, $node_a);
check_join_status($node_b, $node_a);

# Create a table on both nodes
for my $node ($node_a, $node_b)
{
	$node->safe_psql($bdr_test_dbname,
		q{CREATE TABLE trig_test(a int primary key, b int);});
	$node->safe_psql($bdr_test_dbname,
		q{CREATE TABLE trig_test2(a int primary key, b int);});
}

wait_for_apply($node_a, $node_b);
wait_for_apply($node_b, $node_a);
# Insert and ensure it is replicated
$node_a->safe_psql($bdr_test_dbname, q{insert into trig_test values(1,1)});
wait_for_apply($node_a, $node_b);
is($node_b->safe_psql($bdr_test_dbname, 'SELECT a,b FROM trig_test;'),
	'1|1', "initial insert successfully replicated");

# BEFORE INSERT CASE

# Create the same trigger on both nodes
for my $node ($node_a, $node_b)
{
	$node->safe_psql(
		$bdr_test_dbname, q{CREATE FUNCTION trigtest() returns trigger as $$
										 BEGIN
											INSERT INTO trig_test2(a, b) values(new.a, new.b);
											return new;
										  END;$$ language plpgsql;
										 });
	$node->safe_psql(
		$bdr_test_dbname, q{CREATE TRIGGER test_tg BEFORE INSERT ON trig_test
										 FOR EACH ROW EXECUTE PROCEDURE trigtest();
										 });
}

wait_for_apply($node_a, $node_b);
wait_for_apply($node_b, $node_a);
# Insert and ensure it is replicated
$node_a->safe_psql($bdr_test_dbname, q{insert into trig_test values(2,2)});
wait_for_apply($node_a, $node_b);
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 2;'),
	'2|2',
	"insert due to remote before insert trigger");

# Create a trigger on one node only (should not be done!)
$node_b->safe_psql(
	$bdr_test_dbname, q{CREATE FUNCTION trigtest2() returns trigger as $$
										 BEGIN
											INSERT INTO trig_test2(a, b) values(new.a + 50, new.b + 150);
											return new;
										 END;$$ language plpgsql;
										 });
$node_b->safe_psql(
	$bdr_test_dbname, q{CREATE TRIGGER test_tg2 BEFORE INSERT ON trig_test
										 FOR EACH ROW EXECUTE PROCEDURE trigtest2();
										 });

# check that the local trigger works
$node_b->safe_psql($bdr_test_dbname, q{insert into trig_test values(3,3)});
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 3;'),
	'3|3',
	"local before insert trigger");
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 53;'),
	'53|153',
	"local only before insert trigger");

# and that inserts have been replicated too
wait_for_apply($node_b, $node_a);
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test where a = 3;'),
	'3|3',
	"insert replicated");
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 3;'),
	'3|3',
	"insert due to remote before insert trigger");
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 53;'),
	'53|153',
	"insert due to remote only before insert trigger");

# on the other way around check that local trigger is not triggered
$node_a->safe_psql($bdr_test_dbname, q{insert into trig_test values(4,4)});
wait_for_apply($node_a, $node_b);
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 4;'),
	'4|4',
	"insert due to remote before insert trigger");
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT count(*) FROM trig_test2 where a = 54;'),
	'0',
	"local only before insert trigger not triggered");

# AFTER INSERT CASE

# Create the same trigger on both nodes
for my $node ($node_a, $node_b)
{
	# First drop previous trigger
	$node->safe_psql($bdr_test_dbname, q{DROP TRIGGER test_tg on trig_test;});

	# Some cleaning
	$node->safe_psql($bdr_test_dbname, q{TRUNCATE TABLE trig_test;});
	$node->safe_psql($bdr_test_dbname, q{TRUNCATE TABLE trig_test2;});

	# Create the trigger
	$node->safe_psql(
		$bdr_test_dbname, q{CREATE TRIGGER test_tg AFTER INSERT ON trig_test
										 FOR EACH ROW EXECUTE PROCEDURE trigtest();
										 });
}

wait_for_apply($node_a, $node_b);
wait_for_apply($node_b, $node_a);
# Drop trigger that was created on node_b only
$node_b->safe_psql($bdr_test_dbname, q{DROP TRIGGER test_tg2 ON trig_test;});

# Insert and ensure it is replicated
$node_a->safe_psql($bdr_test_dbname, q{insert into trig_test values(2,2)});
wait_for_apply($node_a, $node_b);
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 2;'),
	'2|2',
	"insert due to remote after insert trigger");

# Create a trigger on one node only (should not be done!)
$node_b->safe_psql(
	$bdr_test_dbname, q{CREATE TRIGGER test_tg2 AFTER INSERT ON trig_test
										 FOR EACH ROW EXECUTE PROCEDURE trigtest2();
										 });

# check that the local trigger works
$node_b->safe_psql($bdr_test_dbname, q{insert into trig_test values(3,3)});
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 3;'),
	'3|3',
	"local after insert trigger ok");
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 53;'),
	'53|153',
	"local only after insert trigger ok");

# and that inserts have been replicated too
wait_for_apply($node_b, $node_a);
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test where a = 3;'),
	'3|3',
	"insert replicated");
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 3;'),
	'3|3',
	"insert due to remote after insert trigger");
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 53;'),
	'53|153',
	"insert due to remote only after insert trigger");

# on the other way around check that local trigger is not triggered
$node_a->safe_psql($bdr_test_dbname, q{insert into trig_test values(4,4)});
wait_for_apply($node_a, $node_b);
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 4;'),
	'4|4',
	"insert due to remote after insert trigger");
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT count(*) FROM trig_test2 where a = 54;'),
	'0',
	"local only after insert trigger not triggered");


# BEFORE UPDATE CASE

# Create the same trigger on both nodes
for my $node ($node_a, $node_b)
{
	# First drop previous trigger
	$node->safe_psql($bdr_test_dbname, q{DROP TRIGGER test_tg on trig_test;});

	# Some cleaning
	$node->safe_psql($bdr_test_dbname, q{TRUNCATE TABLE trig_test;});
	$node->safe_psql($bdr_test_dbname, q{TRUNCATE TABLE trig_test2;});

	# Create the trigger
	$node->safe_psql(
		$bdr_test_dbname, q{CREATE TRIGGER test_tg BEFORE UPDATE ON trig_test
										 FOR EACH ROW EXECUTE PROCEDURE trigtest();
										 });
}

wait_for_apply($node_a, $node_b);
wait_for_apply($node_b, $node_a);
# Drop trigger that was created on node_b only
$node_b->safe_psql($bdr_test_dbname, q{DROP TRIGGER test_tg2 ON trig_test;});

# Insert, update and ensure it is replicated
$node_a->safe_psql($bdr_test_dbname, q{insert into trig_test values(2,2)});
$node_a->safe_psql($bdr_test_dbname, q{update trig_test set a=2, b=2});
wait_for_apply($node_a, $node_b);
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 2;'),
	'2|2',
	"insert due to remote before update trigger");

# Create a trigger on one node only (should not be done!)
$node_b->safe_psql(
	$bdr_test_dbname, q{CREATE TRIGGER test_tg2 BEFORE UPDATE ON trig_test
										 FOR EACH ROW EXECUTE PROCEDURE trigtest2();
										 });

# check that the local trigger works
$node_b->safe_psql($bdr_test_dbname, q{update trig_test set a=3, b=3});
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 3;'),
	'3|3',
	"local before update trigger");
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 53;'),
	'53|153',
	"local only before update trigger");

# and that inserts have been replicated too
wait_for_apply($node_b, $node_a);
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test where a = 3;'),
	'3|3',
	"update replicated");
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 3;'),
	'3|3',
	"insert due to remote before update trigger");
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 53;'),
	'53|153',
	"insert due to remote only before update trigger");

# on the other way around check that local trigger is not triggered
$node_a->safe_psql($bdr_test_dbname, q{update trig_test set a=4, b=4});
wait_for_apply($node_a, $node_b);
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 4;'),
	'4|4',
	"insert due to remote before update trigger");
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT count(*) FROM trig_test2 where a = 54;'),
	'0',
	"local only before update trigger not triggered");

# AFTER UPDATE CASE

# Create the same trigger on both nodes
for my $node ($node_a, $node_b)
{
	# First drop previous trigger
	$node->safe_psql($bdr_test_dbname, q{DROP TRIGGER test_tg on trig_test;});

	# Some cleaning
	$node->safe_psql($bdr_test_dbname, q{TRUNCATE TABLE trig_test;});
	$node->safe_psql($bdr_test_dbname, q{TRUNCATE TABLE trig_test2;});

	# Create the trigger
	$node->safe_psql(
		$bdr_test_dbname, q{CREATE TRIGGER test_tg AFTER UPDATE ON trig_test
										 FOR EACH ROW EXECUTE PROCEDURE trigtest();
										 });
}

wait_for_apply($node_a, $node_b);
wait_for_apply($node_b, $node_a);
# Drop trigger that was created on node_b only
$node_b->safe_psql($bdr_test_dbname, q{DROP TRIGGER test_tg2 ON trig_test;});

# Insert, update and ensure it is replicated
$node_a->safe_psql($bdr_test_dbname, q{insert into trig_test values(2,2)});
$node_a->safe_psql($bdr_test_dbname, q{update trig_test set a=2, b=2});
wait_for_apply($node_a, $node_b);
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 2;'),
	'2|2',
	"insert due to remote after update trigger");

# Create a trigger on one node only (should not be done!)
$node_b->safe_psql(
	$bdr_test_dbname, q{CREATE TRIGGER test_tg2 AFTER UPDATE ON trig_test
										 FOR EACH ROW EXECUTE PROCEDURE trigtest2();
										 });

# check that the local trigger works
$node_b->safe_psql($bdr_test_dbname, q{update trig_test set a=3, b=3});
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 3;'),
	'3|3',
	"local after update trigger");
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 53;'),
	'53|153',
	"local only after update trigger");

# and that inserts have been replicated too
wait_for_apply($node_b, $node_a);
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test where a = 3;'),
	'3|3',
	"update replicated");
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 3;'),
	'3|3',
	"insert due to remote after update trigger");
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 53;'),
	'53|153',
	"insert due to remote only after update trigger");

# on the other way around check that local trigger is not triggered
$node_a->safe_psql($bdr_test_dbname, q{update trig_test set a=4, b=4});
wait_for_apply($node_a, $node_b);
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 4;'),
	'4|4',
	"insert due to remote after update trigger");
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT count(*) FROM trig_test2 where a = 54;'),
	'0',
	"local only after update trigger not triggered");

# BEFORE DELETE CASE

# Create the same trigger on both nodes
for my $node ($node_a, $node_b)
{
	# First drop previous trigger
	$node->safe_psql($bdr_test_dbname, q{DROP TRIGGER test_tg on trig_test;});

	# Drop previous function
	$node->safe_psql($bdr_test_dbname, q{DROP FUNCTION trigtest();});

	# Some cleaning
	$node->safe_psql($bdr_test_dbname, q{TRUNCATE TABLE trig_test;});
	$node->safe_psql($bdr_test_dbname, q{TRUNCATE TABLE trig_test2;});

	# Create the function
	$node->safe_psql(
		$bdr_test_dbname, q{CREATE FUNCTION trigtest() returns trigger as $$
										 BEGIN
											INSERT INTO trig_test2(a, b) values(old.a, old.b);
											return old;
										  END;$$ language plpgsql;
										 });

	# Create the trigger
	$node->safe_psql(
		$bdr_test_dbname, q{CREATE TRIGGER test_tg BEFORE DELETE ON trig_test
										 FOR EACH ROW EXECUTE PROCEDURE trigtest();
										 });
}

wait_for_apply($node_a, $node_b);
wait_for_apply($node_b, $node_a);
# Drop trigger that was created on node_b only
$node_b->safe_psql($bdr_test_dbname, q{DROP TRIGGER test_tg2 ON trig_test;});

# Insert, update and ensure it is replicated
$node_a->safe_psql($bdr_test_dbname, q{insert into trig_test values(2,2)});
$node_a->safe_psql($bdr_test_dbname,
	q{delete from trig_test where a=2 and b=2});
wait_for_apply($node_a, $node_b);
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 2;'),
	'2|2',
	"insert due to remote before delete trigger");

# Create a trigger on one node only (should not be done!)
$node_b->safe_psql($bdr_test_dbname, q{DROP FUNCTION trigtest2();});
$node_b->safe_psql(
	$bdr_test_dbname, q{CREATE FUNCTION trigtest2() returns trigger as $$
										 BEGIN
											INSERT INTO trig_test2(a, b) values(old.a + 50, old.b + 150);
											return old;
										 END;$$ language plpgsql;
										 });
$node_b->safe_psql(
	$bdr_test_dbname, q{CREATE TRIGGER test_tg2 BEFORE DELETE ON trig_test
										 FOR EACH ROW EXECUTE PROCEDURE trigtest2();
										 });

# check that the local trigger works
$node_b->safe_psql($bdr_test_dbname, q{insert into trig_test values(3,3)});
$node_b->safe_psql($bdr_test_dbname,
	q{delete from trig_test where a=3 and b=3});
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 3;'),
	'3|3',
	"local before delete trigger");
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 53;'),
	'53|153',
	"local only before delete trigger");

# and that inserts have been replicated too
wait_for_apply($node_b, $node_a);
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT count(*) FROM trig_test where a = 3;'),
	'0',
	"delete replicated");
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 3;'),
	'3|3',
	"insert due to remote before delete trigger");
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 53;'),
	'53|153',
	"insert due to remote only before delete trigger");

# on the other way around check that local trigger is not triggered
$node_a->safe_psql($bdr_test_dbname, q{insert into trig_test values(4,4)});
$node_a->safe_psql($bdr_test_dbname,
	q{delete from trig_test where a=4 and b=4});
wait_for_apply($node_a, $node_b);
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 4;'),
	'4|4',
	"insert due to remote before delete trigger");
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT count(*) FROM trig_test2 where a = 54;'),
	'0',
	"local only before delete trigger not triggered");

# AFTER DELETE CASE

# Create the same trigger on both nodes
for my $node ($node_a, $node_b)
{
	# First drop previous trigger
	$node->safe_psql($bdr_test_dbname, q{DROP TRIGGER test_tg on trig_test;});

	# Some cleaning
	$node->safe_psql($bdr_test_dbname, q{TRUNCATE TABLE trig_test;});
	$node->safe_psql($bdr_test_dbname, q{TRUNCATE TABLE trig_test2;});

	# Create the trigger
	$node->safe_psql(
		$bdr_test_dbname, q{CREATE TRIGGER test_tg AFTER DELETE ON trig_test
										 FOR EACH ROW EXECUTE PROCEDURE trigtest();
										 });
}

wait_for_apply($node_a, $node_b);
wait_for_apply($node_b, $node_a);
# Drop trigger that was created on node_b only
$node_b->safe_psql($bdr_test_dbname, q{DROP TRIGGER test_tg2 ON trig_test;});

# Insert, update and ensure it is replicated
$node_a->safe_psql($bdr_test_dbname, q{insert into trig_test values(2,2)});
$node_a->safe_psql($bdr_test_dbname,
	q{delete from trig_test where a=2 and b=2});
wait_for_apply($node_a, $node_b);
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 2;'),
	'2|2',
	"insert due to remote after delete trigger");

# Create a trigger on one node only (should not be done!)
$node_b->safe_psql(
	$bdr_test_dbname, q{CREATE TRIGGER test_tg2 AFTER DELETE ON trig_test
										 FOR EACH ROW EXECUTE PROCEDURE trigtest2();
										 });

# check that the local trigger works
$node_b->safe_psql($bdr_test_dbname, q{insert into trig_test values(3,3)});
$node_b->safe_psql($bdr_test_dbname,
	q{delete from trig_test where a=3 and b=3});
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 3;'),
	'3|3',
	"local after delete trigger");
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 53;'),
	'53|153',
	"local after delete trigger");

# and that inserts have been replicated too
wait_for_apply($node_b, $node_a);
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT count(*) FROM trig_test where a = 3;'),
	'0',
	"delete replicated");
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 3;'),
	'3|3',
	"insert due to remote after delete trigger");
is( $node_a->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 53;'),
	'53|153',
	"insert due to remote only after delete trigger");

# on the other way around check that local trigger is not triggered
$node_a->safe_psql($bdr_test_dbname, q{insert into trig_test values(4,4)});
$node_a->safe_psql($bdr_test_dbname,
	q{delete from trig_test where a=4 and b=4});
wait_for_apply($node_a, $node_b);
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT a,b FROM trig_test2 where a = 4;'),
	'4|4',
	"insert due to remote after delete trigger");
is( $node_b->safe_psql(
		$bdr_test_dbname, 'SELECT count(*) FROM trig_test2 where a = 54;'),
	'0',
	"local only after delete trigger not triggered");

# Check TRUNCATE

# Truncate not replicated if DDL replication is off
$node_a->safe_psql($bdr_test_dbname, q{TRUNCATE TABLE trig_test2;});
is($node_a->safe_psql($bdr_test_dbname, 'select count(*) FROM trig_test2'),
	'0', "local truncate");
wait_for_apply($node_a, $node_b);
is( $node_b->safe_psql(
		$bdr_test_dbname, 'select count(*) > 0 FROM trig_test2'),
	't',
	"truncate not replicated");

# Check constraints

# Create the same tables on both nodes
for my $node ($node_a, $node_b)
{
	$node->safe_psql(
		$bdr_test_dbname, q{CREATE TABLE tab1(id  int PRIMARY KEY,
										                   id2 int UNIQUE,
														   name varchar(50));});

	$node->safe_psql(
		$bdr_test_dbname, q{CREATE TABLE tab2(id2 int PRIMARY KEY,
										                   tab1id int,
														   tab1id2 int,
														   CONSTRAINT FK2 FOREIGN KEY(tab1id) REFERENCES tab1(id)
														   );});
}

wait_for_apply($node_a, $node_b);
wait_for_apply($node_b, $node_a);

# Add a constraint on node_b only
$node_b->safe_psql($bdr_test_dbname,
	q{alter table tab2 add constraint FK2_2 FOREIGN KEY (tab1id2) REFERENCES tab1(id2);}
);

# Add a few rows
$node_b->safe_psql($bdr_test_dbname, q{insert into tab1 values(1,1,'1');});
$node_b->safe_psql($bdr_test_dbname, q{insert into tab1 values(2,3,'2');});
$node_b->safe_psql($bdr_test_dbname, q{insert into tab2 values(1,1,1);});
wait_for_apply($node_b, $node_a);

# This insert should fail on node_b
my ($psql_ret, $psql_stdout, $psql_stderr) = ('', '', '');
($psql_ret, $psql_stdout, $psql_stderr) =
  $node_b->psql($bdr_test_dbname, qq{insert into tab2 values(2,2,2);});

like($psql_stderr, qr/violates foreign key constraint/,
	"constraint violated");

# While if the same insert is done on node_a
wait_for_apply($node_b, $node_a);
$node_a->safe_psql($bdr_test_dbname, q{insert into tab2 values(2,2,2);});

# It is replicated on node_b, then violating the extra constraint on this node
wait_for_apply($node_a, $node_b);
is( $node_b->safe_psql(
		$bdr_test_dbname,
		'SELECT id2, tab1id, tab1id2 FROM tab2 where id2 = 2;'),
	'2|2|2',
	"replication bypassed the constraint");

done_testing();
