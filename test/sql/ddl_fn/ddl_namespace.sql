\c postgres
SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE SCHEMA test_schema_1
       CREATE UNIQUE INDEX abc_a_idx ON abc (a)

       CREATE VIEW abc_view AS
              SELECT a+1 AS a, b+1 AS b FROM abc

       CREATE TABLE abc (
              a serial,
              b int UNIQUE
       );
$DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE FUNCTION test_schema_1.abc_func() RETURNS void
       AS $$ BEGIN END; $$ LANGUAGE plpgsql;
$DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
SELECT COUNT(*) FROM pg_class WHERE relnamespace =
    (SELECT oid FROM pg_namespace WHERE nspname = 'test_schema_1');

INSERT INTO test_schema_1.abc DEFAULT VALUES;
INSERT INTO test_schema_1.abc DEFAULT VALUES;
INSERT INTO test_schema_1.abc DEFAULT VALUES;

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
SELECT * FROM test_schema_1.abc;
SELECT * FROM test_schema_1.abc_view;

SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER SCHEMA test_schema_1 RENAME TO test_schema_renamed; $DDL$);

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
SELECT COUNT(*) FROM pg_class WHERE relnamespace =
    (SELECT oid FROM pg_namespace WHERE nspname = 'test_schema_1');
\set VERBOSITY terse
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE SCHEMA test_schema_renamed; $DDL$); -- fail, already exists
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE SCHEMA IF NOT EXISTS test_schema_renamed; $DDL$); -- ok with notice

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP SCHEMA test_schema_renamed CASCADE; $DDL$);

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
SELECT COUNT(*) FROM pg_class WHERE relnamespace =
    (SELECT oid FROM pg_namespace WHERE nspname = 'test_schema_renamed');
