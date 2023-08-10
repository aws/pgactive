-- This should be done with pg_regress's --create-role option
-- but it's blocked by bug 37906
CREATE USER nonsuper;
CREATE USER super SUPERUSER;

-- Can't because of bug 37906
--GRANT ALL ON DATABASE regress TO nonsuper;
--GRANT ALL ON DATABASE regress TO nonsuper;

\c postgres
GRANT ALL ON SCHEMA public TO nonsuper;
\c regression
GRANT ALL ON SCHEMA public TO nonsuper;

\c postgres
CREATE EXTENSION btree_gist;
CREATE EXTENSION bdr;

\c regression
CREATE EXTENSION btree_gist;
CREATE EXTENSION bdr;

-- DDL lock state before BDR comes up
SELECT * FROM bdr.bdr_get_global_locks_info();
