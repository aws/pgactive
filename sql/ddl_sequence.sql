-- ALTER TABLE DROP COLUMN (pk column)
CREATE TABLE test (test_id SERIAL);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

\c postgres
\d+ test
SELECT relname, relkind FROM pg_class WHERE relname = 'test_test_id_seq';
\d+ test_test_id_seq

ALTER TABLE test DROP COLUMN test_id;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test
SELECT relname, relkind FROM pg_class WHERE relname = 'test_test_id_seq';

DROP TABLE test;

-- ADD CONSTRAINT PRIMARY KEY
CREATE TABLE test (test_id SERIAL NOT NULL);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
\d+ test
ALTER TABLE test ADD CONSTRAINT test_pkey PRIMARY KEY (test_id);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test

DROP TABLE test;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres

CREATE SEQUENCE test_seq USING bdr;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_seq
\c regression
\d+ test_seq

ALTER SEQUENCE test_seq owned by test_tbl_serial_combined_pk.val;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_seq
\c postgres
\d+ test_seq

-- these should fail
ALTER SEQUENCE test_seq increment by 10;
ALTER SEQUENCE test_seq minvalue 0;
ALTER SEQUENCE test_seq maxvalue 1000000;
ALTER SEQUENCE test_seq restart;
ALTER SEQUENCE test_seq cache 10;
ALTER SEQUENCE test_seq cycle;

DROP SEQUENCE test_seq;

CREATE SEQUENCE test_seq start 10000 owned by test_tbl_serial_combined_pk.val1 USING bdr;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_seq
\c regression
\d+ test_seq

DROP SEQUENCE test_seq;

-- these should fail
CREATE SEQUENCE test_seq increment by 10 USING bdr;
CREATE SEQUENCE test_seq minvalue 10 USING bdr;
CREATE SEQUENCE test_seq maxvalue 10 USING bdr;
CREATE SEQUENCE test_seq cache 10 USING bdr;
CREATE SEQUENCE test_seq cycle USING bdr;

-- non-bdr sequence
CREATE SEQUENCE test_seq increment 10;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_seq
\c postgres
\d+ test_seq

ALTER SEQUENCE test_seq increment by 10;
ALTER SEQUENCE test_seq minvalue 0;
ALTER SEQUENCE test_seq maxvalue 1000000;
ALTER SEQUENCE test_seq restart;
ALTER SEQUENCE test_seq cache 10;
ALTER SEQUENCE test_seq cycle;
ALTER SEQUENCE test_seq RENAME TO renamed_test_seq;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_seq
\d+ renamed_test_seq
\c regression
\d+ test_seq
\d+ renamed_test_seq

ALTER SEQUENCE renamed_test_seq USING bdr;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ renamed_test_seq;
\c postgres
\d+ renamed_test_seq

DROP SEQUENCE renamed_test_seq;

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ renamed_test_seq;
\c regression
\d+ renamed_test_seq

CREATE SEQUENCE test_seq;
CREATE TABLE test_tbl (a int DEFAULT nextval('test_seq'));
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_tbl
\c postgres
\d+ test_tbl

DROP SEQUENCE test_seq CASCADE;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_tbl
\c regression
\d+ test_tbl

DROP TABLE test_tbl;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
