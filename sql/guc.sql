-- Disallow unsafe commands via ALTER SYSTEM SET, config file, ALTER DATABASE set, etc

-- Should work
ALTER SYSTEM
  SET bdr.skip_ddl_locking = on;
ALTER SYSTEM
  SET bdr.skip_ddl_replication = on;
-- Should fail
ALTER SYSTEM
  SET bdr.permit_unsafe_ddl_commands = on;

-- The check for per-database settings only occurs when you're on that
-- database, so we don't block the setting on another DB and the user
-- has to undo it later.
SELECT current_database();

ALTER DATABASE postgres
  SET bdr.permit_unsafe_ddl_commands = on;

-- An ERROR setting a GUC doesn't stop the connection to the DB
-- from succeeding though.
\c postgres
SELECT current_database();

ALTER DATABASE postgres
  RESET bdr.permit_unsafe_ddl_commands;

\c postgres
SELECT current_database();

\c regression
SELECT current_database();

-- This is true even when you ALTER the current database, so this
-- commits fine, but switching back to the DB breaks:
ALTER DATABASE regression
  SET bdr.permit_unsafe_ddl_commands = on;

\c postgres
SELECT current_database();
-- so this will report an error, but we'll still successfully connect to the DB.
\c regression
SELECT current_database();

-- and fix the GUC
ALTER DATABASE regression
  RESET bdr.permit_unsafe_ddl_commands;

\c regression
SELECT current_database();

-- Fixed.



-- Explicit "off" is OK
ALTER DATABASE regression
  SET bdr.permit_unsafe_ddl_commands = off;

ALTER SYSTEM
  SET bdr.permit_unsafe_ddl_commands = off;

ALTER SYSTEM
  RESET bdr.permit_unsafe_ddl_commands;

-- Per-user is OK

ALTER USER super
  SET bdr.permit_unsafe_ddl_commands = on;

ALTER USER super
  SET bdr.permit_unsafe_ddl_commands = off;

ALTER USER super
  RESET bdr.permit_unsafe_ddl_commands;

-- Per session is OK
SET bdr.permit_unsafe_ddl_commands = on;
SET bdr.permit_unsafe_ddl_commands = off;
RESET bdr.permit_unsafe_ddl_commands;
