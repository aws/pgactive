\c postgres

-- No real way to test the sysid, so ignore it
SELECT timeline = 0, dboid = (SELECT oid FROM pg_database WHERE datname = current_database())
FROM pgactive.pgactive_get_local_nodeid();

SELECT current_database() = 'postgres';

-- Test probing for replication connection and get node information of a given
-- dsn. Note that local node and remote node having different node identifiers
-- (r.sysid = l.sysid false) as each database gets unique pgactive node
-- identifier.
SELECT
	r.sysid = l.sysid,
	r.timeline = l.timeline,
	r.dboid = (SELECT oid FROM pg_database WHERE datname = 'regression'),
	variant = pgactive.pgactive_variant(),
	version = pgactive.pgactive_version(),
	version_num = pgactive.pgactive_version_num(),
	min_remote_version_num = pgactive.pgactive_min_remote_version_num(),
	has_required_privs = 't'
FROM pgactive._pgactive_get_node_info_private('dbname=regression') r,
     pgactive.pgactive_get_local_nodeid() l;

SELECT
    r.dboid = (SELECT oid FROM pg_database WHERE datname = current_database())
FROM pgactive._pgactive_get_node_info_private('dbname='||current_database()) r;

-- Verify that parsing slot names then formatting them again produces round-trip
-- output.
WITH namepairs(orig, remote_sysid, remote_timeline, remote_dboid, local_dboid, replication_name, formatted)
AS (
  SELECT
    s.slot_name, p.*, pgactive.pgactive_format_slot_name(p.remote_sysid, p.remote_timeline, p.remote_dboid, p.local_dboid, '')
  FROM pg_catalog.pg_replication_slots s,
    LATERAL pgactive.pgactive_parse_slot_name(s.slot_name) p
)
SELECT orig, formatted
FROM namepairs
WHERE orig <> formatted;

-- Check the view mapping slot names to pgactive nodes. We can't really examine the slot
-- name in the regresschecks, because it changes every run, so make sure we at least
-- find the expected nodes.
SELECT count(1) FROM (
    SELECT ns.node_name
	FROM pgactive.pgactive_nodes LEFT JOIN pgactive.pgactive_node_slots ns USING (node_name)
) q
WHERE node_name IS NULL;

-- Check to see if we can get the local node name
SELECT pgactive.pgactive_get_local_node_name() = 'node-pg';

-- Verify that creating/altering/dropping of pgactive node identifier getter
-- function is disallowed.

-- Must fail
CREATE OR REPLACE FUNCTION pgactive._pgactive_node_identifier_getter_private()
RETURNS numeric AS $$ SELECT '123456'::numeric $$
LANGUAGE SQL;

-- Must fail
ALTER FUNCTION pgactive._pgactive_node_identifier_getter_private STABLE;

-- Must fail
ALTER FUNCTION pgactive._pgactive_node_identifier_getter_private OWNER TO CURRENT_USER;

-- Must fail
ALTER FUNCTION pgactive._pgactive_node_identifier_getter_private RENAME TO alice;

-- Must fail
DROP FUNCTION pgactive._pgactive_node_identifier_getter_private();
