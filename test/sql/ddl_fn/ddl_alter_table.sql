SELECT pgactive.pgactive_replicate_ddl_command($DDL$ DROP TABLE IF EXISTS public.test_tbl; $DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE TABLE public.test_tbl(pk int primary key, dropping_col1 text, dropping_col2 text);
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ADD COLUMN col1 text;
$DDL$);
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ADD COLUMN col2 text;
$DDL$);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ADD COLUMN col3_fail timestamptz NOT NULL DEFAULT now();
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ADD COLUMN serial_col_node1 SERIAL;
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl DROP COLUMN dropping_col1;
$DDL$);
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl DROP COLUMN dropping_col2;
$DDL$);
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ALTER COLUMN col1 SET NOT NULL;
$DDL$);
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ALTER COLUMN col2 SET NOT NULL;
$DDL$);
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ALTER COLUMN col1 DROP NOT NULL;
$DDL$);
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ALTER COLUMN col2 DROP NOT NULL;
$DDL$);
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ALTER COLUMN col1 SET DEFAULT 'abc';
$DDL$);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ALTER COLUMN col2 SET DEFAULT 'abc';
$DDL$);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ALTER COLUMN col1 DROP DEFAULT;
$DDL$);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ALTER COLUMN col2 DROP DEFAULT;
$DDL$);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ADD CONSTRAINT test_const CHECK (true);
$DDL$);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ADD CONSTRAINT test_const1 CHECK (true);
$DDL$);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl DROP CONSTRAINT test_const;
$DDL$);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl DROP CONSTRAINT test_const1;
$DDL$);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ALTER COLUMN col1 SET NOT NULL;
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE UNIQUE INDEX test_idx ON public.test_tbl(col1);
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl REPLICA IDENTITY USING INDEX test_idx;
$DDL$);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ALTER COLUMN col2 SET NOT NULL;
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE UNIQUE INDEX test_idx1 ON public.test_tbl(col2);
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl REPLICA IDENTITY USING INDEX test_idx1;
$DDL$);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl REPLICA IDENTITY DEFAULT;
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
DROP INDEX public.test_idx;
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
DROP INDEX public. test_idx1;
$DDL$);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE UNIQUE INDEX test_idx ON public.test_tbl(col1);
$DDL$);
SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl REPLICA IDENTITY USING INDEX test_idx;
$DDL$);
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT pgactive.pgactive_replicate_ddl_command($DDL$
DROP INDEX public.test_idx;
$DDL$);
\d+ test_tbl
\c regression
\d+ test_tbl

CREATE USER test_user;
SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl OWNER TO test_user;
$DDL$);
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl RENAME COLUMN col1 TO foobar;
$DDL$);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_tbl
\c regression
\d+ test_tbl

\c postgres
\d+ test_tbl
SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl RENAME CONSTRAINT test_tbl_pkey TO test_ddl_pk;
$DDL$);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test_tbl

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
DROP TABLE public.test_tbl;
$DDL$);


SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

-- ALTER COLUMN ... SET STATISTICS
\c postgres
SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE TABLE public.test_tbl(id int);
$DDL$);
SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ALTER COLUMN id SET STATISTICS 10;
$DDL$);


\d+ test_tbl
SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ALTER COLUMN id SET STATISTICS 0;
$DDL$);

\d+ test_tbl
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
\d+ test_tbl
SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_tbl ALTER COLUMN id SET STATISTICS -1;
$DDL$);

\d+ test_tbl
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
\d+ test_tbl
SELECT pgactive.pgactive_replicate_ddl_command($DDL$
DROP TABLE public.test_tbl;
$DDL$);


--- INHERITANCE ---
\c postgres

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE TABLE public.test_inh_root (id int primary key, val1 varchar, val2 int);
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE TABLE public.test_inh_chld1 (child1col int) INHERITS (public.test_inh_root);
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE TABLE public.test_inh_chld2 () INHERITS (public.test_inh_chld1);
$DDL$);


INSERT INTO public.test_inh_root(id, val1, val2)
SELECT x, x::text, x%4 FROM generate_series(1,10) x;

INSERT INTO public.test_inh_chld1(id, val1, val2, child1col)
SELECT x, x::text, x%4+1, x*2 FROM generate_series(11,20) x;

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_inh_root
\d+ test_inh_chld1
\d+ test_inh_chld2
\c regression
\d+ test_inh_root
\d+ test_inh_chld1
\d+ test_inh_chld2

SELECT * FROM public.test_inh_root;
SELECT * FROM public.test_inh_chld1;
SELECT * FROM public.test_inh_chld2;

SET pgactive.skip_ddl_replication = true;
SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE public.test_inh_root ADD CONSTRAINT idchk CHECK (id > 0);
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE ONLY public.test_inh_chld1 ALTER COLUMN id SET DEFAULT 1;
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER TABLE ONLY public.test_inh_root DROP CONSTRAINT idchk;
$DDL$);

RESET pgactive.skip_ddl_replication;

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_inh_root
\d+ test_inh_chld1
\d+ test_inh_chld2
\c postgres
\d+ test_inh_root
\d+ test_inh_chld1
\d+ test_inh_chld2

\c regression

SELECT * FROM public.test_inh_root;
SELECT * FROM public.test_inh_chld1;
SELECT * FROM public.test_inh_chld2;

-- Should fail with an ERROR
ALTER TABLE public.test_inh_chld1 NO INHERIT public.test_inh_root;


-- Will also fail with an ERROR
SELECT pgactive.pgactive_replicate_ddl_command($DDL$ ALTER TABLE public.test_inh_chld1 NO INHERIT public.test_inh_root; $DDL$);

-- Will be permitted
BEGIN;
SET LOCAL pgactive.skip_ddl_replication = true;
SELECT pgactive.pgactive_replicate_ddl_command($DDL$ ALTER TABLE public.test_inh_chld1 NO INHERIT public.test_inh_root;$DDL$);
COMMIT;


SELECT * FROM public.test_inh_root;
SELECT * FROM public.test_inh_chld1;
SELECT * FROM public.test_inh_chld2;

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

\c postgres

SELECT * FROM public.test_inh_root;
SELECT * FROM public.test_inh_chld1;
SELECT * FROM public.test_inh_chld2;

DELETE FROM public.test_inh_root WHERE val2 = 0;
INSERT INTO public.test_inh_root(id, val1, val2) VALUES (200, 'root', 1);
INSERT INTO public.test_inh_chld1(id, val1, val2, child1col) VALUES (200, 'child', 0, 0);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

\c regression

SELECT * FROM public.test_inh_root;
SELECT * FROM public.test_inh_chld1;
SELECT * FROM public.test_inh_chld2;

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
DROP TABLE public.test_inh_chld2;
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
DROP TABLE public.test_inh_chld1;
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
DROP TABLE public.test_inh_root;
$DDL$);

