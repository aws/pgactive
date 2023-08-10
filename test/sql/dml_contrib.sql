-- contrib datatypes
SELECT * FROM public.bdr_regress_variables()
\gset

\c :writedb1

BEGIN;
RESET bdr.skip_ddl_replication;
SELECT bdr.bdr_replicate_ddl_command($$
	CREATE EXTENSION IF NOT EXISTS cube SCHEMA public;
	CREATE EXTENSION IF NOT EXISTS hstore SCHEMA public;

	CREATE TABLE public.contrib_dml (
		id serial primary key,
		fixed public.cube,
		variable public.hstore
	);
$$);
COMMIT;

-- check basic insert replication
INSERT INTO contrib_dml(fixed, variable)
VALUES ('(1,2)', 'a=>1,b=>2'),
       ('(3,4)', 'c=>3,d=>4'),
       ('(5,6)', 'e=>5,f=>6'),
       ('(7,8)', 'g=>7,h=>8'),
       ('(1,2,3)', 'a=>1,b=>2,c=>3'),
       ('(4,5,6)', 'c=>1,d=>2,e=>3'),
       (NULL, NULL);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c :readdb2
SELECT id, fixed, variable FROM contrib_dml ORDER BY id;

-- update one row
\c :writedb2
UPDATE contrib_dml SET fixed = '(1,2,3,4)', variable = 'a=>NULL,b=>1' WHERE id = 1;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c :readdb1
SELECT id, fixed, variable FROM contrib_dml ORDER BY id;

-- update multiple rows
\c :writedb1
UPDATE contrib_dml SET fixed = cube_enlarge(fixed, 1, 1), variable = variable || 'x=>99' WHERE '1' <@ fixed;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c :readdb2
SELECT id, fixed, variable FROM contrib_dml ORDER BY id;

-- delete one row
\c :writedb2
DELETE FROM contrib_dml WHERE id = 2;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c :readdb1
SELECT id, fixed, variable FROM contrib_dml ORDER BY id;

-- delete multiple rows
\c :writedb1
DELETE FROM contrib_dml WHERE id < 4;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c :readdb2
SELECT id, fixed, variable FROM contrib_dml ORDER BY id;

-- truncate
\c :writedb2
TRUNCATE contrib_dml;
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c :readdb1
SELECT id, fixed, variable FROM contrib_dml ORDER BY id;

\c :writedb1
BEGIN;
RESET bdr.skip_ddl_replication;
SELECT bdr.bdr_replicate_ddl_command($$DROP TABLE public.contrib_dml;$$);
COMMIT;
