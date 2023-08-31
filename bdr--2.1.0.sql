-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION bdr" to load this file. \quit

--- We must be able to use exclusion constraints for global sequences among
--- other things.
-- SET bdr.permit_unsafe_ddl_commands = true; is removed for now

-- We don't want to replicate commands from in here
SET bdr.skip_ddl_replication = true;

CREATE SCHEMA bdr;
GRANT USAGE ON SCHEMA bdr TO public;

-- Everything should assume the 'bdr' prefix
SET LOCAL search_path = bdr;

CREATE FUNCTION bdr_version()
RETURNS TEXT
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION bdr_variant()
RETURNS TEXT
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION bdr_get_stats (
    OUT rep_node_id oid,
    OUT rilocalid oid,
    OUT riremoteid text,
    OUT nr_commit int8,
    OUT nr_rollback int8,
    OUT nr_insert int8,
    OUT nr_insert_conflict int8,
    OUT nr_update int8,
    OUT nr_update_conflict int8,
    OUT nr_delete int8,
    OUT nr_delete_conflict int8,
    OUT nr_disconnect int8
)
RETURNS SETOF record
AS 'MODULE_PATHNAME'
LANGUAGE C;

REVOKE ALL ON FUNCTION bdr_get_stats() FROM PUBLIC;

CREATE VIEW bdr_stats AS SELECT * FROM bdr_get_stats();

CREATE TYPE bdr_conflict_type AS ENUM (
    'insert_insert',
    'insert_update',
    'update_update',
    'update_delete',
    'delete_delete',
    'unhandled_tx_abort'
);

COMMENT ON TYPE bdr_conflict_type IS
'The nature of a BDR apply conflict - concurrent updates (update_update), conflicting inserts, etc.';

CREATE TYPE bdr_conflict_handler_action AS ENUM('IGNORE', 'ROW', 'SKIP');

CREATE TABLE bdr_conflict_handlers (
    ch_name NAME NOT NULL,
    ch_type bdr.bdr_conflict_type NOT NULL,
    ch_reloid oid NOT NULL,
    ch_fun text NOT NULL,
    ch_timeframe INTERVAL,
    PRIMARY KEY(ch_reloid, ch_name)
);
REVOKE ALL ON TABLE bdr_conflict_handlers FROM PUBLIC;
SELECT pg_catalog.pg_extension_config_dump('bdr_conflict_handlers', '');

CREATE INDEX bdr_conflict_handlers_ch_type_reloid_idx
    ON bdr_conflict_handlers(ch_reloid, ch_type);

CREATE FUNCTION bdr_create_conflict_handler (
    ch_rel REGCLASS,
    ch_name NAME,
    ch_proc REGPROCEDURE,
    ch_type bdr.bdr_conflict_type,
    ch_timeframe INTERVAL DEFAULT NULL
)
RETURNS VOID
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION bdr_drop_conflict_handler(ch_rel REGCLASS, ch_name NAME)
RETURNS VOID
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE VIEW bdr_list_conflict_handlers(ch_name, ch_type, ch_reloid, ch_fun) AS
	SELECT ch_name, ch_type, ch_reloid, ch_fun, ch_timeframe
	  FROM bdr_conflict_handlers;

CREATE TYPE bdr_conflict_resolution AS ENUM (
    'conflict_trigger_skip_change',
    'conflict_trigger_returned_tuple',
    'last_update_wins_keep_local',
    'last_update_wins_keep_remote',
    'apply_change',
    'skip_change',
    'unhandled_tx_abort'
);

COMMENT ON TYPE bdr_conflict_resolution IS
'Resolution of a bdr conflict - if a conflict was resolved by a conflict trigger, by last-update-wins tests on commit timestamps, etc.';

CREATE SEQUENCE bdr_conflict_history_id_seq;

--
-- bdr_conflict_history records apply conflicts so they can be queried and
-- analysed by administrators.
--
-- This must remain in sync with struct BdrApplyConflict and
-- bdr_conflict_log_table().
--
CREATE TABLE bdr_conflict_history (
    conflict_id         bigint not null default nextval('bdr_conflict_history_id_seq'),
    local_node_sysid    text not null, -- really uint64 but we don't have the type for it
    PRIMARY KEY (local_node_sysid, conflict_id),

    local_conflict_xid  xid not null,     -- xid of conflicting apply tx

    -- lsn of local node at the time the conflict was detected
    local_conflict_lsn  pg_lsn not null,
    local_conflict_time timestamptz not null,
    object_schema       text,
    object_name         text,
    remote_node_sysid   text not null, -- again, really uint64
    remote_txid         xid not null,
    remote_commit_time  timestamptz not null,
    remote_commit_lsn   pg_lsn not null,
    conflict_type       bdr_conflict_type not null,
    conflict_resolution bdr_conflict_resolution not null,
    local_tuple         json,
    remote_tuple        json,
    local_tuple_xmin    xid,
    local_tuple_origin_sysid text,        -- also really uint64

    -- The following apply only for unhandled apply errors and correspond to
    -- fields in ErrorData in elog.h .
    error_message       text,
    error_sqlstate      text CHECK (length(error_sqlstate) = 5),
    error_querystring   text,
    error_cursorpos     integer,
    error_detail        text,
    error_hint          text,
    error_context       text,

     -- schema and table in object_schema, object_name above
    error_columnname    text,
    error_typename      text,
    error_constraintname text,
    error_filename      text,
    error_lineno        integer,
    error_funcname      text,

    remote_node_timeline oid,
    remote_node_dboid   oid,
    local_tuple_origin_timeline oid,
    local_tuple_origin_dboid oid,
    local_commit_time   timestamptz
);
REVOKE ALL ON TABLE bdr_conflict_history FROM PUBLIC;
SELECT pg_catalog.pg_extension_config_dump('bdr_conflict_history', 'WHERE false');

ALTER SEQUENCE bdr_conflict_history_id_seq
  OWNED BY bdr_conflict_history.conflict_id;

COMMENT ON TABLE bdr_conflict_history IS 'Log of all conflicts in this BDR group';
COMMENT ON COLUMN bdr_conflict_history.local_node_sysid IS 'sysid of the local node where the apply conflict occurred';
COMMENT ON COLUMN bdr_conflict_history.remote_node_sysid IS 'sysid of the remote node the conflicting transaction originated from';
COMMENT ON COLUMN bdr_conflict_history.object_schema IS 'Schema of the object involved in the conflict';
COMMENT ON COLUMN bdr_conflict_history.object_name IS 'Name of the object (table, etc.) involved in the conflict';
COMMENT ON COLUMN bdr_conflict_history.local_conflict_xid IS 'Transaction ID of the apply transaction that encountered the conflict';
COMMENT ON COLUMN bdr_conflict_history.local_conflict_lsn IS 'xlog position at the time the conflict occured on the applying node';
COMMENT ON COLUMN bdr_conflict_history.local_conflict_time IS 'The time the conflict was detected on the applying node';
COMMENT ON COLUMN bdr_conflict_history.remote_txid IS 'xid of the remote transaction involved in the conflict';
COMMENT ON COLUMN bdr_conflict_history.remote_commit_time IS 'The time the remote transaction involved in this conflict committed';
COMMENT ON COLUMN bdr_conflict_history.remote_commit_lsn IS 'LSN on remote node at which conflicting transaction committed';
COMMENT ON COLUMN bdr_conflict_history.conflict_type IS 'Nature of the conflict - insert/insert, update/delete, etc.';
COMMENT ON COLUMN bdr_conflict_history.local_tuple IS 'For DML conflicts, the conflicting tuple from the local DB (as json), if logged';
COMMENT ON COLUMN bdr_conflict_history.local_tuple_xmin IS 'If local_tuple is set, the xmin of the conflicting local tuple';
COMMENT ON COLUMN bdr_conflict_history.local_tuple_origin_sysid IS 'The node id for the true origin of the local tuple. Differs from local_node_sysid if the tuple was originally replicated from another node';
COMMENT ON COLUMN bdr_conflict_history.remote_tuple IS 'For DML conflicts, the conflicting tuple from the remote DB (as json), if logged';
COMMENT ON COLUMN bdr_conflict_history.conflict_resolution IS 'How the conflict was resolved/handled; see the enum definition';
COMMENT ON COLUMN bdr_conflict_history.error_message IS 'On apply error, the error message from ereport/elog. Other error fields match.';
COMMENT ON COLUMN bdr_conflict_history.local_commit_time IS 'The time the local transaction involved in this conflict committed';

-- The bdr_nodes table tracks members of a BDR group; it's only concerned with
-- one bdr group so it only has to track enough to uniquely identify each
-- member node, which is the (sysid, timeline, dboid) tuple for that node.
--
-- The sysid must be a numeric (or string) because PostgreSQL has no uint64 SQL
-- type.
--
-- We don't exclude bdr_nodes with pg_extension_config_dump because this is a
-- global table that's sync'd between nodes.
--
CREATE TABLE bdr_nodes (
    node_sysid text not null, -- Really a uint64 but we have no type for that
    node_timeline oid not null,
    node_dboid oid not null,  -- This is an oid local to the node_sysid cluster
    node_status "char" not null,
    node_name text not null,
    node_local_dsn text,
    node_init_from_dsn text,
    node_read_only boolean default false,
    node_seq_id smallint,
    primary key(node_sysid, node_timeline, node_dboid),
    CHECK (node_status in ('b', 'i', 'c', 'o', 'r', 'k'))
);
REVOKE ALL ON TABLE bdr_nodes FROM PUBLIC;

-- bdr.bdr_nodes gets synced by bdr_sync_nodes(), it shouldn't be dumped and
-- applied.
SELECT pg_catalog.pg_extension_config_dump('bdr_nodes', 'WHERE false');

-- Add constrains ensuring node_names are unique and not null
CREATE UNIQUE INDEX bdr_nodes_node_name ON bdr_nodes(node_name);

COMMENT ON TABLE bdr_nodes IS 'All known nodes in this BDR group';
COMMENT ON COLUMN bdr_nodes.node_sysid IS 'BDR generated node identifier';
COMMENT ON COLUMN bdr_nodes.node_timeline IS 'Timeline ID of this node';
COMMENT ON COLUMN bdr_nodes.node_dboid IS 'Local database oid on the cluster (node_sysid, node_timeline)';
COMMENT ON COLUMN bdr_nodes.node_status IS 'Readiness of the node: [b]eginning setup, [i]nitializing, [c]atchup, creating [o]utbound slots, [r]eady, [k]illed. Doesn''t indicate connected/disconnected.';

CREATE TABLE bdr_global_locks (
    locktype text NOT NULL,

    owning_sysid text NOT NULL,
    owning_timeline oid NOT NULL,
    owning_datid oid NOT NULL,

    owner_created_lock_at pg_lsn NOT NULL,

    acquired_sysid text NOT NULL,
    acquired_timeline oid NOT NULL,
    acquired_datid oid NOT NULL,

    acquired_lock_at pg_lsn,

    state text NOT NULL
);
REVOKE ALL ON TABLE bdr_global_locks FROM PUBLIC;
SELECT pg_catalog.pg_extension_config_dump('bdr_global_locks', '');

CREATE UNIQUE INDEX bdr_global_locks_byowner
ON bdr_global_locks(locktype, owning_sysid, owning_timeline, owning_datid);

CREATE TABLE bdr_queued_commands (
    lsn pg_lsn NOT NULL,
    queued_at TIMESTAMP WITH TIME ZONE NOT NULL,
    perpetrator TEXT NOT NULL,
    command_tag TEXT NOT NULL,
    command TEXT NOT NULL,
    search_path TEXT DEFAULT ''
);
REVOKE ALL ON TABLE bdr_queued_commands FROM PUBLIC;
SELECT pg_catalog.pg_extension_config_dump('bdr_queued_commands', '');

CREATE FUNCTION bdr_replicate_ddl_command(cmd TEXT)
RETURNS VOID
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION bdr_truncate_trigger_add()
RETURNS event_trigger
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION bdr_internal_create_truncate_trigger(regclass)
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION bdr_queue_truncate()
RETURNS TRIGGER
AS 'MODULE_PATHNAME', 'bdr_queue_truncate'
LANGUAGE C;

-- This type is tailored to use as input to get_object_address
CREATE TYPE dropped_object AS (
    objtype text,
    objnames text[],
    objargs text[]
);

CREATE TABLE bdr_queued_drops (
    lsn pg_lsn NOT NULL,
    queued_at timestamptz NOT NULL,
    dropped_objects bdr.dropped_object[] NOT NULL
);
REVOKE ALL ON TABLE bdr_queued_drops FROM PUBLIC;
SELECT pg_catalog.pg_extension_config_dump('bdr_queued_drops', '');

CREATE FUNCTION bdr_apply_pause()
RETURNS VOID
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION bdr_apply_resume()
RETURNS VOID
AS 'MODULE_PATHNAME'
LANGUAGE C;

--- Functions for manipulating/displaying replications sets
CREATE FUNCTION bdr_get_table_replication_sets(relation regclass, OUT sets text[])
  VOLATILE
  STRICT
  LANGUAGE 'sql'
  AS $$
    SELECT
        ARRAY(
            SELECT *
            FROM json_array_elements_text(COALESCE((
                SELECT label::json->'sets'
                FROM pg_seclabel
                WHERE provider = 'bdr'
                     AND classoid = 'pg_class'::regclass
                     AND objoid = $1::regclass
                ), '["default"]'))
        )|| '{all}';
  $$;

CREATE TABLE bdr_replication_set_config (
    set_name name PRIMARY KEY,
    replicate_inserts bool NOT NULL DEFAULT true,
    replicate_updates bool NOT NULL DEFAULT true,
    replicate_deletes bool NOT NULL DEFAULT true
);
ALTER TABLE bdr_replication_set_config SET (user_catalog_table = true);

REVOKE ALL ON TABLE bdr_replication_set_config FROM PUBLIC;

-- Fix quoting for format() arguments by directly using regclass with %s
-- instead of %I
CREATE FUNCTION bdr_set_table_replication_sets(p_relation regclass, p_sets text[])
  RETURNS void
  VOLATILE
  LANGUAGE 'plpgsql'
-- remove bdr_permit_unsafe_commands and do not replace
-- by bdr_skip_ddl_replication for now
  SET search_path = ''
  AS $$
DECLARE
    v_label json;
	setting_value text;
BEGIN
    -- emulate STRICT for p_relation parameter
    IF p_relation IS NULL THEN
        RETURN;
    END IF;

    -- query current label
    SELECT label::json INTO v_label
      FROM pg_catalog.pg_seclabel
      WHERE provider = 'bdr'
        AND classoid = 'pg_class'::regclass
        AND objoid = p_relation;

    -- replace old 'sets' parameter with new value
    SELECT json_object_agg(key, value) INTO v_label
      FROM (
        SELECT key, value
        FROM json_each(v_label)
        WHERE key <> 'sets'
      UNION ALL
        SELECT
            'sets', to_json(p_sets)
        WHERE p_sets IS NOT NULL
    ) d;

    -- and now set the appropriate label
	-- bdr_replicate_ddl_command would fail if skip_ddl_replication is true

	SELECT setting INTO setting_value
		FROM pg_settings
		WHERE name = 'bdr.skip_ddl_replication';

	IF setting_value = 'on' or setting_value = 'true' THEN
		PERFORM format('SECURITY LABEL FOR bdr ON TABLE %s IS %L', p_relation, v_label);
	ELSE
		PERFORM bdr.bdr_replicate_ddl_command(format('SECURITY LABEL FOR bdr ON TABLE %s IS %L', p_relation, v_label));
	END IF;
END;
$$;

CREATE FUNCTION bdr_get_local_nodeid (
  sysid OUT text,
  timeline OUT oid,
  dboid OUT oid)
RETURNS record
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION bdr_version_num()
RETURNS integer
AS 'MODULE_PATHNAME'
LANGUAGE C;

COMMENT ON FUNCTION bdr_version_num() IS
'This BDR version represented as (major)*10^4 + (minor)*10^2 + (revision). The subrevision is not included. So 0.8.0.1 is 800';

CREATE FUNCTION bdr_min_remote_version_num()
RETURNS integer
AS 'MODULE_PATHNAME'
LANGUAGE C;

COMMENT ON FUNCTION bdr_min_remote_version_num() IS
'The oldest BDR version that this BDR extension can exchange data with';

CREATE FUNCTION bdr_get_remote_nodeinfo (
	dsn text,
  sysid OUT text,
  timeline OUT oid,
  dboid OUT oid,
	variant OUT text,
  version OUT text,
  version_num OUT integer,
	min_remote_version_num OUT integer,
  is_superuser OUT boolean,
  node_status OUT "char",
  node_name OUT text,
  dbname OUT text,
  dbsize OUT int8,
  max_nodes OUT integer,
  skip_ddl_replication OUT boolean,
  cur_nodes OUT integer,
  datcollate OUT text,
  datctype OUT text)
RETURNS record
AS 'MODULE_PATHNAME'
LANGUAGE C;

REVOKE ALL ON FUNCTION bdr_get_remote_nodeinfo(text) FROM public;

COMMENT ON FUNCTION bdr_get_remote_nodeinfo(text) IS
'Get node identity and BDR info from a remote server by dsn';

CREATE FUNCTION bdr_test_replication_connection (
  dsn text,
  sysid OUT text,
  timeline OUT oid,
  dboid OUT oid)
RETURNS record
AS 'MODULE_PATHNAME'
LANGUAGE C;

REVOKE ALL ON FUNCTION bdr_test_replication_connection(text) FROM public;

COMMENT ON FUNCTION bdr_test_replication_connection(text) IS
'Make a replication-mode connection to the specified DSN and get its node identity';

CREATE FUNCTION bdr_test_remote_connectback (
    remote_dsn text,
    local_dsn text,
    sysid OUT text,
    timeline OUT oid,
    dboid OUT oid,
    variant OUT text,
    version OUT text,
    version_num OUT integer,
    min_remote_version_num OUT integer,
    is_superuser OUT boolean)
RETURNS record
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

REVOKE ALL ON FUNCTION bdr_test_remote_connectback(text,text) FROM public;

COMMENT ON FUNCTION bdr_test_remote_connectback(text,text) IS
'Connect to remote_dsn and from there connect back to local_dsn and report nodeinfo for local_dsn';

CREATE TABLE bdr_connections (
    conn_sysid text not null,
    conn_timeline oid not null,
    conn_dboid oid not null,  -- This is an oid local to the node_sysid cluster

    -- Wondering why there's no FOREIGN KEY to bdr.bdr_nodes?
    --
    -- bdr.bdr_nodes won't be populated when the bdr.bdr_connections row gets
    -- created on the local node.

    -- These fields may later be used by BDR to override connection settings
    -- from one node to a particular other node. At the moment their main use
    -- is for UDR connections, where we must ensure that the connection is only
    -- made from one particular node.
    conn_origin_sysid text,
    conn_origin_timeline oid,
    conn_origin_dboid oid,

    PRIMARY KEY(conn_sysid, conn_timeline, conn_dboid,
                conn_origin_sysid, conn_origin_timeline, conn_origin_dboid),

    -- Either a whole origin ID (for an override or UDR entry) or no
    -- origin ID may be provided.
    CONSTRAINT origin_all_or_none_null
        CHECK ((conn_origin_sysid = '0') = (conn_origin_timeline = 0)
           AND (conn_origin_sysid = '0') = (conn_origin_dboid = 0)),

    conn_dsn text not null,

    conn_apply_delay integer
        CHECK (conn_apply_delay >= 0),

    conn_replication_sets text[]
);

REVOKE ALL ON TABLE bdr_connections FROM public;

COMMENT ON TABLE bdr_connections IS 'Connection information for nodes in the group. Don''t modify this directly, use the provided functions. One entry should exist per node in the group.';
COMMENT ON COLUMN bdr_connections.conn_sysid IS 'System identifer for the node this entry''s dsn refers to';
COMMENT ON COLUMN bdr_connections.conn_timeline IS 'System timeline ID for the node this entry''s dsn refers to';
COMMENT ON COLUMN bdr_connections.conn_dboid IS 'System database OID for the node this entry''s dsn refers to';
COMMENT ON COLUMN bdr_connections.conn_origin_sysid IS 'If set, ignore this entry unless the local sysid is this';
COMMENT ON COLUMN bdr_connections.conn_origin_timeline IS 'If set, ignore this entry unless the local timeline is this';
COMMENT ON COLUMN bdr_connections.conn_origin_dboid IS 'If set, ignore this entry unless the local dboid is this';
COMMENT ON COLUMN bdr_connections.conn_dsn IS 'A libpq-style connection string specifying how to make a connection to this node from other nodes';
COMMENT ON COLUMN bdr_connections.conn_apply_delay IS 'If set, milliseconds to wait before applying each transaction from the remote node. Mainly for debugging. If null, the global default applies.';
COMMENT ON COLUMN bdr_connections.conn_replication_sets IS 'Replication sets this connection should participate in, if non-default';

SELECT pg_catalog.pg_extension_config_dump('bdr_connections', 'WHERE false');

CREATE FUNCTION bdr_connections_changed()
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C;

REVOKE ALL ON FUNCTION bdr_connections_changed() FROM public;

COMMENT ON FUNCTION bdr_connections_changed() IS
'Internal BDR function, do not call directly';

--
-- This is a helper for node_join, for internal use only. It's called on the
-- remote end by the init code when joining an existing group, to do the
-- remote-side setup.
--
CREATE FUNCTION _bdr_join_node_private (
    sysid text, timeline oid, dboid oid,
    node_external_dsn text,
    apply_delay integer,
    replication_sets text[]
    )
RETURNS void LANGUAGE plpgsql VOLATILE
SET search_path = bdr, pg_catalog
AS
$body$
DECLARE
    status "char";
BEGIN
    LOCK TABLE bdr.bdr_connections IN EXCLUSIVE MODE;
    LOCK TABLE pg_catalog.pg_shseclabel IN EXCLUSIVE MODE;

    IF bdr_variant() <> 'BDR' THEN
        RAISE USING
            MESSAGE = 'full BDR required but this module is built for '||bdr_variant(),
            DETAIL = 'The target node is running something other than full BDR so you cannot join a BDR node to it.',
            HINT = 'Install full BDR if possible or use the UDR functions.',
            ERRCODE = 'feature_not_supported';
    END IF;

    -- Assert that the joining node has a bdr_nodes entry with state = i on this join-target node
    SELECT INTO status
    FROM bdr.bdr_nodes
    WHERE node_sysid = sysid
      AND node_timeline = timeline
      AND node_dboid = dboid;

    IF NOT FOUND THEN
        RAISE object_not_in_prerequisite_state
              USING MESSAGE = format('bdr.bdr_nodes entry for (%s,%s,%s) not found',
                                     sysid, timeline, dboid);
    END IF;

    IF status <> 'i' THEN
        RAISE object_not_in_prerequisite_state
              USING MESSAGE = format('bdr.bdr_nodes entry for (%s,%s,%s) has unexpected status %L (expected ''i'')',
                                     sysid, timeline, dboid, status);
    END IF;

    -- Insert or Update the connection info on this node, which we must be
    -- initing from.
    -- No need to care about concurrency here as we hold EXCLUSIVE LOCK.
    BEGIN
        INSERT INTO bdr.bdr_connections
        (conn_sysid, conn_timeline, conn_dboid,
         conn_origin_sysid, conn_origin_timeline, conn_origin_dboid,
         conn_dsn,
         conn_apply_delay, conn_replication_sets)
        VALUES
        (sysid, timeline, dboid,
         '0', 0, 0,
         node_external_dsn,
         CASE WHEN apply_delay = -1 THEN NULL ELSE apply_delay END,
         replication_sets);
    EXCEPTION WHEN unique_violation THEN
        UPDATE bdr.bdr_connections
        SET conn_dsn = node_external_dsn,
            conn_apply_delay = CASE WHEN apply_delay = -1 THEN NULL ELSE apply_delay END,
            conn_replication_sets = replication_sets
        WHERE conn_sysid = sysid
          AND conn_timeline = timeline
          AND conn_dboid = dboid
          AND conn_origin_sysid = '0'
          AND conn_origin_timeline = 0
          AND conn_origin_dboid = 0;
    END;

    -- Schedule the apply worker launch for commit time
    PERFORM bdr.bdr_connections_changed();
END;
$body$;

CREATE FUNCTION _bdr_update_seclabel_private()
RETURNS void LANGUAGE plpgsql
SET search_path = bdr, pg_catalog
-- SET bdr.permit_unsafe_ddl_commands = on is removed for now
SET bdr.skip_ddl_replication = on
-- SET bdr.skip_ddl_locking = on is removed for now
AS $body$
DECLARE
    v_label json;
BEGIN
    -- Update 'bdr' parameter in the current label if there's one. (Right now,
    -- there's not much point to this but later we'll be possibly have more
    -- information in there.)

    -- First, select existing label
    SELECT label::json INTO v_label
    FROM pg_catalog.pg_shseclabel
    WHERE provider = 'bdr'
      AND classoid = 'pg_database'::regclass
      AND objoid = (SELECT oid FROM pg_database WHERE datname = current_database());

    -- Then, replace 'bdr' with 'bdr'::true
    SELECT json_object_agg(key, value) INTO v_label
    FROM (
        SELECT key, value
        FROM json_each(v_label)
        WHERE key <> 'bdr'
      UNION ALL
        SELECT 'bdr', to_json(true)
    ) d;

    -- And, set the newly computed label (It's safe to do this early, it won't
    -- take effect until commit).
    EXECUTE format('SECURITY LABEL FOR bdr ON DATABASE %I IS %L',
                   current_database(), v_label);
END;
$body$;

CREATE FUNCTION _bdr_begin_join_private (
    caller text,
    local_node_name text,
    node_local_dsn text,
    remote_dsn text,
    remote_sysid OUT text,
    remote_timeline OUT oid,
    remote_dboid OUT oid,
    bypass_collation_checks boolean,
    bypass_node_identifier_creation boolean
)
RETURNS record LANGUAGE plpgsql VOLATILE
SET search_path = bdr, pg_catalog
-- SET bdr.permit_unsafe_ddl_commands = on is removed for now
SET bdr.skip_ddl_replication = on
-- SET bdr.skip_ddl_locking = on is removed for now
AS $body$
DECLARE
    localid RECORD;
    localid_from_dsn RECORD;
    remote_nodeinfo RECORD;
    remote_nodeinfo_r RECORD;
	  cur_node RECORD;
    local_max_node_value integer;
    local_skip_ddl_replication_value boolean;
    local_db_collation_info_r RECORD;
    collation_errmsg text;
    collation_hintmsg text;
BEGIN
    -- Only one tx can be adding connections
    LOCK TABLE bdr.bdr_connections IN EXCLUSIVE MODE;
    LOCK TABLE bdr.bdr_nodes IN EXCLUSIVE MODE;
    LOCK TABLE pg_catalog.pg_shseclabel IN EXCLUSIVE MODE;

    -- Generate BDR node identifier if asked
    IF bypass_node_identifier_creation THEN
      RAISE WARNING USING
        MESSAGE = 'skipping creation of BDR node identifier for this node',
        HINT = 'The ''bypass_node_identifier_creation'' option is only available for bdr_init_copy tool.';
    ELSE
      PERFORM bdr._bdr_generate_node_identifier_private();
    END IF;

    SELECT sysid, timeline, dboid INTO localid
    FROM bdr.bdr_get_local_nodeid();

    RAISE LOG USING MESSAGE = format('node identity of node being created is (%s,%s,%s)', localid.sysid, localid.timeline, localid.dboid);

    -- If there's already an entry for ourselves in bdr.bdr_connections then we
    -- know this node is part of an active BDR group and cannot be joined to
    -- another group.
    PERFORM 1 FROM bdr_connections
    WHERE conn_sysid = localid.sysid
      AND conn_timeline = localid.timeline
      AND conn_dboid = localid.dboid
      AND (conn_origin_sysid = '0'
           AND conn_origin_timeline = 0
           AND conn_origin_dboid = 0);

    IF FOUND THEN
        RAISE USING
            MESSAGE = 'this node is already a member of a BDR group',
            HINT = 'Connect to the node you wish to add and run '||caller||' from it instead.',
            ERRCODE = 'object_not_in_prerequisite_state';
    END IF;

    -- Validate that the local connection is usable and matches the node
    -- identity of the node we're running on.
    --
    -- For BDR this will NOT check the 'dsn' if 'node_local_dsn' gets supplied.
    -- We don't know if 'dsn' is even valid for loopback connections and can't
    -- assume it is. That'll get checked later by BDR specific code.
    --
    -- We'll get a null node name back at this point since we haven't inserted
    -- our nodes record (and it wouldn't have committed yet if we had).
    --
    SELECT * INTO localid_from_dsn
    FROM bdr_get_remote_nodeinfo(node_local_dsn);

    IF localid_from_dsn.sysid <> localid.sysid
        OR localid_from_dsn.timeline <> localid.timeline
        OR localid_from_dsn.dboid <> localid.dboid
    THEN
        RAISE USING
            MESSAGE = 'node identity for local dsn does not match current node',
            DETAIL = format($$The dsn '%s' connects to a node with identity (%s,%s,%s) but the local node is (%s,%s,%s)$$,
                node_local_dsn, localid_from_dsn.sysid, localid_from_dsn.timeline,
                localid_from_dsn.dboid, localid.sysid, localid.timeline, localid.dboid),
            HINT = 'The node_local_dsn (or, for bdr, dsn if node_local_dsn is null) parameter must refer to the node you''re running this function from.',
            ERRCODE = 'object_not_in_prerequisite_state';
    END IF;

    IF NOT localid_from_dsn.is_superuser THEN
        RAISE USING
            MESSAGE = 'local dsn does not have superuser rights',
            DETAIL = format($$The dsn '%s' connects successfully but does not grant superuser rights.$$, node_local_dsn),
            ERRCODE = 'object_not_in_prerequisite_state';
    END IF;

    -- Now interrogate the remote node, if specified, and sanity check its
    -- connection too. The discovered node identity is returned if found.
    --
    -- This will error out if there are issues with the remote node.
    IF remote_dsn IS NOT NULL THEN
        SELECT * INTO remote_nodeinfo
        FROM bdr_get_remote_nodeinfo(remote_dsn);

        remote_sysid := remote_nodeinfo.sysid;
        remote_timeline := remote_nodeinfo.timeline;
        remote_dboid := remote_nodeinfo.dboid;

        IF NOT remote_nodeinfo.is_superuser THEN
            RAISE USING
                MESSAGE = 'connection to remote node does not have superuser rights',
                DETAIL = format($$The dsn '%s' connects successfully but does not grant superuser rights.$$, remote_dsn),
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

        IF remote_nodeinfo.version_num < bdr_min_remote_version_num() THEN
            RAISE USING
                MESSAGE = 'remote node''s BDR version is too old',
                DETAIL = format($$The dsn '%s' connects successfully but the remote node version %s is less than the required version %s.$$,
                    remote_dsn, remote_nodeinfo.version_num, bdr_min_remote_version_num()),
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

        IF remote_nodeinfo.min_remote_version_num > bdr_version_num() THEN
            RAISE USING
                MESSAGE = 'remote node''s BDR version is too new or this node''s version is too old',
                DETAIL = format($$The dsn '%s' connects successfully but the remote node version %s requires this node to run at least bdr %s, not the current %s.$$,
                    remote_dsn, remote_nodeinfo.version_num, remote_nodeinfo.min_remote_version_num,
                    bdr_min_remote_version_num()),
                ERRCODE = 'object_not_in_prerequisite_state';

        END IF;

        IF remote_nodeinfo.node_status IS NULL THEN
            RAISE USING
                MESSAGE = 'remote node does not appear to be a fully running BDR node',
                DETAIL = format($$The dsn '%s' connects successfully but the target node has no entry in bdr.bdr_nodes.$$, remote_dsn),
                ERRCODE = 'object_not_in_prerequisite_state';
        ELSIF remote_nodeinfo.node_status IS DISTINCT FROM bdr.bdr_node_status_to_char('BDR_NODE_STATUS_READY') THEN
            RAISE USING
                MESSAGE = 'remote node does not appear to be a fully running BDR node',
                DETAIL = format($$The dsn '%s' connects successfully but the target node has bdr.bdr_nodes node_status=%s instead of expected 'r'.$$, remote_dsn, remote_nodeinfo.node_status),
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

        SELECT setting::integer INTO local_max_node_value FROM pg_settings
          WHERE name = 'bdr.max_nodes';

        IF local_max_node_value <> remote_nodeinfo.max_nodes THEN
            RAISE USING
                MESSAGE = 'joining node and BDR group have different values for bdr.max_nodes parameter',
                DETAIL = format('bdr.max_nodes value for joining node is ''%s'' and remote node is ''%s''.',
                                local_max_node_value, remote_nodeinfo.max_nodes),
                HINT = 'The parameter must be set to the same value on all BDR members.',
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

		-- using pg_file_settings here as bdr.skip_ddl_replication is SET to on when entering
		-- the function.
		SELECT setting::boolean INTO local_skip_ddl_replication_value FROM pg_file_settings
			WHERE name = 'bdr.skip_ddl_replication'
			ORDER BY seqno DESC
			LIMIT 1;

		IF local_skip_ddl_replication_value <> remote_nodeinfo.skip_ddl_replication THEN
			RAISE USING
				MESSAGE = 'joining node and BDR group have different values for bdr.skip_ddl_replication parameter',
				DETAIL = format('bdr.skip_ddl_replication value for joining node is ''%s'' and remote node is ''%s''.',
								local_skip_ddl_replication_value, remote_nodeinfo.skip_ddl_replication),
				HINT = 'The parameter must be set to the same value on all BDR members.',
				ERRCODE = 'object_not_in_prerequisite_state';
		END IF;

        IF local_max_node_value = remote_nodeinfo.cur_nodes THEN
            RAISE USING
                MESSAGE = 'cannot allow more than bdr.max_nodes number of nodes in a BDR group',
                HINT = 'Increase bdr.max_nodes parameter value on joining node as well as on all other BDR members.',
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

        SELECT datcollate, datctype FROM pg_database
          WHERE datname = current_database() INTO local_db_collation_info_r;

        IF local_db_collation_info_r.datcollate <> remote_nodeinfo.datcollate OR
           local_db_collation_info_r.datctype <> remote_nodeinfo.datctype THEN

          collation_errmsg := 'joining node and remote node have different database collation settings';
          collation_hintmsg := 'Use the same database collation settings for both nodes.';

          IF bypass_collation_checks THEN
            RAISE WARNING USING
              MESSAGE = collation_errmsg,
              HINT = collation_hintmsg,
              ERRCODE = 'object_not_in_prerequisite_state';
          ELSE
            RAISE EXCEPTION USING
              MESSAGE = collation_errmsg,
              HINT = collation_hintmsg,
              ERRCODE = 'object_not_in_prerequisite_state';
          END IF;
        END IF;
    END IF;

    -- Verify that we can make a replication connection to the remote node so
    -- that pg_hba.conf issues get caught early.
    IF remote_dsn IS NOT NULL THEN
        -- Returns (sysid, timeline, dboid) on success, else ERRORs
        SELECT * FROM bdr_test_replication_connection(remote_dsn)
        INTO remote_nodeinfo_r;

        IF (remote_nodeinfo_r.sysid, remote_nodeinfo_r.timeline, remote_nodeinfo_r.dboid)
            IS DISTINCT FROM
           (remote_sysid, remote_timeline, remote_dboid)
            AND
           (remote_sysid, remote_timeline, remote_dboid)
            IS DISTINCT FROM
           (NULL, NULL, NULL)
        THEN
            -- This just shouldn't happen, so no fancy error. The all-NULLs
            -- case can only arise when we're connecting to a peer with old BDR
            -- versions, where we can't get the sysid etc. from SQL.
            RAISE USING
                MESSAGE = 'replication and non-replication connections to remote node reported different node id';
        END IF;

        -- In case they're NULL because of bdr_get_remote_nodeinfo due to an
        -- old upstream
        remote_sysid := remote_nodeinfo_r.sysid;
        remote_timeline := remote_nodeinfo_r.timeline;
        remote_dboid := remote_nodeinfo_r.dboid;

    END IF;

    -- Create local node record so the apply worker knows to start initializing
    -- this node with bdr_init_replica when it's started.
    --
    -- bdr_init_copy might've created a node entry in catchup mode already, in
    -- which case we can skip this.
    SELECT node_status FROM bdr_nodes
    WHERE node_sysid = localid.sysid
      AND node_timeline = localid.timeline
      AND node_dboid = localid.dboid
    INTO cur_node;

    IF NOT FOUND THEN
        INSERT INTO bdr_nodes (
            node_name,
            node_sysid, node_timeline, node_dboid,
            node_status, node_local_dsn, node_init_from_dsn
        ) VALUES (
            local_node_name,
            localid.sysid, localid.timeline, localid.dboid,
            bdr.bdr_node_status_to_char('BDR_NODE_STATUS_BEGINNING_INIT'),
            node_local_dsn, remote_dsn
        );
    ELSIF bdr.bdr_node_status_from_char(cur_node.node_status) = 'BDR_NODE_STATUS_CATCHUP' THEN
        RAISE DEBUG 'starting node join in BDR_NODE_STATUS_CATCHUP';
    ELSE
        RAISE USING
            MESSAGE = 'a bdr_nodes entry for this node already exists',
            DETAIL = format('bdr.bdr_nodes entry for (%s,%s,%s) named ''%s'' with status %s exists.',
                            cur_node.node_sysid, cur_node.node_timeline, cur_node.node_dboid,
                            cur_node.node_name, bdr.bdr_node_status_from_char(cur_node.node_status)),
            ERRCODE = 'object_not_in_prerequisite_state';
    END IF;

    PERFORM bdr._bdr_update_seclabel_private();
END;
$body$;

--
-- The public interface for node join/addition, to be run to join a currently
-- unconnected node with a blank database to a BDR group.
--
CREATE FUNCTION bdr_join_group (
    local_node_name text,
    node_external_dsn text,
    join_using_dsn text,
    node_local_dsn text DEFAULT NULL,
    apply_delay integer DEFAULT NULL,
    replication_sets text[] DEFAULT ARRAY['default'],
    bypass_collation_checks boolean DEFAULT false,
    bypass_node_identifier_creation boolean DEFAULT false
    )
RETURNS void LANGUAGE plpgsql VOLATILE
SET search_path = bdr, pg_catalog
-- SET bdr.permit_unsafe_ddl_commands = on is removed for now
SET bdr.skip_ddl_replication = on
-- SET bdr.skip_ddl_locking = on is removed for now
AS $body$
DECLARE
    localid record;
    connectback_nodeinfo record;
    remoteinfo record;
BEGIN

    -- Prohibit enabling BDR where pglogical is installed
	IF (
		SELECT count(1)
		FROM pg_extension
		WHERE extname = 'pglogical'
		) > 0
	THEN
        RAISE USING
            MESSAGE = 'BDR can''t be enabled because an external logical replication extension is installed',
            ERRCODE = 'object_not_in_prerequisite_state',
            DETAIL = 'BDR doesn''t allow a node to pull in changes from more than one logical replication sources';
	END IF;

    -- Prohibit enabling BDR where a subscription exists
	IF (
		SELECT count(1)
		FROM pg_subscription
		WHERE subdbid = (SELECT oid
						 FROM pg_database
						 WHERE datname = current_database()
						)
		) > 0
	THEN
        RAISE USING
            MESSAGE = 'BDR can''t be enabled because a logical replication subscription is created',
            ERRCODE = 'object_not_in_prerequisite_state',
            DETAIL = 'BDR doesn''t allow a node to pull in changes from more than one logical replication sources';
	END IF;

    IF node_external_dsn IS NULL THEN
        RAISE USING
            MESSAGE = 'dsn may not be null',
            ERRCODE = 'invalid_parameter_value';
    END IF;

    IF bdr_variant() <> 'BDR' THEN
        RAISE USING
            MESSAGE = 'full BDR required but this module is built for '||bdr_variant(),
            DETAIL = 'The local node is not running full BDR, which is required to use bdr_join.',
            HINT = 'Install full BDR if possible or use the UDR functions.',
            ERRCODE = 'feature_not_supported';
    END IF;

    PERFORM bdr._bdr_begin_join_private(
        caller := '',
        local_node_name := local_node_name,
        node_local_dsn := CASE WHEN node_local_dsn IS NULL
          THEN node_external_dsn ELSE node_local_dsn END,
        remote_dsn := join_using_dsn,
        bypass_collation_checks := bypass_collation_checks,
        bypass_node_identifier_creation := bypass_node_identifier_creation);

    SELECT sysid, timeline, dboid INTO localid
    FROM bdr.bdr_get_local_nodeid();

    -- Request additional connection tests to determine that the remote is
    -- reachable for replication and non-replication mode and that the remote
    -- can connect back to us via 'dsn' on non-replication and replication
    -- modes.
    --
    -- This cannot be checked for the first node since there's no peer to ask
    -- for help.
    IF join_using_dsn IS NOT NULL THEN

        SELECT * INTO connectback_nodeinfo
        FROM bdr.bdr_test_remote_connectback(join_using_dsn, node_external_dsn);

        -- The connectback must actually match our local node identity and must
        -- provide a superuser connection.
        IF NOT connectback_nodeinfo.is_superuser THEN
            RAISE USING
                MESSAGE = 'node_external_dsn does not have superuser rights when connecting via remote node',
                DETAIL = format($$The dsn '%s' connects successfully but does not grant superuser rights.$$, dsn),
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

        IF (connectback_nodeinfo.sysid, connectback_nodeinfo.timeline, connectback_nodeinfo.dboid)
          IS DISTINCT FROM
           (localid.sysid, localid.timeline, localid.dboid)
          AND
           (connectback_nodeinfo.sysid, connectback_nodeinfo.timeline, connectback_nodeinfo.dboid)
          IS DISTINCT FROM
           (NULL, NULL, NULL) -- Returned by old versions' dummy functions
        THEN
            RAISE USING
                MESSAGE = 'node identity for node_external_dsn does not match current node when connecting back via remote',
                DETAIL = format($$The dsn '%s' connects to a node with identity (%s,%s,%s) but the local node is (%s,%s,%s).$$,
                    node_local_dsn, connectback_nodeinfo.sysid, connectback_nodeinfo.timeline,
                    connectback_nodeinfo.dboid, localid.sysid, localid.timeline, localid.dboid),
                HINT = 'The ''node_external_dsn'' parameter must refer to the node you''re running this function from, from the perspective of the node pointed to by join_using_dsn.',
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;
    END IF;

    -- Null/empty checks are skipped, the underlying constraints on the table
    -- will catch that for us.
    INSERT INTO bdr.bdr_connections (
        conn_sysid, conn_timeline, conn_dboid,
        conn_origin_sysid, conn_origin_timeline, conn_origin_dboid,
        conn_dsn, conn_apply_delay, conn_replication_sets
    ) VALUES (
        localid.sysid, localid.timeline, localid.dboid,
        '0', 0, 0,
        node_external_dsn, apply_delay, replication_sets
    );

    -- Now ensure the per-db worker is started if it's not already running.
    -- This won't actually take effect until commit time, it just adds a commit
    -- hook to start the worker when we commit.
    PERFORM bdr.bdr_connections_changed();
END;
$body$;

COMMENT ON FUNCTION bdr_join_group(text, text, text, text, integer, text[], boolean, boolean) IS
'Join an existing BDR group by connecting to a member node and copying its contents';

CREATE FUNCTION bdr_create_group (
    local_node_name text,
    node_external_dsn text,
    node_local_dsn text DEFAULT NULL,
    apply_delay integer DEFAULT NULL,
    replication_sets text[] DEFAULT ARRAY['default']
    )
RETURNS void LANGUAGE plpgsql VOLATILE
SET search_path = bdr, pg_catalog
-- SET bdr.permit_unsafe_ddl_commands = on is removed for now
SET bdr.skip_ddl_replication = on
-- SET bdr.skip_ddl_locking = on is removed for now
AS $body$
DECLARE
	t record;
BEGIN

    -- Prohibit enabling BDR where exclusion constraints exist
    FOR t IN
        SELECT n.nspname, r.relname, c.conname, c.contype
        FROM pg_constraint c
          INNER JOIN pg_namespace n ON c.connamespace = n.oid
          INNER JOIN pg_class r ON c.conrelid = r.oid
          INNER JOIN LATERAL unnest(bdr.bdr_get_table_replication_sets(c.conrelid)) rs(rsname) ON (rs.rsname = ANY(replication_sets))
        WHERE c.contype = 'x'
          AND r.relpersistence = 'p'
          AND r.relkind = 'r'
          AND n.nspname NOT IN ('pg_catalog', 'bdr', 'information_schema')
    LOOP
        RAISE USING
            MESSAGE = 'BDR can''t be enabled because exclusion constraints exist on persistent tables that are not excluded from replication',
            ERRCODE = 'object_not_in_prerequisite_state',
            DETAIL = format('Table %I.%I has exclusion constraint %I.', t.nspname, t.relname, t.conname),
            HINT = 'Drop the exclusion constraint(s), change the table(s) to UNLOGGED if they don''t need to be replicated, or exclude the table(s) from the active replication set(s).';
    END LOOP;

    -- Warn users about secondary unique indexes
    FOR t IN
        SELECT n.nspname, r.relname, c.conname, c.contype
        FROM pg_constraint c
          INNER JOIN pg_namespace n ON c.connamespace = n.oid
          INNER JOIN pg_class r ON c.conrelid = r.oid
          INNER JOIN LATERAL unnest(bdr.bdr_get_table_replication_sets(c.conrelid)) rs(rsname) ON (rs.rsname = ANY(replication_sets))
        WHERE c.contype = 'u'
          AND r.relpersistence = 'p'
          AND r.relkind = 'r'
          AND n.nspname NOT IN ('pg_catalog', 'bdr', 'information_schema')
    LOOP
        RAISE WARNING USING
            MESSAGE = 'secondary unique constraint(s) exist on replicated table(s)',
            DETAIL = format('Table %I.%I has secondary unique constraint %I. This may cause unhandled replication conflicts.', t.nspname, t.relname, t.conname),
            HINT = 'Drop the secondary unique constraint(s), change the table(s) to UNLOGGED if they don''t need to be replicated, or exclude the table(s) from the active replication set(s).';
    END LOOP;

    -- Warn users about missing primary keys
    FOR t IN
        SELECT n.nspname, r.relname, c.conname
        FROM pg_class r INNER JOIN pg_namespace n ON r.relnamespace = n.oid
          LEFT OUTER JOIN pg_constraint c ON (c.conrelid = r.oid AND c.contype = 'p')
        WHERE n.nspname NOT IN ('pg_catalog', 'bdr', 'information_schema')
          AND relkind = 'r'
          AND relpersistence = 'p'
          AND c.oid IS NULL
    LOOP
        RAISE WARNING USING
            MESSAGE = format('table %I.%I has no PRIMARY KEY', t.nspname, t.relname),
            HINT = 'Tables without a PRIMARY KEY cannot be UPDATEd or DELETEd from, only INSERTed into. Add a PRIMARY KEY.';
    END LOOP;

    -- Create ON TRUNCATE triggers for BDR on existing tables
    -- See bdr_truncate_trigger_add for the matching event trigger for tables
    -- created after join.
    --
    -- The triggers may be created already because the bdr event trigger
    -- runs when the bdr extension is created, even if there's no active
    -- bdr connections yet, so tables created after the extension is created
    -- will get the trigger already. So skip tables that have a tg named
    -- 'truncate_trigger' calling proc 'bdr.bdr_queue_truncate'.
    FOR t IN
        SELECT r.oid AS relid
        FROM pg_class r
          INNER JOIN pg_namespace n ON (r.relnamespace = n.oid)
          LEFT JOIN pg_trigger tg ON (r.oid = tg.tgrelid AND tgname = 'truncate_trigger')
          LEFT JOIN pg_proc p ON (p.oid = tg.tgfoid AND p.proname = 'bdr_queue_truncate')
          LEFT JOIN pg_namespace pn ON (pn.oid = p.pronamespace AND pn.nspname = 'bdr')
        WHERE r.relpersistence = 'p'
          AND r.relkind = 'r'
          AND n.nspname NOT IN ('pg_catalog', 'bdr', 'information_schema')
          AND tg.oid IS NULL AND p.oid IS NULL and pn.oid IS NULL
    LOOP
        -- We use a C function here because in addition to trigger creation
        -- we must also mark it tgisinternal.
        PERFORM bdr.bdr_internal_create_truncate_trigger(t.relid);
    END LOOP;

    PERFORM bdr.bdr_join_group(
        local_node_name := local_node_name,
        node_external_dsn := node_external_dsn,
        join_using_dsn := null,
        node_local_dsn := node_local_dsn,
        apply_delay := apply_delay,
        replication_sets := replication_sets);
END;
$body$;

COMMENT ON FUNCTION bdr_create_group(text, text, text, integer, text[]) IS
'Create a BDR group, turning a stand-alone database into the first node in a BDR group';

CREATE FUNCTION bdr_detach_nodes(p_nodes text[])
RETURNS void LANGUAGE plpgsql VOLATILE
SET search_path = bdr, pg_catalog
-- SET bdr.permit_unsafe_ddl_commands = on is removed for now
SET bdr.skip_ddl_replication = on
-- SET bdr.skip_ddl_locking = on is removed for now
AS $body$
DECLARE
    unknown_node_names text := NULL;
    r record;
BEGIN
    -- concurrency
    LOCK TABLE bdr.bdr_connections IN EXCLUSIVE MODE;
    LOCK TABLE bdr.bdr_nodes IN EXCLUSIVE MODE;
    LOCK TABLE pg_catalog.pg_shseclabel IN EXCLUSIVE MODE;

    -- Ensure we're not running on the node being detached.
    -- We can't safely ensure that the change gets replicated
    -- to peer nodes before we cut off our local connections
    -- if running on the node being detached.
    --
    -- (This restriction can be lifted later if we add
    --  multi-phase negotiated detach).
    --
    IF bdr.bdr_get_local_node_name() = ANY(p_nodes) THEN
        -- One exception is if we're the only live node
        IF (SELECT count(node_status) FROM bdr.bdr_nodes WHERE node_status IN (bdr.bdr_node_status_to_char('BDR_NODE_STATUS_READY'))) > 1 THEN
            RAISE USING
                MESSAGE = 'cannot detach a node from its self',
                DETAIL = 'Attempted to bdr_detach_nodes(...) on node '||bdr.bdr_get_local_node_name()||' which is one of the nodes being detached.',
                HINT = 'You must call call bdr_detach_nodes on a node that is not being removed.',
                ERRCODE = 'object_in_use';
        ELSE
            RAISE WARNING USING
                MESSAGE = 'detaching last node',
                HINT = 'Marking last node as detached. To remove BDR completely use bdr.bdr_remove(...).';
        END IF;
    END IF;

    SELECT
        string_agg(to_remove.remove_node_name, ', ')
    FROM
        bdr.bdr_nodes
        RIGHT JOIN unnest(p_nodes) AS to_remove(remove_node_name)
        ON (bdr_nodes.node_name = to_remove.remove_node_name)
    WHERE bdr_nodes.node_name IS NULL
    INTO unknown_node_names;

    IF unknown_node_names IS NOT NULL THEN
        RAISE USING
            MESSAGE = format('no node(s) named %s found', unknown_node_names),
            ERRCODE = 'no_data_found';
    END IF;

    FOR r IN
        SELECT
            node_name, node_status
        FROM
            bdr.bdr_nodes
            INNER JOIN unnest(p_nodes) AS to_remove(remove_node_name)
            ON (bdr_nodes.node_name = to_remove.remove_node_name)
        WHERE bdr_nodes.node_status <> bdr.bdr_node_status_to_char('BDR_NODE_STATUS_READY')
    LOOP
        IF r.node_status = bdr.bdr_node_status_to_char('BDR_NODE_STATUS_KILLED') THEN
            RAISE INFO 'node %i is already detached, ignoring', r.node_name;
        ELSE
            RAISE WARNING 'node % is in state % not expected ''r'' (BDR_NODE_STATUS_READY), attempting to remove anyway',
                r.node_name, r.node_status;
        END IF;
    END LOOP;

	DELETE from bdr.bdr_connections
	WHERE (conn_sysid, conn_timeline, conn_dboid)
	in (select node_sysid, node_timeline, node_dboid FROM
		bdr.bdr_nodes WHERE node_name = ANY(p_nodes));

    UPDATE bdr.bdr_nodes
    SET node_status = bdr.bdr_node_status_to_char('BDR_NODE_STATUS_KILLED')
    WHERE node_name = ANY(p_nodes);

    -- Notify local perdb worker to kill nodes.
    PERFORM bdr.bdr_connections_changed();
END;
$body$;

CREATE FUNCTION bdr_wait_for_node_ready(
  timeout integer DEFAULT 0,
  progress_interval integer DEFAULT 60)
RETURNS void LANGUAGE plpgsql VOLATILE
AS $body$
DECLARE
  r1 record;
  r2 record;
  t_lp_cnt integer := 0;
  p_lp_cnt integer := 0;
  first_time boolean := true;
  l_db_init_sz int8;
  l_db_sz int8;
  r_db text;
  p_pct integer;
  p_stime timestamp;
  p_etime timestamp;
  p_elapsed interval;
BEGIN

    IF timeout < 0 THEN
      RAISE EXCEPTION '''timeout'' parameter must not be negative';
    END IF;

    IF progress_interval < 0 THEN
      RAISE EXCEPTION '''progress_interval'' parameter must not be negative';
    END IF;

    IF current_setting('transaction_isolation') <> 'read committed' THEN
        RAISE EXCEPTION 'can only wait for node join in an ISOLATION LEVEL READ COMMITTED transaction, not %',
                        current_setting('transaction_isolation');
    END IF;

    LOOP
      SELECT * FROM bdr.bdr_nodes
      WHERE (node_sysid, node_timeline, node_dboid)
        = bdr.bdr_get_local_nodeid()
      INTO r1;

      PERFORM pg_sleep(1);

      IF r1.node_status = 'r' THEN
        IF progress_interval > 0 AND r2 IS NOT NULL THEN
          p_etime := clock_timestamp();
          p_elapsed := p_etime - p_stime;
          RAISE NOTICE
              USING MESSAGE = format('successfully restored database ''%s'' from node %s in %s',
                                     r2.dbname, r2.node_name, p_elapsed);
        END IF;
        EXIT;
      END IF;

      IF timeout > 0 THEN
        t_lp_cnt := t_lp_cnt + 1;
        IF t_lp_cnt > timeout THEN
          RAISE EXCEPTION 'node % cannot reach ready state within % seconds, current state is %',
                          r1.node_name, timeout, r1.node_status;
        END IF;
      END IF;

      IF progress_interval > 0 AND r1.node_init_from_dsn IS NOT NULL THEN
        p_lp_cnt := p_lp_cnt + 1;

        IF first_time THEN
          SELECT * FROM bdr.bdr_get_remote_nodeinfo(r1.node_init_from_dsn) INTO r2;
          SELECT pg_size_pretty(r2.dbsize) INTO r_db;
          SELECT pg_database_size(r1.node_dboid) INTO l_db_init_sz;
          p_stime := clock_timestamp();
          first_time := false;
        END IF;

        IF p_lp_cnt > progress_interval THEN
          SELECT pg_database_size(r1.node_dboid) INTO l_db_sz;
          IF l_db_sz = 0 OR l_db_sz = l_db_init_sz THEN
            RAISE NOTICE
                USING MESSAGE = format('transferring of database ''%s'' (%s) from node %s in progress',
                                       r2.dbname, r_db, r2.node_name);
          ELSE
            SELECT ROUND((l_db_sz::real/r2.dbsize::real) * 100.0) INTO p_pct;
            RAISE NOTICE
              USING MESSAGE = format('restoring database ''%s'', %s%% of %s complete',
                                     r2.dbname, p_pct, r_db);
          END IF;
          p_lp_cnt := 0;
        END IF;
      END IF;
    END LOOP;
END;
$body$;

CREATE TYPE bdr_sync_type AS ENUM ('none', 'full');

CREATE FUNCTION bdr_parse_slot_name (
    slot_name name,
    remote_sysid OUT text,
    remote_timeline OUT oid,
    remote_dboid OUT oid,
    local_dboid OUT oid,
    replication_name OUT name
)
RETURNS record
AS 'MODULE_PATHNAME','bdr_parse_slot_name_sql'
LANGUAGE C STRICT IMMUTABLE;

COMMENT ON FUNCTION bdr_parse_slot_name(name) IS
'Parse a slot name from the bdr plugin and report the embedded field values';

CREATE FUNCTION bdr_format_slot_name (
    remote_sysid text,
    remote_timeline oid,
    remote_dboid oid,
    local_dboid oid,
    replication_name name DEFAULT ''
)
RETURNS name
AS 'MODULE_PATHNAME','bdr_format_slot_name_sql'
LANGUAGE C STRICT IMMUTABLE;

COMMENT ON FUNCTION bdr_format_slot_name(text, oid, oid, oid, name) IS
'Format a BDR slot name from node identity parameters';

CREATE FUNCTION bdr_get_local_node_name() RETURNS text
LANGUAGE sql
AS $$
SELECT node_name
FROM bdr.bdr_nodes n,
     bdr.bdr_get_local_nodeid() i
WHERE n.node_sysid = i.sysid
  AND n.node_timeline = i.timeline
  AND n.node_dboid = i.dboid;
$$;

COMMENT ON FUNCTION bdr_get_local_node_name() IS
'Return the name from bdr.bdr_nodes for the local node, or null if no entry exists';

CREATE FUNCTION bdr_is_apply_paused()
RETURNS boolean
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION bdr_set_node_read_only (node_name text, read_only boolean)
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C VOLATILE;

CREATE FUNCTION bdr_drop_remote_slot(sysid text, timeline oid, dboid oid)
RETURNS boolean
AS 'MODULE_PATHNAME'
LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION bdr_get_workers_info (
    OUT sysid text,
    OUT timeline oid,
    OUT dboid oid,
    OUT worker_type text,
    OUT pid int4
)
RETURNS SETOF record
AS 'MODULE_PATHNAME'
LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION bdr_terminate_workers(text, oid, oid, text)
RETURNS boolean
LANGUAGE SQL
AS $$
SELECT pg_catalog.pg_terminate_backend(pid) FROM bdr.bdr_get_workers_info()
-- For per-db worker, we don't expect sysid and timeline, but rely on dboid.
  WHERE CASE WHEN worker_type = 'per-db' THEN (dboid, worker_type) = ($3, $4)
        ELSE (sysid, timeline, dboid, worker_type) = ($1, $2, $3, $4) END;
$$;

CREATE FUNCTION bdr_skip_changes(
    from_sysid text,
    from_timeline oid,
    from_dboid oid,
    upto_lsn pg_lsn)
RETURNS void
AS 'MODULE_PATHNAME','bdr_skip_changes'
LANGUAGE C;

CREATE FUNCTION bdr_get_connection_replication_sets(target_node_name text)
RETURNS text[]
LANGUAGE plpgsql
AS $$
DECLARE
  sysid text;
  timeline oid;
  dboid oid;
  replication_sets text[];
BEGIN
  SELECT node_sysid, node_timeline, node_dboid
  FROM bdr.bdr_nodes
  WHERE node_name = target_node_name
  INTO sysid, timeline, dboid;

  IF NOT FOUND THEN
    RAISE EXCEPTION 'no node with name % found in bdr.bdr_nodes',target_node_name;
  END IF;

  IF (
    SELECT count(1)
    FROM bdr.bdr_connections
    WHERE conn_sysid = sysid
      AND conn_timeline = timeline
      AND conn_dboid = dboid
    ) > 1
  THEN
    RAISE WARNING 'there are node-specific override entries for node % in bdr.bdr_connections. Only the default connection''s replication sets will be returned.',node_name;
  END IF;

  SELECT bdr.bdr_get_connection_replication_sets(sysid, timeline, dboid) INTO replication_sets;
  RETURN replication_sets;
END;
$$;

CREATE FUNCTION bdr_set_connection_replication_sets (
  replication_sets text[],
  target_node_name text
)
RETURNS void
LANGUAGE plpgsql
AS $$
DECLARE
  sysid text;
  timeline oid;
  dboid oid;
BEGIN
  SELECT node_sysid, node_timeline, node_dboid
  FROM bdr.bdr_nodes
  WHERE node_name = target_node_name
  INTO sysid, timeline, dboid;

  IF NOT FOUND THEN
    RAISE EXCEPTION 'no node with name % found in bdr.bdr_nodes',target_node_name;
  END IF;

  IF (
    SELECT count(1)
    FROM bdr.bdr_connections
    WHERE conn_sysid = sysid
      AND conn_timeline = timeline
      AND conn_dboid = dboid
    ) > 1
  THEN
    RAISE WARNING 'there are node-specific override entries for node % in bdr.bdr_connections. Only the default connection''s replication sets will be changed. Use the 6-argument form of this function to change others.',node_name;
  END IF;

  PERFORM bdr.bdr_set_connection_replication_sets(replication_sets, sysid, timeline, dboid);
END;
$$;

CREATE FUNCTION _bdr_pause_worker_management_private(boolean)
RETURNS void
AS 'MODULE_PATHNAME','bdr_pause_worker_management'
LANGUAGE C;

COMMENT ON FUNCTION _bdr_pause_worker_management_private(boolean) IS
'BDR-internal function for test use only';

CREATE FUNCTION bdr_parse_replident_name (
    replident text,
    remote_sysid OUT text,
    remote_timeline OUT oid,
    remote_dboid OUT oid,
    local_dboid OUT oid,
    replication_name OUT name
)
RETURNS record
AS 'MODULE_PATHNAME','bdr_parse_replident_name_sql'
LANGUAGE C STRICT IMMUTABLE;

COMMENT ON FUNCTION bdr_parse_replident_name(text) IS
'Parse a replication identifier name from the bdr plugin and report the embedded field values';

CREATE FUNCTION bdr_format_replident_name (
    remote_sysid text,
    remote_timeline oid,
    remote_dboid oid,
    local_dboid oid,
    replication_name name DEFAULT ''
)
RETURNS text
AS 'MODULE_PATHNAME','bdr_format_replident_name_sql'
LANGUAGE C STRICT IMMUTABLE;

COMMENT ON FUNCTION bdr_format_replident_name(text, oid, oid, oid, name) IS
'Format a BDR replication identifier name from node identity parameters';

CREATE FUNCTION _bdr_destroy_temporary_dump_directories_private()
RETURNS void
AS 'MODULE_PATHNAME','bdr_destroy_temporary_dump_directories'
LANGUAGE C STRICT;

REVOKE ALL ON FUNCTION _bdr_destroy_temporary_dump_directories_private() FROM public;

COMMENT ON FUNCTION _bdr_destroy_temporary_dump_directories_private() IS
'Remove temporary dump directories used for node initialization.';

-- Completely de-BDR-ize a node. Updated to fix #281.
CREATE FUNCTION bdr_remove (
  force boolean DEFAULT false)
RETURNS void
LANGUAGE plpgsql
-- SET bdr.skip_ddl_locking = on is removed for now
-- SET bdr.permit_unsafe_ddl_commands = on is removed for now
SET bdr.skip_ddl_replication = on
SET search_path = 'bdr,pg_catalog'
AS $$
DECLARE
  local_node_status "char";
  _seqschema name;
  _seqname name;
  _seqmax bigint;
  _tableoid oid;
  _truncate_tg record;
BEGIN

  SELECT node_status FROM bdr.bdr_nodes WHERE (node_sysid, node_timeline, node_dboid) = bdr.bdr_get_local_nodeid()
  INTO local_node_status;

  IF NOT (local_node_status = 'k' OR local_node_status IS NULL) THEN
    IF force THEN
      RAISE WARNING 'forcing deletion of possibly active BDR node';

      UPDATE bdr.bdr_nodes
      SET node_status = 'k'
      WHERE (node_sysid, node_timeline, node_dboid) = bdr.bdr_get_local_nodeid();

      PERFORM bdr._bdr_pause_worker_management_private(false);

      PERFORM pg_sleep(5);

      RAISE NOTICE 'node forced to detached state, now removing';
    ELSE
      RAISE EXCEPTION 'this BDR node might still be active, not removing';
    END IF;
  END IF;

  RAISE NOTICE 'removing BDR from node';

   -- Strip the database security label
  EXECUTE format('SECURITY LABEL FOR bdr ON DATABASE %I IS NULL', current_database());

  -- Suspend worker management, so when we terminate apply workers and
  -- walsenders they won't get relaunched.
  PERFORM bdr._bdr_pause_worker_management_private(true);

  -- Terminate WAL sender(s) associated with this database.
  PERFORM bdr.bdr_terminate_workers(node_sysid, node_timeline, node_dboid, 'walsender')
  FROM bdr.bdr_nodes
  WHERE (node_sysid, node_timeline, node_dboid) <> bdr.bdr_get_local_nodeid();

  -- Terminate apply worker(s) associated with this database.
  PERFORM bdr.bdr_terminate_workers(node_sysid, node_timeline, node_dboid, 'apply')
  FROM bdr.bdr_nodes
  WHERE (node_sysid, node_timeline, node_dboid) <> bdr.bdr_get_local_nodeid();

  -- Delete all connections and all nodes except the current one
  DELETE FROM bdr.bdr_connections
  WHERE (conn_sysid, conn_timeline, conn_dboid) <> bdr.bdr_get_local_nodeid();

  DELETE FROM bdr.bdr_nodes
  WHERE (node_sysid, node_timeline, node_dboid) <> bdr.bdr_get_local_nodeid();

  -- Let the perdb worker resume work and figure out everything's
  -- going away.
  PERFORM bdr._bdr_pause_worker_management_private(false);
  PERFORM bdr.bdr_connections_changed();

  -- Give it a few seconds
  PERFORM pg_sleep(2);

  -- Terminate per-db worker associated with this database.
  PERFORM bdr.bdr_terminate_workers(sysid, timeline, dboid, 'per-db')
    FROM bdr.bdr_get_local_nodeid();

  -- Clear out the rest of bdr_nodes and bdr_connections
  DELETE FROM bdr.bdr_nodes;
  DELETE FROM bdr.bdr_connections;

  -- Drop peer replication slots for this DB
  PERFORM pg_drop_replication_slot(slot_name)
  FROM pg_catalog.pg_replication_slots,
       bdr.bdr_parse_slot_name(slot_name) ps
  WHERE ps.local_dboid = (select oid from pg_database where datname = current_database())
       AND plugin = 'bdr';

  -- and replication origins
  PERFORM pg_replication_origin_drop(roname)
  FROM pg_catalog.pg_replication_origin,
       bdr.bdr_parse_replident_name(roname) pi
  WHERE pi.local_dboid = (select oid from pg_database where datname = current_database());

  -- Strip the security labels we use for replication sets from all the tables
  FOR _tableoid IN
    SELECT objoid
    FROM pg_catalog.pg_seclabel
    INNER JOIN pg_catalog.pg_class ON (pg_seclabel.objoid = pg_class.oid)
    WHERE provider = 'bdr'
      AND classoid = 'pg_catalog.pg_class'::regclass
      AND pg_class.relkind = 'r'
  LOOP
    -- regclass's text out adds quoting and schema qualification if needed
    EXECUTE format('SECURITY LABEL FOR bdr ON TABLE %s IS NULL', _tableoid::regclass);
  END LOOP;

  -- Drop the on-truncate triggers. They'd otherwise get cascade-dropped when
  -- the BDR extension was dropped, but this way the system is clean. We can't
  -- drop ones under the 'bdr' schema.
  FOR _truncate_tg IN
    SELECT
      n.nspname AS tgrelnsp,
      c.relname AS tgrelname,
      t.tgname AS tgname,
      d.objid AS tgobjid,
      d.refobjid AS tgrelid
    FROM pg_depend d
    INNER JOIN pg_class c ON (d.refclassid = 'pg_class'::regclass AND d.refobjid = c.oid)
    INNER JOIN pg_namespace n ON (c.relnamespace = n.oid)
    INNER JOIN pg_trigger t ON (d.classid = 'pg_trigger'::regclass and d.objid = t.oid)
    INNER JOIN pg_depend d2 ON (d.classid = d2.classid AND d.objid = d2.objid)
    WHERE tgname LIKE 'truncate_trigger_%'
      AND d2.refclassid = 'pg_proc'::regclass
      AND d2.refobjid = 'bdr.bdr_queue_truncate'::regproc
      AND n.nspname <> 'bdr'
  LOOP
    EXECUTE format('DROP TRIGGER %I ON %I.%I',
         _truncate_tg.tgname, _truncate_tg.tgrelnsp, _truncate_tg.tgrelname);

    -- The trigger' dependency entry will be dangling because of how we dropped
    -- it.
    DELETE FROM pg_depend
    WHERE classid = 'pg_trigger'::regclass AND
      (objid = _truncate_tg.tgobjid
       AND (refclassid = 'pg_proc'::regclass AND refobjid = 'bdr.bdr_queue_truncate'::regproc)
          OR
          (refclassid = 'pg_class'::regclass AND refobjid = _truncate_tg.tgrelid)
	  );

  END LOOP;

  -- Delete the other detritus from the extension. The user should really drop it,
  -- but we should try to restore a clean state anyway.
  DELETE FROM bdr.bdr_queued_commands;
  DELETE FROM bdr.bdr_queued_drops;
  DELETE FROM bdr.bdr_global_locks;
  DELETE FROM bdr.bdr_conflict_handlers;
  DELETE FROM bdr.bdr_conflict_history;
  DELETE FROM bdr.bdr_replication_set_config;

  PERFORM bdr._bdr_destroy_temporary_dump_directories_private();

  -- We can't drop the BDR extension, we just need to tell the user to do that.
  RAISE NOTICE 'BDR removed from this node. You can now DROP EXTENSION bdr and, if this is the last BDR node on this PostgreSQL instance, remove bdr from shared_preload_libraries.';
END;
$$;

REVOKE ALL ON FUNCTION bdr_remove(boolean) FROM public;

COMMENT ON FUNCTION bdr_remove(boolean) IS
'Remove all BDR security labels, slots, replication origins, replication sets, etc from the local node.';

CREATE FUNCTION bdr_is_active_in_db()
RETURNS boolean
AS 'MODULE_PATHNAME','bdr_is_active_in_db'
LANGUAGE C;

CREATE EVENT TRIGGER bdr_truncate_trigger_add
ON ddl_command_end
EXECUTE PROCEDURE bdr.bdr_truncate_trigger_add();

ALTER EVENT TRIGGER bdr_truncate_trigger_add ENABLE ALWAYS;

-- Marking this immutable is technically a bit cheeky as we could add new
-- statuses. But for index use we need it, and it's safe since any unrecognised
-- entries will result in ERRORs and can thus never exist in an index.
CREATE FUNCTION bdr_node_status_from_char("char")
RETURNS text
AS 'MODULE_PATHNAME','bdr_node_status_from_char'
LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION bdr_node_status_to_char(text)
RETURNS "char"
AS 'MODULE_PATHNAME','bdr_node_status_to_char'
LANGUAGE C STRICT IMMUTABLE;

-- BDR doesn't like partial unique indexes. We'd really like an index like:
--
--   CREATE UNIQUE INDEX ON bdr.bdr_nodes(node_seq_id) WHERE (node_status IN (bdr.bdr_node_status_to_char('BDR_NODE_STATUS_READY')));
--
-- But, the simple way we do updates to those catalogs doesn't support partial
-- or expression indexes. So no constraint enforces node ID uniqueness.

CREATE FUNCTION bdr_snowflake_id_nextval(regclass)
RETURNS bigint
AS 'MODULE_PATHNAME','bdr_snowflake_id_nextval_oid'
LANGUAGE C STRICT VOLATILE;

COMMENT ON FUNCTION bdr_snowflake_id_nextval(regclass) IS
'Generate sequence values unique to this node using a local sequence as a seed';

-- For testing purposes we sometimes want to be able to override the timestamp
-- etc.
CREATE FUNCTION _bdr_snowflake_id_nextval_private(regclass, bigint)
RETURNS bigint
AS 'MODULE_PATHNAME','bdr_snowflake_id_nextval_oid'
LANGUAGE C STRICT VOLATILE;

COMMENT ON FUNCTION _bdr_snowflake_id_nextval_private(regclass, bigint) IS
'Function for BDR testing only, do not use in application code';

CREATE FUNCTION bdr_acquire_global_lock(lockmode text)
RETURNS void
AS 'MODULE_PATHNAME','bdr_acquire_global_lock'
LANGUAGE C VOLATILE STRICT;

REVOKE ALL ON FUNCTION bdr_acquire_global_lock(text) FROM public;

COMMENT ON FUNCTION bdr_acquire_global_lock(text) IS
'Acquire bdr global lock ("ddl lock") in specified mode';

CREATE FUNCTION bdr_xact_replication_origin(xid)
RETURNS oid
AS 'MODULE_PATHNAME','bdr_xact_replication_origin'
LANGUAGE C;

REVOKE ALL ON FUNCTION bdr_xact_replication_origin(xid) FROM public;

COMMENT ON FUNCTION bdr_xact_replication_origin(xid) IS
'Get replication origin id for a given transaction';

--
-- When upgrading an existing cluster we must assign node sequence IDs.
--
-- We can't do that safely during the upgrade script run since the changes
-- won't get replicated, so we have do it as a user-initiated post-upgrade
-- step.
--
CREATE FUNCTION bdr_assign_seq_ids_post_upgrade()
RETURNS void LANGUAGE plpgsql AS
$$
DECLARE
  errd text;
  dofail boolean := false;
  n record;
BEGIN
  -- Refuse to run if it looks like there might be a dangling 'i' node or
  -- something.
  errd := 'One or more nodes have status other than expected BDR_NODE_STATUS_READY (r) or BDR_NODE_STATUS_KILLED (k): ';
  FOR n IN
 	  SELECT * FROM bdr.bdr_nodes WHERE node_status NOT IN (
      bdr.bdr_node_status_to_char('BDR_NODE_STATUS_READY'),
      bdr.bdr_node_status_to_char('BDR_NODE_STATUS_KILLED')
    )
  LOOP
    errd := errd || 'node % has status % (%);', n.node_name, n.node_status;
    dofail := true;
  END LOOP;
  IF dofail THEN
    RAISE USING
        MESSAGE = 'cannot upgrade BDR extension because some nodes are not ready',
        DETAIL = errd,
        HINT = 'Make sure no nodes are joining or partially joined in bdr.bdr_nodes.',
        ERRCODE = 'object_not_in_prerequisite_state';
  END IF;

  -- if all nodes look sensible, generate sequence IDs, skipping over any
  -- already-assigned values, and start counting from the lowest assigned
  -- value. In theory there shouldn't be one, but we don't actively stop users
  -- joining nodes when some other nodes have no node_seq_id, so there could
  -- be...
  UPDATE bdr.bdr_nodes
  SET node_seq_id = seqid
  FROM (
    SELECT
      n2.node_sysid, n2.node_timeline, n2.node_dboid,
      (
        row_number()
        OVER (ORDER BY n2.node_sysid, n2.node_timeline, n2.node_dboid)
        +
        coalesce((
          SELECT max(n3.node_seq_id)
          FROM bdr.bdr_nodes n3
          WHERE n3.node_status NOT IN (bdr.bdr_node_status_to_char('BDR_NODE_STATUS_KILLED'))
        ), 0)
      ) AS node_seq_id
    FROM bdr.bdr_nodes n2
    WHERE n2.node_status NOT IN (bdr.bdr_node_status_to_char('BDR_NODE_STATUS_KILLED'))
    AND n2.node_seq_id IS NULL
  ) n(sysid, timeline, dboid, seqid)
  WHERE (node_sysid, node_timeline, node_dboid) =
	(sysid, timeline, dboid)
  AND node_seq_id IS NULL;

END;
$$;

CREATE FUNCTION bdr_get_global_locks_info (
	OUT owner_replorigin oid,
	OUT owner_sysid text,
	OUT owner_timeline oid,
	OUT owner_dboid oid,
	OUT lock_mode text,
	OUT lock_state text,
	OUT owner_local_pid integer,
	/* rest is lower level diagnostic stuff */
	OUT lockcount integer,
	OUT npeers integer,
	OUT npeers_confirmed integer,
	OUT npeers_declined integer,
	OUT npeers_replayed integer,
	OUT replay_upto	pg_lsn)
RETURNS record
AS 'MODULE_PATHNAME', 'bdr_get_global_locks_info'
LANGUAGE C VOLATILE;

COMMENT ON FUNCTION bdr_get_global_locks_info() IS
'Backing function for bdr_global_locks_info view';

CREATE VIEW bdr_global_locks_info AS
SELECT
 owner_replorigin = 0 AS owner_is_my_node,
 owner_sysid, owner_timeline, owner_dboid,
 (SELECT node_name FROM bdr.bdr_nodes WHERE (node_sysid,node_timeline,node_dboid) = (owner_sysid, owner_timeline, owner_dboid)) AS owner_node_name,
 lock_mode, lock_state, owner_local_pid,
 coalesce(owner_local_pid = pg_backend_pid(),'f') AS owner_is_my_backend,
 owner_replorigin,
 lockcount, npeers, npeers_confirmed, npeers_declined, npeers_replayed,
 replay_upto
FROM bdr_get_global_locks_info();

COMMENT ON VIEW bdr_global_locks_info IS
'Diagnostic information on BDR global locking state, see manual';

CREATE FUNCTION bdr_wait_for_slots_confirmed_flush_lsn(slotname name, target pg_lsn)
RETURNS void
AS 'MODULE_PATHNAME','bdr_wait_for_slots_confirmed_flush_lsn'
LANGUAGE C;

COMMENT ON FUNCTION bdr_wait_for_slots_confirmed_flush_lsn(name,pg_lsn) IS
'Wait until slotname (or all slots, if null) has passed specified lsn (or current lsn, if null)';

CREATE FUNCTION bdr_handle_rejoin()
  RETURNS trigger AS
$$
BEGIN
-- Don't insert any rows on the re-joining node with a 'k' status.
-- That way, duplicated keys on the primary key or node_name are avoided.
 IF NEW.node_status = 'k' THEN
	RETURN NULL;

-- Adding a new node (could be the re-joining node)
 ELSIF NEW.node_status = 'i' THEN
-- We must ensure the delete done below on the other nodes matches the primary
-- key on the re-joining node (so update the primary key accordingly).
-- That way the delete can be propagated safely on the re-joining node.
	UPDATE bdr.bdr_nodes SET node_sysid = NEW.node_sysid
		WHERE node_status = 'k'
		AND node_timeline = NEW.node_timeline
		AND node_dboid = NEW.node_dboid
		AND node_name = NEW.node_name;
-- Delete the existing entry related to the re-joining node, so that it can be
-- re-inserted with the right status.
	DELETE FROM bdr.bdr_nodes
	WHERE node_status = 'k'
		  AND node_sysid = NEW.node_sysid
		  AND node_timeline = NEW.node_timeline
		  AND node_dboid = NEW.node_dboid;
 END IF;
 RETURN NEW;
END;$$
LANGUAGE 'plpgsql';

CREATE TRIGGER bdr_handle_rejoin_trigg
BEFORE INSERT
ON bdr.bdr_nodes
FOR EACH ROW
EXECUTE PROCEDURE bdr_handle_rejoin();

CREATE FUNCTION _bdr_generate_node_identifier_private()
RETURNS void
AS 'MODULE_PATHNAME','bdr_generate_node_identifier'
LANGUAGE C STRICT;

REVOKE ALL ON FUNCTION _bdr_generate_node_identifier_private() FROM PUBLIC;

COMMENT ON FUNCTION _bdr_generate_node_identifier_private()
IS 'Generate BDR node identifier and create its getter function';

CREATE FUNCTION bdr_get_node_identifier()
RETURNS numeric
AS 'MODULE_PATHNAME','bdr_get_node_identifier'
LANGUAGE C STRICT;

REVOKE ALL ON FUNCTION bdr_get_node_identifier() FROM PUBLIC;

COMMENT ON FUNCTION bdr_get_node_identifier()
IS 'Get BDR node identifier';

CREATE FUNCTION bdr_fdw_validator(
    options text[],
    catalog oid
)
RETURNS void
AS 'MODULE_PATHNAME', 'bdr_fdw_validator'
LANGUAGE C STRICT;

CREATE FOREIGN DATA WRAPPER bdr_fdw VALIDATOR bdr_fdw_validator;

CREATE FUNCTION bdr_conninfo_cmp(
  conninfo1 text,
  conninfo2 text
)
RETURNS boolean
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION bdr_update_node_conninfo (
    node_name_to_update text,
    node_dsn_to_update text
    )
RETURNS void LANGUAGE plpgsql VOLATILE
SET search_path = bdr, pg_catalog
AS $body$
DECLARE
  r record;
  updated_rows int; -- a variable to store the row count
BEGIN
  -- Only one tx can update node connection info
  LOCK TABLE bdr.bdr_nodes IN EXCLUSIVE MODE;
  LOCK TABLE bdr.bdr_connections IN EXCLUSIVE MODE;

  SELECT * FROM bdr.bdr_nodes WHERE node_name = node_name_to_update
    INTO r;

  IF NOT FOUND THEN
    RAISE EXCEPTION 'no node with name % found in bdr.bdr_nodes',
      node_name_to_update;
  END IF;

  -- Update node DSNs for all nodes that joined BDR group using passed-in node.
  UPDATE bdr.bdr_nodes SET node_init_from_dsn = node_dsn_to_update
    WHERE node_init_from_dsn IS NOT NULL AND
      bdr.bdr_conninfo_cmp(node_init_from_dsn, r.node_local_dsn) AND
      node_status = bdr.bdr_node_status_to_char('BDR_NODE_STATUS_READY');

  GET DIAGNOSTICS updated_rows = ROW_COUNT;
  IF updated_rows = 0 THEN
    RAISE EXCEPTION 'could not find any row in bdr.bdr_nodes to update node_init_from_dsn';
  END IF;

  -- Update node DSN for passed-in node.
  UPDATE bdr.bdr_nodes SET node_local_dsn = node_dsn_to_update
    WHERE node_name = node_name_to_update AND
      node_status = bdr.bdr_node_status_to_char('BDR_NODE_STATUS_READY');

  GET DIAGNOSTICS updated_rows = ROW_COUNT;
  IF updated_rows = 0 THEN
    RAISE EXCEPTION 'could not find any row in bdr.bdr_nodes to update node_local_dsn';
  END IF;

  -- Update node DSN for passed-in node in bdr.bdr_connections.
  UPDATE bdr.bdr_connections SET conn_dsn = node_dsn_to_update
    WHERE conn_sysid = r.node_sysid AND
      conn_timeline = r.node_timeline AND
      conn_dboid = r.node_dboid AND
      conn_dsn = r.node_local_dsn;

  GET DIAGNOSTICS updated_rows = ROW_COUNT;
  IF updated_rows = 0 THEN
    RAISE EXCEPTION 'could not find any row in bdr.bdr_connections to update conn_dsn';
  END IF;
END;
$body$;

REVOKE ALL ON FUNCTION bdr_update_node_conninfo(text, text) FROM public;

COMMENT ON FUNCTION bdr_update_node_conninfo(text, text) IS
'Updates a node connection info across BDR internal tables.';

CREATE FUNCTION get_last_applied_xact_info(
  sysid text,
  timeline oid,
  dboid oid,
  OUT last_applied_xact_id oid,
  OUT last_applied_xact_committs timestamptz,
  OUT last_applied_xact_at timestamptz
)
RETURNS record
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

REVOKE ALL ON FUNCTION get_last_applied_xact_info(text, oid, oid) FROM public;

COMMENT ON FUNCTION get_last_applied_xact_info(text, oid, oid) IS
'Gets last applied transaction info of apply worker for a given node.';

CREATE FUNCTION get_replication_lag_info(
    OUT slot_name name,
    OUT last_sent_xact_id oid,
    OUT last_sent_xact_committs timestamptz,
    OUT last_sent_xact_at timestamptz,
    OUT last_applied_xact_id oid,
    OUT last_applied_xact_committs timestamptz,
    OUT last_applied_xact_at timestamptz
)
RETURNS SETOF record
AS 'MODULE_PATHNAME'
LANGUAGE C VOLATILE STRICT;

REVOKE ALL ON FUNCTION get_replication_lag_info() FROM public;

COMMENT ON FUNCTION get_replication_lag_info() IS
'Gets replication lag info.';

CREATE VIEW bdr.bdr_node_slots AS
SELECT n.node_name,
 s.slot_name, s.restart_lsn AS slot_restart_lsn, s.confirmed_flush_lsn AS slot_confirmed_lsn,
 s.active AS walsender_active,
 s.active_pid AS walsender_pid,
 r.sent_lsn, r.write_lsn, r.flush_lsn, r.replay_lsn,
 l.last_sent_xact_id,
 l.last_sent_xact_committs,
 l.last_sent_xact_at,
 l.last_applied_xact_id,
 l.last_applied_xact_committs,
 l.last_applied_xact_at
FROM
 pg_catalog.pg_replication_slots s
 CROSS JOIN LATERAL bdr.bdr_parse_slot_name(s.slot_name) ps(remote_sysid, remote_timeline, remote_dboid, local_dboid, replication_name)
 INNER JOIN bdr.bdr_nodes n ON ((n.node_sysid = ps.remote_sysid) AND (n.node_timeline = ps.remote_timeline) AND (n.node_dboid = ps.remote_dboid))
 INNER JOIN bdr.get_replication_lag_info() l ON (l.slot_name = s.slot_name)
 LEFT JOIN pg_catalog.pg_stat_replication r ON (r.pid = s.active_pid)
WHERE ps.local_dboid = (select oid from pg_database where datname = current_database())
  AND s.plugin = 'bdr';

-- RESET bdr.permit_unsafe_ddl_commands; is removed for now
RESET bdr.skip_ddl_replication;
RESET search_path;
