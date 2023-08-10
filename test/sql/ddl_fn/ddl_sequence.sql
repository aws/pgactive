--  ALTER TABLE public.DROP COLUMN (pk column)
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test (test_id SERIAL); $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

\c postgres
\d+ test
SELECT relname, relkind FROM pg_class WHERE relname = 'test_test_id_seq';
\d+ test_test_id_seq

SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER TABLE public.test  DROP COLUMN test_id; $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test
SELECT relname, relkind FROM pg_class WHERE relname = 'test_test_id_seq';

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test; $DDL$);

-- ADD CONSTRAINT PRIMARY KEY
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test (test_id SERIAL NOT NULL); $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
\d+ test
SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER TABLE public.test ADD CONSTRAINT test_pkey PRIMARY KEY (test_id); $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test; $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres

-- normal sequence
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE SEQUENCE public.test_seq increment 10; $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_seq
\c postgres
\d+ test_seq

SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER SEQUENCE public.test_seq increment by 10; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER SEQUENCE public.test_seq minvalue 0; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER SEQUENCE public.test_seq maxvalue 1000000; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER SEQUENCE public.test_seq restart; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER SEQUENCE public.test_seq cache 10; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER SEQUENCE public.test_seq cycle; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER SEQUENCE public.test_seq RENAME TO renamed_test_seq; $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_seq
\d+ renamed_test_seq
\c regression
\d+ test_seq
\d+ renamed_test_seq
\c postgres


SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP SEQUENCE public.renamed_test_seq; $DDL$);

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ renamed_test_seq;
\c regression
\d+ renamed_test_seq

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE SEQUENCE public.test_seq; $DDL$);
-- DESTINATION COLUMN TYPE REQUIRED BIGINT
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE IF EXISTS public.test_tbl; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_tbl (a int DEFAULT bdr.bdr_snowflake_id_nextval('public.test_seq'),b text); $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_tbl
\c postgres
\d+ test_tbl
INSERT INTO test_tbl(b) VALUES('abc');
SELECT count(*) FROM test_tbl;

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_tbl; $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_tbl (a bigint DEFAULT bdr.bdr_snowflake_id_nextval('public.test_seq'),b text); $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_tbl
\c postgres
\d+ test_tbl
INSERT INTO test_tbl(b) VALUES('abc');
SELECT count(*) FROM test_tbl;
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP SEQUENCE public.test_seq CASCADE; $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_tbl
\c regression
\d+ test_tbl
