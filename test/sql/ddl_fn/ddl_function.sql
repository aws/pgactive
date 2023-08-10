-- tests for functions and triggers
\c postgres super
SELECT bdr.bdr_replicate_ddl_command($DDL$ 
CREATE FUNCTION public.test_fn(IN inpar character varying (20), INOUT inoutpar integer, OUT timestamp with time zone) RETURNS SETOF record AS
$$
BEGIN
	PERFORM E'\t\r\n\b\f';
END;
$$ LANGUAGE plpgsql IMMUTABLE  STRICT;
$DDL$);
\df test_fn
\c regression
\df test_fn

SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER FUNCTION public.test_fn(varchar, integer) SECURITY DEFINER CALLED ON NULL INPUT VOLATILE ROWS 1 COST 1; $DDL$);
\df test_fn
\c postgres
\df test_fn

SELECT bdr.bdr_replicate_ddl_command($DDL$ 
CREATE OR REPLACE FUNCTION public.test_fn(IN inpar varchar, INOUT inoutpar integer, OUT timestamp with time zone) RETURNS SETOF record AS 
$$
BEGIN
END;
$$ LANGUAGE plpgsql STABLE;
$DDL$);
\df test_fn
\c regression
\df test_fn

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP FUNCTION public.test_fn(varchar, integer); $DDL$);
\df test_fn
\c postgres
\df test_fn


SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE FUNCTION public.test_trigger_fn() RETURNS trigger AS 
$$
BEGIN
END;
$$ LANGUAGE plpgsql;
$DDL$);
\df test_trigger_fn

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE FUNCTION public.showtrigstate(rel regclass) 
RETURNS TABLE (
	tgname name,
	tgenabled "char",
	tgisinternal boolean)
LANGUAGE sql AS
$$
SELECT
  CASE WHEN t.tgname LIKE 'truncate_trigger%' THEN 'truncate_trigger' ELSE t.tgname END,
  t.tgenabled, t.tgisinternal
FROM pg_catalog.pg_trigger t
WHERE t.tgrelid = $1
ORDER BY t.tgname;
$$;
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_trigger_table (f1 integer, f2 text); $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE TRIGGER test_trigger_fn_trg1 BEFORE INSERT OR DELETE ON public.test_trigger_table FOR EACH STATEMENT WHEN (True) EXECUTE PROCEDURE public.test_trigger_fn(); 
$DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ 
CREATE TRIGGER test_trigger_fn_trg2 AFTER UPDATE OF f1 ON public.test_trigger_table FOR EACH ROW EXECUTE PROCEDURE public.test_trigger_fn(); 
$DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
-- We can't use \d+ here because tgisinternal triggers have names with the oid
-- appended, and that varies run-to-run. Use a custom query.
SELECT * FROM showtrigstate('test_trigger_table'::regclass);
\c regression
SELECT * FROM showtrigstate('test_trigger_table'::regclass);

SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER TRIGGER test_trigger_fn_trg1 ON public.test_trigger_table RENAME TO test_trigger_fn_trg; $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT * FROM showtrigstate('test_trigger_table'::regclass);
\c postgres
SELECT * FROM showtrigstate('test_trigger_table'::regclass);

SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER TABLE public.test_trigger_table DISABLE TRIGGER test_trigger_fn_trg; $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT * FROM showtrigstate('test_trigger_table'::regclass);
\c regression
SELECT * FROM showtrigstate('test_trigger_table'::regclass);

SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER TABLE public.test_trigger_table DISABLE TRIGGER ALL; $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT * FROM showtrigstate('test_trigger_table'::regclass);
\c postgres
SELECT * FROM showtrigstate('test_trigger_table'::regclass);

SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER TABLE public.test_trigger_table ENABLE TRIGGER test_trigger_fn_trg2; $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT * FROM showtrigstate('test_trigger_table'::regclass);
\c regression
SELECT * FROM showtrigstate('test_trigger_table'::regclass);

SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER TABLE public.test_trigger_table ENABLE TRIGGER USER; $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT * FROM showtrigstate('test_trigger_table'::regclass);
\c postgres
SELECT * FROM showtrigstate('test_trigger_table'::regclass);

SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER TABLE public.test_trigger_table ENABLE ALWAYS TRIGGER test_trigger_fn_trg; $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT * FROM showtrigstate('test_trigger_table'::regclass);
\c regression
SELECT * FROM showtrigstate('test_trigger_table'::regclass);

SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER TABLE public.test_trigger_table ENABLE REPLICA TRIGGER test_trigger_fn_trg2; $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT * FROM showtrigstate('test_trigger_table'::regclass);
\c postgres
SELECT * FROM showtrigstate('test_trigger_table'::regclass);

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TRIGGER test_trigger_fn_trg2 ON public.test_trigger_table; $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
SELECT * FROM showtrigstate('test_trigger_table'::regclass);
\c regression
SELECT * FROM showtrigstate('test_trigger_table'::regclass);

-- should fail (for test to be useful it should be called on different node than SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE FUNCTION) $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP FUNCTION public.test_trigger_fn(); $DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_trigger_table; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP FUNCTION public.test_trigger_fn(); $DDL$);
SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\d+ test_trigger_table
\c postgres
\d+ test_trigger_table
\df test_trigger_fn

