/* pgactive--2.1.2--2.1.3.sql */

-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION pgactive UPDATE TO '2.1.3'" to load this file. \quit

SET pgactive.skip_ddl_replication = true;
-- Everything should assume the 'pgactive' prefix
SET LOCAL search_path = pgactive;

DROP FUNCTION IF EXISTS has_required_privs();

CREATE FUNCTION has_required_privs()
RETURNS boolean
AS 'MODULE_PATHNAME','_pgactive_has_required_privs'
LANGUAGE C STRICT;

REVOKE ALL ON FUNCTION has_required_privs() FROM public;

COMMENT ON FUNCTION has_required_privs() IS
'Checks if current user has required privileges.';

-- RESET pgactive.permit_unsafe_ddl_commands; is removed for now
RESET pgactive.skip_ddl_replication;
RESET search_path;
