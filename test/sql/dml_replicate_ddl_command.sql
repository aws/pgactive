-- complex datatype handling
SELECT * FROM public.bdr_regress_variables()
\gset

\c :writedb1

-- Make sure we properly guard against mixing DDL and DML in
-- bdr.replicate_ddl_command

SELECT bdr.bdr_replicate_ddl_command($DDL$
  CREATE TABLE public.foo(id integer PRIMARY KEY, bar integer);
  INSERT INTO public.foo(id, bar) VALUES (1, 42);
$DDL$);

-- Create it properly now
SELECT bdr.bdr_replicate_ddl_command($DDL$
  CREATE TABLE public.foo(id integer PRIMARY KEY, bar integer);
$DDL$);

INSERT INTO foo(id, bar) VALUES (1, 42);

-- and check for guard against UPDATE too.  This is a known-pathalogical case
-- that'll break replication.

SELECT bdr.bdr_replicate_ddl_command($DDL$
  SET LOCAL search_path = 'public';
  ALTER TABLE foo ADD COLUMN baz integer;
  UPDATE foo SET baz = bar;
  ALTER TABLE foo DROP COLUMN bar;
$DDL$);

-- Do it right
BEGIN;
SELECT bdr.bdr_replicate_ddl_command($DDL$
  SET LOCAL search_path = 'public';
  ALTER TABLE foo ADD COLUMN baz integer;
$DDL$);
UPDATE foo SET baz = bar;
SELECT bdr.bdr_replicate_ddl_command($DDL$
  SET LOCAL search_path = 'public';
  ALTER TABLE foo DROP COLUMN bar;
$DDL$);
COMMIT;
