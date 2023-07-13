--  ALTER TABLE public.DROP COLUMN (pk column)
CREATE TABLE public.test (test_id SERIAL);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

\c postgres
\d+ test
SELECT relname, relkind FROM pg_class WHERE relname = 'test_test_id_seq';
\d+ test_test_id_seq

ALTER TABLE public.test  DROP COLUMN test_id;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test
SELECT relname, relkind FROM pg_class WHERE relname = 'test_test_id_seq';

DROP TABLE public.test;

-- ADD CONSTRAINT PRIMARY KEY
CREATE TABLE public.test (test_id SERIAL NOT NULL);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
\d+ test
ALTER TABLE public.test ADD CONSTRAINT test_pkey PRIMARY KEY (test_id);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test

DROP TABLE public.test;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres

-- normal sequence
CREATE SEQUENCE public.test_seq increment 10;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_seq
\c postgres
\d+ test_seq

ALTER SEQUENCE public.test_seq increment by 10;
ALTER SEQUENCE public.test_seq minvalue 0;
ALTER SEQUENCE public.test_seq maxvalue 1000000;
ALTER SEQUENCE public.test_seq restart;
ALTER SEQUENCE public.test_seq cache 10;
ALTER SEQUENCE public.test_seq cycle;
ALTER SEQUENCE public.test_seq RENAME TO renamed_test_seq;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_seq
\d+ renamed_test_seq
\c regression
\d+ test_seq
\d+ renamed_test_seq
\c postgres


DROP SEQUENCE public.renamed_test_seq;

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ renamed_test_seq;
\c regression
\d+ renamed_test_seq

CREATE SEQUENCE public.test_seq;
-- DESTINATION COLUMN TYPE REQUIRED BIGINT
DROP TABLE IF EXISTS public.test_tbl;
CREATE TABLE public.test_tbl (a int DEFAULT bdr.bdr_snowflake_id_nextval('public.test_seq'),b text);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_tbl
\c postgres
\d+ test_tbl
INSERT INTO test_tbl(b) VALUES('abc');
SELECT count(*) FROM test_tbl;

DROP TABLE public.test_tbl;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

CREATE TABLE public.test_tbl (a bigint DEFAULT bdr.bdr_snowflake_id_nextval('public.test_seq'),b text);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_tbl
\c postgres
\d+ test_tbl
INSERT INTO test_tbl(b) VALUES('abc');
SELECT count(*) FROM test_tbl;
DROP SEQUENCE public.test_seq CASCADE;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_tbl
\c regression
\d+ test_tbl
