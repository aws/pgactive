\c regression

SELECT pgactive.pgactive_is_apply_paused();

SELECT pgactive.pgactive_replicate_ddl_command('CREATE TABLE public.pause_test(x text primary key);');
INSERT INTO pause_test(x) VALUES ('before pause');
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

\c postgres

SELECT pgactive.pgactive_is_apply_paused();
SELECT pgactive.pgactive_apply_pause();
SELECT pgactive.pgactive_is_apply_paused();
-- It's necessary to wait for a latch timeout on apply workers
-- until pgactive_apply_pause gets taught to set their latches.
SELECT pg_sleep(6);

\c regression

INSERT INTO pause_test(x) VALUES ('after pause before resume');

\c postgres

-- Give more time for a row to replicate if it's going to
-- (it shouldn't)
SELECT pg_sleep(1);

-- Pause state is preserved across sessions
SELECT pgactive.pgactive_is_apply_paused();

-- Must not see row from after pause
SELECT x FROM pause_test;

SELECT pgactive.pgactive_apply_resume();

\c regression

INSERT INTO pause_test(x) VALUES ('after resume');

-- The pause latch timeout is 5 minutes. To make sure that setting
-- the latch is doing its job and unpausing before timeout, expect
-- resume to take effect well before then.
BEGIN;
SET LOCAL statement_timeout = '60s';
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
COMMIT;

\c postgres

-- Must see all three rows
SELECT x FROM pause_test;
