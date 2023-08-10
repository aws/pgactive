\c postgres

CREATE TABLE test_src_tbl(a serial, b varchar(100), c date, primary key (a,c));
CREATE VIEW test_view AS SELECT * FROM test_src_tbl WHERE a > 1;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_view
\c regression
\d+ test_view
SELECT * FROM test_view;

INSERT INTO test_src_tbl (b,c) VALUES('a', '2014-01-01'), ('b', '2014-02-02'), ('c', '2014-03-03');
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
SELECT * FROM test_view;

UPDATE test_view SET b = a || b;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
SELECT * FROM test_src_tbl;
SELECT * FROM test_view;

ALTER VIEW test_view ALTER COLUMN c SET DEFAULT '2000-01-01';
INSERT INTO test_view(b) VALUES('y2k');
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
SELECT * FROM test_src_tbl;
SELECT * FROM test_view;

ALTER VIEW test_view RENAME TO renamed_test_view;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
INSERT INTO renamed_test_view(b) VALUES('d');
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
SELECT * FROM test_src_tbl;
SELECT * FROM renamed_test_view;

DROP VIEW renamed_test_view;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d renamed_test_view
\c regression
\d renamed_test_view

CREATE VIEW test_view AS SELECT * FROM test_src_tbl;
DROP TABLE test_src_tbl CASCADE;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

\d test_view
\c postgres
\d test_view
