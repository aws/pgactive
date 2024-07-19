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

CREATE OR REPLACE FUNCTION pgactive_get_connection_replication_sets(
    sysid text, timeline oid, dboid oid,
    origin_sysid text default '0',
    origin_timeline oid default 0,
    origin_dboid oid default 0
)
RETURNS text[]
LANGUAGE plpgsql
AS $$
DECLARE
  found_sets text[];
BEGIN
  SELECT conn_replication_sets
  FROM pgactive.pgactive_connections
  WHERE conn_sysid = sysid
    AND conn_timeline = timeline
    AND conn_dboid = dboid
  INTO found_sets;

  IF NOT FOUND THEN
    RAISE EXCEPTION 'No pgactive.pgactive_connections entry found for node (%)',
      	sysid;
  END IF;

  RETURN found_sets;
END;
$$;

CREATE OR REPLACE FUNCTION pgactive_set_connection_replication_sets(
    new_replication_sets text[],
    sysid text, timeline oid, dboid oid,
    origin_sysid text default '0',
    origin_timeline oid default 0,
    origin_dboid oid default 0
)
RETURNS void
LANGUAGE plpgsql
AS $$
BEGIN
  UPDATE pgactive.pgactive_connections
  SET conn_replication_sets = new_replication_sets
  WHERE conn_sysid = sysid
    AND conn_timeline = timeline
    AND conn_dboid = dboid;

  IF NOT FOUND THEN
    RAISE EXCEPTION 'No pgactive.pgactive_connections entry found for node (%)',
      sysid;
  END IF;

  -- The other nodes will notice the change when they replay the new tuple; we
  -- only have to explicitly notify the local node.
  PERFORM pgactive.pgactive_connections_changed();
END;
$$;

-- RESET pgactive.permit_unsafe_ddl_commands; is removed for now
RESET pgactive.skip_ddl_replication;
RESET search_path;
