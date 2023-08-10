-- So we can verify that CHECK constraint violation fires,
-- we need to fake up a conflict, which we'll do by defining
-- a function that returns different results on each node
-- by lying about its stability.
CREATE TABLE check_table (
    check_value integer
);

SELECT bdr.bdr_set_table_replication_sets('check_table', '{unused}');

INSERT INTO check_table(check_value) VALUES (1);
\c postgres
INSERT INTO check_table(check_value) VALUES (2);
\c regression

\c postgres
SELECT * FROM check_table;
\c regression
SELECT * FROM check_table;

CREATE FUNCTION expected_check_value() RETURNS integer
LANGUAGE sql IMMUTABLE AS
$$ SELECT check_value FROM check_table; $$;

CREATE TABLE constraint_test (
	check_col INTEGER,
	CONSTRAINT some_constraint CHECK (check_col = expected_check_value())
);

-- Inserting the wrong value should fail on upstream
INSERT INTO constraint_test(check_col) VALUES (2);
-- Insert should succeed on upstream, fail on downstream
INSERT INTO constraint_test(check_col) VALUES (1);

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

-- Getting a DDL lock should fail due to replay delay if
-- apply is failing due to violated check constraint
BEGIN;
SET LOCAL statement_timeout = '5s';
CREATE TABLE fail_me(x integer);
ROLLBACK;

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

SELECT * FROM constraint_test;
\c postgres
SELECT * FROM constraint_test;
\c regression

TRUNCATE TABLE constraint_test;

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);


ALTER TABLE constraint_test
DROP CONSTRAINT some_constraint;

CREATE FUNCTION test_tg() RETURNS trigger LANGUAGE plpgsql AS
$$
BEGIN
   NEW.check_col := (SELECT check_value FROM check_table);
   RAISE WARNING 'trigger fired, s_r_r is %, new col val is %', current_setting('session_replication_role'), NEW.check_col;
   RETURN NEW;
END;
$$;

CREATE TRIGGER test_tg
BEFORE INSERT ON constraint_test
FOR EACH ROW EXECUTE PROCEDURE test_tg();

CREATE FUNCTION test_tg_after() RETURNS trigger LANGUAGE plpgsql AS
$$
BEGIN
  RAISE WARNING 'constraint trigger fired, s_r_r is %, val is %', current_setting('session_replication_role'), NEW.check_col;
  RETURN NEW;
END;
$$;

CREATE CONSTRAINT TRIGGER constraint_test_tg
AFTER INSERT ON constraint_test
FOR EACH ROW EXECUTE PROCEDURE test_tg_after();

ALTER TABLE constraint_test
ENABLE ALWAYS TRIGGER test_tg;

ALTER TABLE constraint_test
ENABLE ALWAYS TRIGGER constraint_test_tg;

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

--
-- If the trigger fires this will change the value to be different
-- on upstream and downstream. If it only fires on upstream the row
-- will be 1 on both.
--
INSERT INTO constraint_test(check_col) VALUES (4);

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

SELECT * FROM constraint_test;
\c postgres
SELECT * FROM constraint_test;
\c regression
