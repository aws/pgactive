-- Allow commands via ALTER SYSTEM SET, config file, ALTER DATABASE set, etc

ALTER SYSTEM
  SET bdr.skip_ddl_locking = on;
ALTER SYSTEM
  SET bdr.skip_ddl_replication = on;
ALTER SYSTEM
  SET bdr.permit_unsafe_ddl_commands = on;

-- The check for per-database settings only occurs when you're on that
-- database, so we don't block the setting on another DB and the user
-- has to undo it later.
SELECT current_database();

-- Should be ok
ALTER DATABASE postgres
  SET bdr.skip_ddl_locking = on;

-- Should fail
ALTER DATABASE postgres
  SET bdr.skip_ddl_locking = off;

-- An ERROR setting a GUC doesn't stop the connection to the DB
-- from succeeding though.
\c postgres
SELECT current_database();

ALTER DATABASE postgres
  RESET bdr.skip_ddl_locking;

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
  RESET bdr.skip_ddl_locking;

\c regression
SELECT current_database();

-- Fixed.



-- Explicit "off" is Not OK
ALTER DATABASE regression
  SET bdr.skip_ddl_locking = off;

-- Unless at the system level
ALTER SYSTEM
  SET bdr.skip_ddl_locking = off;

ALTER SYSTEM
  RESET bdr.skip_ddl_locking;

-- Per-user is OK

ALTER USER super
  SET bdr.skip_ddl_replication = on;

-- Unless not permitted
ALTER USER super
  SET bdr.skip_ddl_replication = off;

ALTER USER super
  RESET bdr.skip_ddl_replication;

-- Per session is OK
SET bdr.permit_unsafe_ddl_commands = on;
SET bdr.permit_unsafe_ddl_commands = off;
SET bdr.skip_ddl_replication = on;
SET bdr.skip_ddl_locking = on;
SET bdr.permit_ddl_locking = off;

-- Unless values are not permitted
SET bdr.skip_ddl_replication = off;
SET bdr.skip_ddl_locking = off;
SET bdr.permit_ddl_locking = on;

RESET bdr.permit_unsafe_ddl_commands;
RESET bdr.skip_ddl_replication;;
RESET bdr.skip_ddl_locking;
RESET bdr.permit_ddl_locking;
