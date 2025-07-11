#!/usr/bin/perl
#
# Test global sequences with various parameter settings
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


# Create an upstream node and bring up pgactive
my $node_a = PostgreSQL::Test::Cluster->new('node_a');
initandstart_pgactive_group($node_a);
my ($ret, $stdout, $stderr);

# Create global sequence
exec_ddl( $node_a, qq{CREATE SEQUENCE public.test_seq;} );
exec_ddl( $node_a, qq{ALTER SEQUENCE public.test_seq MINVALUE -10;} );
exec_ddl( $node_a, qq{ALTER SEQUENCE public.test_seq  INCREMENT BY -1;} );
exec_ddl( $node_a, qq{SELECT setval('public.test_seq', -8);} );

# Try nextval on the sequence
($ret, $stdout, $stderr) = $node_a->psql($pgactive_test_dbname,
qq{ SELECT pgactive.pgactive_snowflake_id_nextval('test_seq'::regclass) FROM generate_series(1,3::bigint); });
like($stderr, qr/produced a negative result/, "psql error message for nextval reaching minvalue");

# alter global sequence for positive increment and set a max value.
# psql should error out on reaching maxvalue
exec_ddl( $node_a, qq{ALTER SEQUENCE public.test_seq NO MINVALUE;} );
exec_ddl( $node_a, qq{ALTER SEQUENCE public.test_seq  INCREMENT BY 2;} );
exec_ddl( $node_a, qq{ALTER SEQUENCE public.test_seq MAXVALUE 3;} );
exec_ddl( $node_a, qq{ALTER SEQUENCE public.test_seq RESTART;} );
exec_ddl( $node_a, qq{SELECT setval('public.test_seq', 1);} );

($ret, $stdout, $stderr) = $node_a->psql($pgactive_test_dbname,
qq{ SELECT pgactive.pgactive_snowflake_id_nextval('test_seq'::regclass) FROM generate_series(1,4::bigint); });
like($stderr, qr/reached maximum value of sequence/, "psql error message for nextval reaching maxvalue");


# Now make is_cycled true and try nextval to cross maxvalue

exec_ddl( $node_a, qq{ALTER SEQUENCE public.test_seq RESTART;} );
exec_ddl( $node_a, qq{ALTER SEQUENCE public.test_seq CYCLE;} );

($ret, $stdout, $stderr) = $node_a->psql($pgactive_test_dbname,
qq{ SELECT pgactive.pgactive_snowflake_id_nextval('test_seq'::regclass) FROM generate_series(1,4::bigint); });
like($stderr, qr/produced a negative result/, "psql error message for nextval cycling back to negative");

done_testing();
