\c postgres

-- No real way to test the sysid, so ignore it
SELECT timeline= 1, dboid = (SELECT oid FROM pg_database WHERE datname = current_database())
FROM bdr.bdr_get_local_nodeid();

SELECT current_database() = 'postgres';

-- Test probing for remote node information
SELECT
	r.sysid = l.sysid,
	r.timeline = l.timeline,
	r.dboid = (SELECT oid FROM pg_database WHERE datname = 'regression'),
	variant = bdr.bdr_variant(),
	version = bdr.bdr_version(),
	version_num = bdr.bdr_version_num(),
	min_remote_version_num = bdr.bdr_min_remote_version_num(),
	is_superuser = 't'
FROM bdr.bdr_get_remote_nodeinfo('dbname=regression') r,
     bdr.bdr_get_local_nodeid() l;

-- bdr.bdr_get_remote_nodeinfo can also be used to probe the local dsn
-- and make sure it works.
SELECT
    r.dboid = (SELECT oid FROM pg_database WHERE datname = current_database())
FROM bdr.bdr_get_remote_nodeinfo('dbname='||current_database()) r;

-- Test probing for replication connection
SELECT
	r.sysid = l.sysid,
	r.timeline = l.timeline,
	r.dboid = (SELECT oid FROM pg_database WHERE datname = 'regression')
FROM bdr.bdr_test_replication_connection('dbname=regression') r,
     bdr.bdr_get_local_nodeid() l;

-- Probing replication connection for the local dsn will work too
-- even though the identifier is the same.
SELECT
	r.dboid = (SELECT oid FROM pg_database WHERE datname = current_database())
FROM bdr.bdr_test_replication_connection('dbname='||current_database()) r;

-- Verify that parsing slot names then formatting them again produces round-trip
-- output.
WITH namepairs(orig, remote_sysid, remote_timeline, remote_dboid, local_dboid, replication_name, formatted)
AS (
  SELECT
    s.slot_name, p.*, bdr.bdr_format_slot_name(p.remote_sysid, p.remote_timeline, p.remote_dboid, p.local_dboid, '')
  FROM pg_catalog.pg_replication_slots s,
    LATERAL bdr.bdr_parse_slot_name(s.slot_name) p
)
SELECT orig, formatted
FROM namepairs
WHERE orig <> formatted;

-- Check the view mapping slot names to bdr nodes. We can't really examine the slot
-- name in the regresschecks, because it changes every run, so make sure we at least
-- find the expected nodes.
SELECT count(1) FROM (
    SELECT ns.node_name
	FROM bdr.bdr_nodes LEFT JOIN bdr.bdr_node_slots ns USING (node_name)
) q
WHERE node_name IS NULL;

-- Check to see if we can get the local node name
SELECT bdr.bdr_get_local_node_name() = 'node-pg';

