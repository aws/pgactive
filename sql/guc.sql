-- Allow commands via ALTER SYSTEM SET, config file, ALTER DATABASE set, etc

ALTER SYSTEM
  SET bdr.skip_ddl_replication = on;

-- The check for per-database settings only occurs when you're on that
-- database, so we don't block the setting on another DB and the user
-- has to undo it later.
SELECT current_database();

-- Should be ok
ALTER DATABASE postgres
  SET bdr.skip_ddl_replication = on;

-- Should fail
ALTER DATABASE postgres
  SET bdr.skip_ddl_replication = off;

-- An ERROR setting a GUC doesn't stop the connection to the DB
-- from succeeding though.
\c postgres
SELECT current_database();

ALTER DATABASE postgres
  RESET bdr.skip_ddl_replication;

\c postgres
SELECT current_database();

\c regression
SELECT current_database();

-- This is true even when you ALTER the current database, so this
-- commits fine, but switching back to the DB breaks:
ALTER DATABASE regression
  SET bdr.skip_ddl_replication = off;

\c postgres
SELECT current_database();
-- so this will report an error, but we'll still successfully connect to the DB.
\c regression
SELECT current_database();

-- and fix the GUC
ALTER DATABASE regression
  RESET bdr.skip_ddl_replication;

\c regression
SELECT current_database();

-- Fixed.



-- Explicit "off" is Not OK
ALTER DATABASE regression
  SET bdr.skip_ddl_replication = off;

-- Unless at the system level
ALTER SYSTEM
  SET bdr.skip_ddl_replication = off;

ALTER SYSTEM
  RESET bdr.skip_ddl_replication;

-- Per-user is OK

ALTER USER super
  SET bdr.skip_ddl_replication = on;

-- Unless not permitted
ALTER USER super
  SET bdr.skip_ddl_replication = off;

ALTER USER super
  RESET bdr.skip_ddl_replication;

-- Per session is OK
SET bdr.skip_ddl_replication = on;

-- Unless values are not permitted
SET bdr.skip_ddl_replication = off;

RESET bdr.skip_ddl_replication;;
