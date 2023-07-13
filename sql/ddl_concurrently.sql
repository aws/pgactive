\c regression

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE TABLE public.concurrently_test (
	id integer not null primary key
);
$DDL$);

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres

\d public.concurrently_test

-- Fails: ddl rep not skipped
DROP INDEX CONCURRENTLY concurrently_test_pkey;

-- Fails: ddl rep not skipped
CREATE INDEX CONCURRENTLY named_index ON concurrently_test(id);

-- Fails: drop the constraint
SET bdr.skip_ddl_replication = on;
DROP INDEX CONCURRENTLY concurrently_test_pkey;
RESET bdr.skip_ddl_replication;

-- Fails: no direct DDL
ALTER TABLE public.concurrently_test
DROP CONSTRAINT concurrently_test_pkey;

-- succeeds
SELECT bdr.bdr_replicate_ddl_command($DDL$
ALTER TABLE public.concurrently_test
DROP CONSTRAINT concurrently_test_pkey;
$DDL$);

SELECT relname FROM pg_class WHERE relname IN ('named_index', 'concurrently_test_pkey') AND relkind = 'i' ORDER BY relname;

-- We can create a new index
SET bdr.skip_ddl_replication = on;
CREATE INDEX CONCURRENTLY named_index ON concurrently_test(id);
RESET bdr.skip_ddl_replication;

SELECT relname FROM pg_class WHERE relname IN ('named_index', 'concurrently_test_pkey') AND relkind = 'i' ORDER BY relname;

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

\c regression

SELECT relname FROM pg_class WHERE relname IN ('named_index', 'concurrently_test_pkey') AND relkind = 'i' ORDER BY relname;

SET bdr.skip_ddl_replication = on;
CREATE INDEX CONCURRENTLY named_index ON concurrently_test(id);
RESET bdr.skip_ddl_replication;

SELECT relname FROM pg_class WHERE relname IN ('named_index', 'concurrently_test_pkey') AND relkind = 'i' ORDER BY relname;

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

\c postgres

-- Fails, no skip ddl rep
DROP INDEX CONCURRENTLY named_index;

SELECT relname FROM pg_class WHERE relname IN ('named_index', 'concurrently_test_pkey') AND relkind = 'i' ORDER BY relname;

-- ok
SET bdr.skip_ddl_replication = on;
DROP INDEX CONCURRENTLY named_index;
RESET bdr.skip_ddl_replication;

SELECT relname FROM pg_class WHERE relname IN ('named_index', 'concurrently_test_pkey') AND relkind = 'i' ORDER BY relname;

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

\c regression

SELECT relname FROM pg_class WHERE relname IN ('named_index', 'concurrently_test_pkey') AND relkind = 'i' ORDER BY relname;

-- Have to drop on each node
SET bdr.skip_ddl_replication = on;
DROP INDEX CONCURRENTLY named_index;
RESET bdr.skip_ddl_replication;

SELECT relname FROM pg_class WHERE relname IN ('named_index', 'concurrently_test_pkey') AND relkind = 'i' ORDER BY relname;
