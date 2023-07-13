\c regression
SELECT bdr.bdr_replicate_ddl_command($DDL$                          
CREATE VIEW public.list_privileges  AS
SELECT n.nspname as "Schema",
  c.relname as "Name",
  CASE c.relkind WHEN 'r' THEN 'table' WHEN 'v' THEN 'view' WHEN 'm' THEN 'materialized view' WHEN 'S' THEN 'sequence' WHEN 'f' THEN 'foreign table' END as "Type",
  pg_catalog.array_to_string(c.relacl, E'\n') AS "Access privileges"
FROM pg_catalog.pg_class c
     LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
WHERE c.relkind IN ('r', 'v', 'm', 'S', 'f')
  AND n.nspname ~ '^grant_test.*'
ORDER BY 1, 2;
$DDL$);

SET SESSION AUTHORIZATION super;
SELECT bdr.bdr_replicate_ddl_command($DDL$ 
CREATE SCHEMA grant_test
	CREATE TABLE test_tbl(a serial, b text, primary key (a))
	CREATE VIEW test_view AS SELECT * FROM test_tbl;
$DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ 
CREATE FUNCTION grant_test.test_func(i int, out o text) AS $$SELECT i::text;$$ LANGUAGE SQL STRICT SECURITY DEFINER;
$DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TYPE grant_test.test_type AS (prefix text, number text); $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE DOMAIN grant_test.test_domain AS timestamptz DEFAULT '2014-01-01' NOT NULL; $DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$ GRANT SELECT, INSERT ON grant_test.test_tbl TO nonsuper WITH GRANT OPTION; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ GRANT SELECT, INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON grant_test.test_view TO nonsuper; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ GRANT USAGE, UPDATE ON grant_test.test_tbl_a_seq TO nonsuper; $DDL$);

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT * from list_privileges;
\c postgres
SELECT * from list_privileges;

SELECT bdr.bdr_replicate_ddl_command($DDL$ REVOKE ALL PRIVILEGES ON ALL TABLES IN SCHEMA grant_test FROM PUBLIC, nonsuper; $DDL$);

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT * from list_privileges;
\c regression
SELECT * from list_privileges;

SELECT bdr.bdr_replicate_ddl_command($DDL$ GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA grant_test TO nonsuper WITH  GRANT OPTION; $DDL$);

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT * from list_privileges;
\c postgres
SELECT * from list_privileges;

SELECT bdr.bdr_replicate_ddl_command($DDL$ REVOKE TRIGGER, INSERT, UPDATE, DELETE, REFERENCES, TRUNCATE ON grant_test.test_view FROM nonsuper; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ REVOKE ALL PRIVILEGES ON grant_test.test_tbl_a_seq FROM nonsuper; $DDL$);

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT * from list_privileges;
\c regression
SELECT * from list_privileges;

SELECT bdr.bdr_replicate_ddl_command($DDL$ GRANT EXECUTE ON FUNCTION grant_test.test_func(int) TO nonsuper; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ GRANT USAGE ON TYPE grant_test.test_type TO nonsuper; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ GRANT ALL PRIVILEGES ON DOMAIN grant_test.test_domain TO nonsuper WITH  GRANT OPTION; $DDL$);

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT proacl FROM pg_proc WHERE oid = 'grant_test.test_func(int)'::regprocedure;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_domain'::regtype;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_type'::regtype;
\c postgres
SELECT proacl FROM pg_proc WHERE oid = 'grant_test.test_func(int)'::regprocedure;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_domain'::regtype;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_type'::regtype;

SELECT bdr.bdr_replicate_ddl_command($DDL$ REVOKE ALL PRIVILEGES ON FUNCTION grant_test.test_func(int) FROM nonsuper; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ REVOKE ALL PRIVILEGES ON TYPE grant_test.test_type FROM nonsuper; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ REVOKE USAGE ON DOMAIN grant_test.test_domain FROM nonsuper; $DDL$);

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT proacl FROM pg_proc WHERE oid = 'grant_test.test_func(int)'::regprocedure;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_domain'::regtype;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_type'::regtype;
\c regression
SELECT proacl FROM pg_proc WHERE oid = 'grant_test.test_func(int)'::regprocedure;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_domain'::regtype;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_type'::regtype;
\set VERBOSITY terse
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP SCHEMA grant_test CASCADE; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP VIEW public.list_privileges; $DDL$);
