\c postgres

-- The DB name bdr_supervisordb is reserved by BDR. None
-- of these commands may be permitted.

CREATE DATABASE bdr_supervisordb;

DROP DATABASE bdr_supervisordb;

ALTER DATABASE bdr_supervisordb RENAME TO someothername;

ALTER DATABASE regression RENAME TO bdr_supervisordb;

-- We can connect to the supervisor db...
\c bdr_supervisordb

SET log_statement = 'all';

-- We actually did connect
SELECT current_database();

-- And do read-only work
SELECT 1;

-- but not do anything interesting
CREATE TABLE create_fails(id integer);

\d

-- except vacuum
VACUUM;
