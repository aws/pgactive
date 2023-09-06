\c regression

SET SESSION AUTHORIZATION super;
CREATE SCHEMA grant_test
	CREATE TABLE test_tbl(a serial, b text, primary key (a))
	CREATE VIEW test_view AS SELECT * FROM test_tbl;

CREATE FUNCTION grant_test.test_func(i int, out o text) AS $$SELECT i::text;$$ LANGUAGE SQL STRICT SECURITY DEFINER;
CREATE TYPE grant_test.test_type AS (prefix text, number text);
CREATE DOMAIN grant_test.test_domain AS timestamptz DEFAULT '2014-01-01' NOT NULL;

GRANT SELECT, INSERT ON grant_test.test_tbl TO nonsuper WITH GRANT OPTION;
GRANT SELECT, INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON grant_test.test_view TO nonsuper;
GRANT USAGE, UPDATE ON grant_test.test_tbl_a_seq TO nonsuper;

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\dp grant_test.*
\c postgres
\dp grant_test.*

REVOKE ALL PRIVILEGES ON ALL TABLES IN SCHEMA grant_test FROM PUBLIC, nonsuper;

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\dp grant_test.*
\c regression
\dp grant_test.*

GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA grant_test TO nonsuper WITH GRANT OPTION;

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\dp grant_test.*
\c postgres
\dp grant_test.*

REVOKE TRIGGER, INSERT, UPDATE, DELETE, REFERENCES, TRUNCATE ON grant_test.test_view FROM nonsuper;
REVOKE ALL PRIVILEGES ON grant_test.test_tbl_a_seq FROM nonsuper;

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\dp grant_test.*
\c regression
\dp grant_test.*

GRANT EXECUTE ON FUNCTION grant_test.test_func(int) TO nonsuper;
GRANT USAGE ON TYPE grant_test.test_type TO nonsuper;
GRANT ALL PRIVILEGES ON DOMAIN grant_test.test_domain TO nonsuper WITH GRANT OPTION;

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT proacl FROM pg_proc WHERE oid = 'grant_test.test_func(int)'::regprocedure;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_domain'::regtype;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_type'::regtype;
\c postgres
SELECT proacl FROM pg_proc WHERE oid = 'grant_test.test_func(int)'::regprocedure;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_domain'::regtype;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_type'::regtype;

REVOKE ALL PRIVILEGES ON FUNCTION grant_test.test_func(int) FROM nonsuper;
REVOKE ALL PRIVILEGES ON TYPE grant_test.test_type FROM nonsuper;
REVOKE USAGE ON DOMAIN grant_test.test_domain FROM nonsuper;

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT proacl FROM pg_proc WHERE oid = 'grant_test.test_func(int)'::regprocedure;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_domain'::regtype;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_type'::regtype;
\c regression
SELECT proacl FROM pg_proc WHERE oid = 'grant_test.test_func(int)'::regprocedure;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_domain'::regtype;
SELECT typacl FROM pg_type WHERE oid = 'grant_test.test_type'::regtype;

DROP SCHEMA grant_test CASCADE;
