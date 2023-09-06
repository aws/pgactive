\c postgres

-- The DB name pgactive_supervisordb is reserved by pgactive. None
-- of these commands may be permitted.

CREATE DATABASE pgactive_supervisordb;

DROP DATABASE pgactive_supervisordb;

ALTER DATABASE pgactive_supervisordb RENAME TO someothername;

ALTER DATABASE regression RENAME TO pgactive_supervisordb;

-- We can connect to the supervisor db...
\c pgactive_supervisordb

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
