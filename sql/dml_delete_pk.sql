-- RT #37826 "issuing a DELETE broken replication"
SELECT * FROM public.bdr_regress_variables()
\gset

\c :writedb1

BEGIN;
RESET bdr.skip_ddl_replication;
SELECT bdr.bdr_replicate_ddl_command($$
	CREATE TABLE public.test (
		id TEXT,
		ts TIMESTAMP DEFAULT ('now'::TEXT)::TIMESTAMP,
		PRIMARY KEY (id)
	);
$$);
COMMIT;

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

-- INSERT data
INSERT INTO test (id, ts) VALUES ('row', '1970-07-21 12:00:00');
INSERT INTO test (id) VALUES ('broken');
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c :readdb2
SELECT id FROM test ORDER BY ts;

-- DELETE one row by PK
\c :writedb2
DELETE FROM test WHERE id = 'row';
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT id FROM test ORDER BY ts;

\c :readdb1
SELECT id FROM test ORDER BY ts;

\c :writedb1
DELETE FROM test WHERE id = 'broken';
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT id FROM test ORDER BY ts;

\c :readdb2
SELECT id FROM test ORDER BY ts;

\c :writedb1
BEGIN;
RESET bdr.skip_ddl_replication;
SELECT bdr.bdr_replicate_ddl_command($$DROP TABLE public.test;$$);
COMMIT;
