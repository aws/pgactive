\c postgres
CREATE VIEW public.list_extension AS
SELECT e.extname AS "Name",  n.nspname AS "Schema", c.description AS "Description"
FROM pg_catalog.pg_extension e LEFT JOIN pg_catalog.pg_namespace n ON n.oid = e.extnamespace LEFT JOIN pg_catalog.pg_description c ON c.objoid = e.oid AND c.classoid = 'pg_catalog.pg_extension'::pg_catalog.regclass
WHERE e.extname ~ '^(pg_trgm)$'
ORDER BY 1;

-- create nonexistant extension
CREATE EXTENSION pg_trgm;
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\c regression
SELECT * from list_extension;

-- drop and recreate using CINE
DROP EXTENSION pg_trgm;
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\c postgres
SELECT * from list_extension;
CREATE EXTENSION IF NOT EXISTS pg_trgm;
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\c regression
SELECT * from list_extension;

-- CINE existing extension
CREATE EXTENSION IF NOT EXISTS pg_trgm;
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\c postgres
SELECT * from list_extension;

DROP EXTENSION pg_trgm;
DROP VIEW public.list_extension;
