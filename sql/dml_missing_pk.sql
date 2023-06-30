-- test sanity checks for tables without pk
SELECT * FROM public.bdr_regress_variables()
\gset

\c :writedb1

BEGIN;
RESET bdr.skip_ddl_replication;
-- Suppress some CONTEXT msgs that vary by version
SET LOCAL client_min_messages = warning;
SELECT bdr.bdr_replicate_ddl_command($$
	CREATE TABLE public.bdr_missing_pk_parent(a serial PRIMARY KEY);
	CREATE TABLE public.bdr_missing_pk(a serial) INHERITS (public.bdr_missing_pk_parent);
	CREATE VIEW public.bdr_missing_pk_view AS SELECT * FROM public.bdr_missing_pk;
$$);
COMMIT;

INSERT INTO bdr_missing_pk SELECT generate_series(1, 10);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\c :readdb2
SELECT * FROM bdr_missing_pk;

-- these should fail
\c :writedb2
UPDATE bdr_missing_pk SET a = 1;
DELETE FROM bdr_missing_pk WHERE a = 1;

UPDATE bdr_missing_pk_parent SET a = 1;
DELETE FROM bdr_missing_pk_parent WHERE a = 1;

WITH foo AS (
	UPDATE bdr_missing_pk SET a = 1 WHERE a > 1 RETURNING a
) SELECT * FROM foo;

WITH foo AS (
	DELETE FROM bdr_missing_pk RETURNING a
) SELECT * FROM foo;

UPDATE bdr_missing_pk_view SET a = 1;
DELETE FROM bdr_missing_pk_view WHERE a = 1;

WITH foo AS (
	UPDATE bdr_missing_pk_view SET a = 1 WHERE a > 1 RETURNING a
) SELECT * FROM foo;

WITH foo AS (
	DELETE FROM bdr_missing_pk_view RETURNING a
) SELECT * FROM foo;

WITH foo AS (
	UPDATE bdr_missing_pk SET a = 1 RETURNING *
) INSERT INTO bdr_missing_pk SELECT * FROM foo;

WITH foo AS (
	DELETE FROM bdr_missing_pk_view RETURNING a
) INSERT INTO bdr_missing_pk SELECT * FROM foo;


-- success again
TRUNCATE bdr_missing_pk;
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\c :readdb1
SELECT * FROM bdr_missing_pk;

\c :writedb1
-- Direct updates to the catalogs should be permitted, though
-- not necessarily smart.
UPDATE pg_class
SET relname = 'bdr_missing_pk_renamed'
WHERE relname = 'bdr_missing_pk';

SELECT n.nspname as "Schema",
  c.relname as "Name"
FROM pg_catalog.pg_class c
     LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
WHERE c.relkind = 'r'
  AND n.nspname !~ '^pg_toast'
  AND c.relname ~ '^(bdr_missing_pk.*)$'
  AND pg_catalog.pg_table_is_visible(c.oid)
ORDER BY 1,2;
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);

\c :readdb2
-- The catalog change should not replicate
SELECT n.nspname as "Schema",
  c.relname as "Name"
FROM pg_catalog.pg_class c
     LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
WHERE c.relkind = 'r'
  AND n.nspname !~ '^pg_toast'
  AND c.relname ~ '^(bdr_missing_pk.*)$'
  AND pg_catalog.pg_table_is_visible(c.oid)
ORDER BY 1,2;

\c :writedb1
UPDATE pg_class
SET relname = 'bdr_missing_pk'
WHERE relname = 'bdr_missing_pk_renamed';

BEGIN;
RESET bdr.skip_ddl_replication;
SET LOCAL client_min_messages = warning;
SELECT bdr.bdr_replicate_ddl_command($$DROP TABLE public.bdr_missing_pk CASCADE;$$);
COMMIT;
