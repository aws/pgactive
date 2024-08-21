/* pgactive--2.1.3--2.1.4.sql */

-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION pgactive UPDATE TO '2.1.4'" to load this file. \quit

SET pgactive.skip_ddl_replication = true;
-- Everything should assume the 'pgactive' prefix
SET LOCAL search_path = pgactive;

-- Fix quoting for format() arguments by directly using regclass with %s
-- instead of %I
DROP FUNCTION pgactive_set_table_replication_sets(p_relation regclass, p_sets text[]);

CREATE FUNCTION pgactive_set_table_replication_sets(p_relation regclass, exclude_table boolean)
  RETURNS void
  VOLATILE
  LANGUAGE 'plpgsql'
-- remove pgactive_permit_unsafe_commands and do not replace
-- by pgactive_skip_ddl_replication for now
  SET search_path = ''
  AS $$
DECLARE
    v_label json;
	setting_value text;
	p_sets text[];
BEGIN
    -- emulate STRICT for p_relation parameter
    IF p_relation IS NULL THEN
        RETURN;
    END IF;

    -- Prohibit if not exactly one node (as we may need to update pgactive_connections)
	IF (
		SELECT count(1)
		FROM pgactive.pgactive_nodes
		WHERE node_status NOT IN (pgactive.pgactive_node_status_to_char('pgactive_NODE_STATUS_KILLED'))
		) != 1
	THEN
        RAISE USING
            MESSAGE = 'pgactive can''t exclude or include table from replication',
            ERRCODE = 'object_not_in_prerequisite_state',
            DETAIL = 'replication set exclude or include can only be performed after pgactive_create_group() and before pgactive_join_group()';
	END IF;

	IF (exclude_table) THEN
        -- Prohibit exclude if include has been done
		IF (
			SELECT count(1)
			FROM pgactive.pgactive_connections
			WHERE 'include_rs' = ANY(conn_replication_sets)
			) > 0
		THEN
             RAISE USING
                 MESSAGE = 'pgactive can''t exclude table from replication',
                 ERRCODE = 'object_not_in_prerequisite_state',
                 DETAIL = 'pgactive doesn''t allow exclude set setup when an include set has already been used';
		END IF;
		p_sets := '{exclude_rs}';
	ELSE
        -- Prohibit include if exclude has been done
		IF (
			SELECT count(1)
			FROM pg_seclabel
			WHERE label like '%exclude_rs%'
			) > 0
		THEN
             RAISE USING
                 MESSAGE = 'pgactive can''t include table from replication',
                 ERRCODE = 'object_not_in_prerequisite_state',
                 DETAIL = 'pgactive doesn''t allow an include set setup when exclude set has already been used';
		END IF;
		p_sets := '{include_rs}';
	END IF;

    -- query current label
    SELECT label::json INTO v_label
      FROM pg_catalog.pg_seclabel
      WHERE provider = 'pgactive'
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
    ) d;

    -- and now set the appropriate label
	-- pgactive_replicate_ddl_command would fail if skip_ddl_replication is true

	SELECT setting INTO setting_value
		FROM pg_settings
		WHERE name = 'pgactive.skip_ddl_replication';

	IF setting_value = 'on' or setting_value = 'true' THEN
		IF v_label IS NOT NULL THEN
			EXECUTE 'SECURITY LABEL FOR pgactive ON TABLE ' || p_relation || ' IS ' || pg_catalog.quote_literal(v_label);
		ELSE
			EXECUTE 'SECURITY LABEL FOR pgactive ON TABLE ' || p_relation || ' IS NULL';
		END IF;
	ELSE
		PERFORM pgactive.pgactive_replicate_ddl_command(format('SECURITY LABEL FOR pgactive ON TABLE %s IS %L', p_relation, v_label));
	END IF;

	IF (exclude_table IS FALSE) THEN
	  UPDATE pgactive.pgactive_connections SET conn_replication_sets = p_sets;
	  PERFORM pgactive.pgactive_connections_changed();
	END IF;
END;
$$;


CREATE OR REPLACE FUNCTION pgactive_exclude_table_replication_set(p_relation regclass)
RETURNS void
VOLATILE
LANGUAGE 'plpgsql'
-- remove pgactive_permit_unsafe_commands and do not replace
-- by pgactive_skip_ddl_replication for now
SET search_path = ''
AS $$
BEGIN
	PERFORM pgactive.pgactive_set_table_replication_sets(p_relation, true);
END;
$$;


CREATE OR REPLACE FUNCTION pgactive_include_table_replication_set(p_relation regclass)
RETURNS void
VOLATILE
LANGUAGE 'plpgsql'
-- remove pgactive_permit_unsafe_commands and do not replace
-- by pgactive_skip_ddl_replication for now
SET search_path = ''
AS $$
BEGIN
	PERFORM pgactive.pgactive_set_table_replication_sets(p_relation, false);
END;
$$;

DROP FUNCTION pgactive_get_connection_replication_sets(
    text[],
    text, oid, oid,
    text,
    oid,
    oid
);

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
  -- Prohibit if not exactly one node (as we may need to update pgactive_connections)
  IF (
      SELECT count(1)
      FROM pgactive.pgactive_nodes
      WHERE node_status NOT IN (pgactive.pgactive_node_status_to_char('pgactive_NODE_STATUS_KILLED'))
  ) != 1
  THEN
     RAISE USING
     MESSAGE = 'pgactive can''t set connection replication sets',
     ERRCODE = 'object_not_in_prerequisite_state',
     DETAIL = 'set connection replication sets can only be performed after pgactive_create_group() and before pgactive_join_group()';
  END IF;

  -- Prohibit setting conn_replication_sets to non default
  IF (new_replication_sets != '{default}')
  THEN
     RAISE USING
     MESSAGE = 'pgactive can''t set connection replication sets to non default value',
     ERRCODE = 'object_not_in_prerequisite_state',
     DETAIL = 'pgactive doesn''t allow to set connection replication sets but {default}';
  END IF;

  UPDATE pgactive.pgactive_connections
  SET conn_replication_sets = new_replication_sets
  WHERE conn_sysid = sysid
    AND conn_timeline = timeline
    AND conn_dboid = dboid;

  IF NOT FOUND THEN
    IF origin_timeline <> '0' OR origin_timeline <> 0 OR origin_dboid <> 0 THEN
      RAISE EXCEPTION 'No pgactive.pgactive_connections entry found from origin (%,%,%) to (%,%,%)',
		origin_sysid, origin_timeline, origin_dboid, sysid, timeline, dboid;
    ELSE
      RAISE EXCEPTION 'No pgactive.pgactive_connections entry found for (%,%,%) with default origin (0,0,0)',
		sysid, timeline, dboid;
    END IF;
  END IF;

  -- The other nodes will notice the change when they replay the new tuple; we
  -- only have to explicitly notify the local node.
  PERFORM pgactive.pgactive_connections_changed();
END;
$$;

DROP FUNCTION pgactive_get_workers_info();
CREATE FUNCTION pgactive_get_workers_info (
    OUT sysid text,
    OUT timeline oid,
    OUT dboid oid,
    OUT worker_type text,
    OUT pid int4,
    OUT unregistered boolean
)
RETURNS SETOF record
AS 'MODULE_PATHNAME'
LANGUAGE C VOLATILE STRICT;

DROP FUNCTION pgactive_terminate_workers(text, oid, oid, text);
CREATE OR REPLACE FUNCTION pgactive_terminate_workers(text, oid, oid, text)
RETURNS boolean
LANGUAGE SQL
AS $$
SELECT pg_catalog.pg_terminate_backend(pid) FROM pgactive.pgactive_get_workers_info()
-- For per-db worker, we don't expect sysid and timeline, but rely on dboid.
  WHERE unregistered = false AND
        CASE WHEN worker_type = 'per-db' THEN (dboid, worker_type) = ($3, $4)
        ELSE (sysid, timeline, dboid, worker_type) = ($1, $2, $3, $4) END;
$$;

REVOKE ALL ON FUNCTION pgactive_set_connection_replication_sets(text[], text, oid, oid, text, oid, oid) FROM public;
REVOKE ALL ON FUNCTION pgactive_get_workers_info() FROM public;
REVOKE ALL ON FUNCTION pgactive_terminate_workers(text, oid, oid, text) FROM public;

CREATE OR REPLACE FUNCTION pgactive_set_connection_replication_sets (
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
  -- Prohibit if not exactly one node (as we may need to update pgactive_connections)
  IF (
      SELECT count(1)
      FROM pgactive.pgactive_nodes
      WHERE node_status NOT IN (pgactive.pgactive_node_status_to_char('pgactive_NODE_STATUS_KILLED'))
  ) != 1
  THEN
     RAISE USING
     MESSAGE = 'pgactive can''t set connection replication sets',
     ERRCODE = 'object_not_in_prerequisite_state',
     DETAIL = 'set connection replication sets can only be performed after pgactive_create_group() and before pgactive_join_group()';
  END IF;

  -- Prohibit setting conn_replication_sets to non default
  IF (replication_sets != '{default}')
  THEN
     RAISE USING
     MESSAGE = 'pgactive can''t set connection replication sets to non default value',
     ERRCODE = 'object_not_in_prerequisite_state',
     DETAIL = 'pgactive doesn''t allow to set connection replication sets but {default}';
  END IF;

  SELECT node_sysid, node_timeline, node_dboid
  FROM pgactive.pgactive_nodes
  WHERE node_name = target_node_name
  INTO sysid, timeline, dboid;

  IF NOT FOUND THEN
    RAISE EXCEPTION 'no node with name % found in pgactive.pgactive_nodes',target_node_name;
  END IF;

  IF (
    SELECT count(1)
    FROM pgactive.pgactive_connections
    WHERE conn_sysid = sysid
      AND conn_timeline = timeline
      AND conn_dboid = dboid
    ) > 1
  THEN
    RAISE WARNING 'there are node-specific override entries for node % in pgactive.pgactive_connections. Only the default connection''s replication sets will be changed. Use the 6-argument form of this function to change others.',node_name;
  END IF;

  PERFORM pgactive.pgactive_set_connection_replication_sets(replication_sets, sysid, timeline, dboid);
END;
$$;

REVOKE ALL ON FUNCTION pgactive_set_connection_replication_sets(text[], text) FROM public;


--
-- The public interface for node join/addition, to be run to join a currently
-- unconnected node with a blank database to a pgactive group.
--
DROP FUNCTION pgactive_join_group(text, text, text, integer, text[], boolean, boolean, boolean);

CREATE FUNCTION pgactive.pgactive_join_group (
    node_name text,
    node_dsn text,
    join_using_dsn text,
    apply_delay integer DEFAULT NULL,
    replication_sets text[] DEFAULT ARRAY['default'],
    bypass_collation_check boolean DEFAULT false,
    bypass_node_identifier_creation boolean DEFAULT false,
    bypass_user_tables_check boolean DEFAULT false,
    data_only_node_init boolean DEFAULT false
    )
RETURNS void LANGUAGE plpgsql VOLATILE
SET search_path = pgactive, pg_catalog
-- SET pgactive.permit_unsafe_ddl_commands = on is removed for now
SET pgactive.skip_ddl_replication = on
-- SET pgactive.skip_ddl_locking = on is removed for now
AS $body$
DECLARE
    localid record;
    connectback_nodeinfo record;
    remoteinfo record;
	contains_include_rs boolean;
    current_dboid oid;
BEGIN

	contains_include_rs = false;
    -- Prohibit enabling pgactive where pglogical is installed
	IF (
		SELECT count(1)
		FROM pg_extension
		WHERE extname = 'pglogical'
		) > 0
	THEN
        RAISE USING
            MESSAGE = 'pgactive can''t be enabled because an external logical replication extension is installed',
            ERRCODE = 'object_not_in_prerequisite_state',
            DETAIL = 'pgactive doesn''t allow a node to pull in changes from more than one logical replication sources';
	END IF;

    -- Prohibit enabling pgactive where a subscription exists
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
            MESSAGE = 'pgactive can''t be enabled because a logical replication subscription is created',
            ERRCODE = 'object_not_in_prerequisite_state',
            DETAIL = 'pgactive doesn''t allow a node to pull in changes from more than one logical replication sources';
	END IF;

    IF node_dsn IS NULL THEN
        RAISE USING
            MESSAGE = 'node_dsn can not be null',
            ERRCODE = 'invalid_parameter_value';
    END IF;

  -- Prohibit enabling pgactive when there is an existing per-db worker.
  SELECT oid FROM pg_database
    WHERE datname = current_database() INTO current_dboid;
	IF (
		SELECT count(1)
		FROM pgactive.pgactive_get_workers_info()
		WHERE worker_type = 'per-db' AND dboid = current_dboid
		) > 0
	THEN
    RAISE USING
      MESSAGE = 'pgactive can''t be enabled because there is an existing per-db worker for the current database',
      ERRCODE = 'object_not_in_prerequisite_state';
  END IF;

    PERFORM pgactive._pgactive_begin_join_private(
        caller := '',
        node_name := node_name,
        node_dsn := node_dsn,
        remote_dsn := join_using_dsn,
        bypass_collation_check := bypass_collation_check,
        bypass_node_identifier_creation := bypass_node_identifier_creation,
        bypass_user_tables_check := bypass_user_tables_check,
        data_only_node_init := data_only_node_init);

    SELECT sysid, timeline, dboid INTO localid
    FROM pgactive.pgactive_get_local_nodeid();

    -- Request additional connection tests to determine that the remote is
    -- reachable for replication and non-replication mode and that the remote
    -- can connect back to us via 'dsn' on non-replication and replication
    -- modes.
    --
    -- This cannot be checked for the first node since there's no peer to ask
    -- for help.
    IF join_using_dsn IS NOT NULL THEN

        SELECT * INTO connectback_nodeinfo
        FROM pgactive._pgactive_get_node_info_private(node_dsn, join_using_dsn);

        -- The connectback must actually match our local node identity and must
        -- provide a connection that has required rights.
        IF NOT connectback_nodeinfo.has_required_privs THEN
            RAISE USING
                MESSAGE = 'node_dsn does not have required rights when connecting via remote node',
                DETAIL = format($$The dsn '%s' connects successfully but does not have required rights.$$, dsn),
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
                MESSAGE = 'node identity for node_dsn does not match current node when connecting back via remote',
                DETAIL = format($$The dsn '%s' connects to a node with identity (%s,%s,%s) but the local node is (%s,%s,%s).$$,
                    node_dsn, connectback_nodeinfo.sysid, connectback_nodeinfo.timeline,
                    connectback_nodeinfo.dboid, localid.sysid, localid.timeline, localid.dboid),
                HINT = 'The ''node_dsn'' parameter must refer to the node you''re running this function from, from the perspective of the node pointed to by join_using_dsn.',
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

		SELECT * INTO remoteinfo FROM
              _pgactive_get_node_info_private(join_using_dsn);

		IF (remoteinfo.nb_include_rs > 0) THEN
			contains_include_rs = true;
		END IF;

    END IF;

    -- Null/empty checks are skipped, the underlying constraints on the table
    -- will catch that for us.
    INSERT INTO pgactive.pgactive_connections (
        conn_sysid, conn_timeline, conn_dboid,
        conn_dsn, conn_apply_delay, conn_replication_sets
    ) VALUES (
        localid.sysid, localid.timeline, localid.dboid,
        node_dsn, apply_delay, replication_sets
    );

	IF (contains_include_rs)
    THEN
		UPDATE pgactive.pgactive_connections SET conn_replication_sets = '{include_rs}';
    END IF;

    -- Now ensure the per-db worker is started if it's not already running.
    -- This won't actually take effect until commit time, it just adds a commit
    -- hook to start the worker when we commit.
    PERFORM pgactive.pgactive_connections_changed();
END;
$body$;

COMMENT ON FUNCTION pgactive.pgactive_join_group(text, text, text, integer, text[], boolean, boolean, boolean, boolean) IS
'Join an existing pgactive group by connecting to a member node and copying its contents';

REVOKE ALL ON FUNCTION pgactive.pgactive_join_group(text, text, text, integer, text[], boolean, boolean, boolean, boolean) FROM public;

DROP FUNCTION _pgactive_get_node_info_private (text, text);

CREATE FUNCTION _pgactive_get_node_info_private (
	local_dsn text,
  remote_dsn text DEFAULT NULL,
  sysid OUT text,
  timeline OUT oid,
  dboid OUT oid,
	variant OUT text,
  version OUT text,
  version_num OUT integer,
	min_remote_version_num OUT integer,
  has_required_privs OUT boolean,
  node_status OUT "char",
  node_name OUT text,
  dbname OUT text,
  dbsize OUT int8,
  indexessize OUT int8,
  max_nodes OUT integer,
  skip_ddl_replication OUT boolean,
  nb_include_rs OUT integer,
  cur_nodes OUT integer,
  datcollate OUT text,
  datctype OUT text)
RETURNS record
AS 'MODULE_PATHNAME','pgactive_get_node_info'
LANGUAGE C;

REVOKE ALL ON FUNCTION _pgactive_get_node_info_private(text, text) FROM public;

COMMENT ON FUNCTION _pgactive_get_node_info_private(text, text) IS
'Verify both replication and non-replication connections to the given dsn and get node info; when specified remote_dsn ask remote node to connect back to local node';

DROP FUNCTION _pgactive_begin_join_private(text, text, text, text, boolean, boolean, boolean);
CREATE FUNCTION _pgactive_begin_join_private (
    caller text,
    node_name text,
    node_dsn text,
    remote_dsn text,
    remote_sysid OUT text,
    remote_timeline OUT oid,
    remote_dboid OUT oid,
    bypass_collation_check boolean,
    bypass_node_identifier_creation boolean,
    bypass_user_tables_check boolean,
    data_only_node_init boolean
)
RETURNS record LANGUAGE plpgsql VOLATILE
SET search_path = pgactive, pg_catalog
-- SET pgactive.permit_unsafe_ddl_commands = on is removed for now
SET pgactive.skip_ddl_replication = on
-- SET pgactive.skip_ddl_locking = on is removed for now
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
    data_dir text;
    temp_dump_dir text;
    same_file_system_mount_point boolean;
    free_disk_space1 int8;
    free_disk_space1_p text;
    free_disk_space2 int8;
    free_disk_space2_p text;
    remote_dbsize_p text;
    current_dboid oid;
BEGIN
    -- Only one tx can be adding connections
    LOCK TABLE pgactive.pgactive_connections IN EXCLUSIVE MODE;
    LOCK TABLE pgactive.pgactive_nodes IN EXCLUSIVE MODE;
    LOCK TABLE pg_catalog.pg_shseclabel IN EXCLUSIVE MODE;

    -- Generate pgactive node identifier if asked
    IF bypass_node_identifier_creation THEN
      RAISE WARNING USING
        MESSAGE = 'skipping creation of pgactive node identifier for this node',
        HINT = 'The ''bypass_node_identifier_creation'' option is only available for pgactive_init_copy tool.';
    ELSE
      PERFORM pgactive._pgactive_generate_node_identifier_private();
    END IF;

    SELECT sysid, timeline, dboid INTO localid
    FROM pgactive.pgactive_get_local_nodeid();

    RAISE LOG USING MESSAGE = format('node identity of node being created is (%s,%s,%s)', localid.sysid, localid.timeline, localid.dboid);

    -- If there's already an entry for ourselves in pgactive.pgactive_connections then we
    -- know this node is part of an active pgactive group and cannot be joined to
    -- another group.
    PERFORM 1 FROM pgactive_connections
    WHERE conn_sysid = localid.sysid
      AND conn_timeline = localid.timeline
      AND conn_dboid = localid.dboid;

    IF FOUND THEN
        RAISE USING
            MESSAGE = 'this node is already a member of a pgactive group',
            HINT = 'Connect to the node you wish to add and run '||caller||' from it instead.',
            ERRCODE = 'object_not_in_prerequisite_state';
    END IF;

    -- Validate that the local connection is usable and matches the node
    -- identity of the node we're running on.
    --
    -- For pgactive this will NOT check the 'dsn' if 'node_dsn' gets supplied.
    -- We don't know if 'dsn' is even valid for loopback connections and can't
    -- assume it is. That'll get checked later by pgactive specific code.
    --
    -- We'll get a null node name back at this point since we haven't inserted
    -- our nodes record (and it wouldn't have committed yet if we had).
    --
    SELECT * INTO localid_from_dsn
    FROM _pgactive_get_node_info_private(node_dsn);

    IF localid_from_dsn.sysid <> localid.sysid
        OR localid_from_dsn.timeline <> localid.timeline
        OR localid_from_dsn.dboid <> localid.dboid
    THEN
        RAISE USING
            MESSAGE = 'node identity for local dsn does not match current node',
            DETAIL = format($$The dsn '%s' connects to a node with identity (%s,%s,%s) but the local node is (%s,%s,%s)$$,
                node_dsn, localid_from_dsn.sysid, localid_from_dsn.timeline,
                localid_from_dsn.dboid, localid.sysid, localid.timeline, localid.dboid),
            HINT = 'The node_dsn parameter must refer to the node you''re running this function from.',
            ERRCODE = 'object_not_in_prerequisite_state';
    END IF;

    IF NOT localid_from_dsn.has_required_privs THEN
        RAISE USING
            MESSAGE = 'node_dsn does not have required rights',
            DETAIL = format($$The dsn '%s' connects successfully but does not have required rights.$$, node_dsn),
            ERRCODE = 'object_not_in_prerequisite_state';
    END IF;

    IF data_only_node_init THEN
        bypass_user_tables_check := true;
    END IF;

    IF NOT bypass_user_tables_check THEN
      PERFORM 1 FROM pg_class r
        INNER JOIN pg_namespace n ON r.relnamespace = n.oid
        WHERE n.nspname NOT IN ('pg_catalog', 'pgactive', 'information_schema')
        AND relkind = 'r' AND relpersistence = 'p';

      IF FOUND THEN
          RAISE USING
              MESSAGE = 'database joining pgactive group has existing user tables',
              HINT = 'Ensure no user tables in the database.',
              ERRCODE = 'object_not_in_prerequisite_state';
      END IF;
    END IF;

    -- Now interrogate the remote node, if specified, and sanity check its
    -- connection too. The discovered node identity is returned if found.
    --
    -- This will error out if there are issues with the remote node.
    IF remote_dsn IS NOT NULL THEN
        SELECT * INTO remote_nodeinfo
        FROM _pgactive_get_node_info_private(remote_dsn);

        remote_sysid := remote_nodeinfo.sysid;
        remote_timeline := remote_nodeinfo.timeline;
        remote_dboid := remote_nodeinfo.dboid;

        IF NOT remote_nodeinfo.has_required_privs THEN
            RAISE USING
                MESSAGE = 'connection to remote node does not have required rights',
                DETAIL = format($$The dsn '%s' connects successfully but does not have required rights.$$, remote_dsn),
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

        IF remote_nodeinfo.version_num < pgactive_min_remote_version_num() THEN
            RAISE USING
                MESSAGE = 'remote node''s pgactive version is too old',
                DETAIL = format($$The dsn '%s' connects successfully but the remote node version %s is less than the required version %s.$$,
                    remote_dsn, remote_nodeinfo.version_num, pgactive_min_remote_version_num()),
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

        IF remote_nodeinfo.min_remote_version_num > pgactive_version_num() THEN
            RAISE USING
                MESSAGE = 'remote node''s pgactive version is too new or this node''s version is too old',
                DETAIL = format($$The dsn '%s' connects successfully but the remote node version %s requires this node to run at least pgactive %s, not the current %s.$$,
                    remote_dsn, remote_nodeinfo.version_num, remote_nodeinfo.min_remote_version_num,
                    pgactive_min_remote_version_num()),
                ERRCODE = 'object_not_in_prerequisite_state';

        END IF;

        IF remote_nodeinfo.node_status IS NULL THEN
            RAISE USING
                MESSAGE = 'remote node does not appear to be a fully running pgactive node',
                DETAIL = format($$The dsn '%s' connects successfully but the target node has no entry in pgactive.pgactive_nodes.$$, remote_dsn),
                ERRCODE = 'object_not_in_prerequisite_state';
        ELSIF remote_nodeinfo.node_status IS DISTINCT FROM pgactive.pgactive_node_status_to_char('pgactive_NODE_STATUS_READY') THEN
            RAISE USING
                MESSAGE = 'remote node does not appear to be a fully running pgactive node',
                DETAIL = format($$The dsn '%s' connects successfully but the target node has pgactive.pgactive_nodes node_status=%s instead of expected 'r'.$$, remote_dsn, remote_nodeinfo.node_status),
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

        SELECT setting::integer INTO local_max_node_value FROM pg_settings
          WHERE name = 'pgactive.max_nodes';

        IF local_max_node_value <> remote_nodeinfo.max_nodes THEN
            RAISE USING
                MESSAGE = 'joining node and pgactive group have different values for pgactive.max_nodes parameter',
                DETAIL = format('pgactive.max_nodes value for joining node is ''%s'' and remote node is ''%s''.',
                                local_max_node_value, remote_nodeinfo.max_nodes),
                HINT = 'The parameter must be set to the same value on all pgactive members.',
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

        SELECT setting FROM pg_settings
          WHERE name = 'data_directory' INTO data_dir;

        SELECT pgactive._pgactive_get_free_disk_space(data_dir) INTO free_disk_space1;
        SELECT pg_size_pretty(free_disk_space1) INTO free_disk_space1_p;
        SELECT pg_size_pretty(remote_nodeinfo.dbsize) INTO remote_dbsize_p;

        -- We estimate that postgres needs 20% more disk space as temporary
        -- workspace while restoring database for running queries or building
        -- indexes. Note that it is just an estimation, the actual disk space
        -- needed depends on various factors. Hence we emit a warning to inform
        -- early, not an error.
        IF free_disk_space1 < (1.2 * remote_nodeinfo.dbsize) THEN
          RAISE WARNING USING
            MESSAGE = 'node might fail to join pgactive group as disk space is likely to be insufficient',
            DETAIL = format('joining node data directory file system mount point has %s free disk space and remote database is %s in size.',
                            free_disk_space1_p, remote_dbsize_p),
            HINT = 'Ensure enough free space on joining node file system.',
            ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

        SELECT setting FROM pg_settings
          WHERE name = 'pgactive.temp_dump_directory' INTO temp_dump_dir;

        SELECT pgactive._pgactive_get_free_disk_space(temp_dump_dir) INTO free_disk_space2;
        SELECT pg_size_pretty(free_disk_space2) INTO free_disk_space2_p;

        -- We estimate that pg_dump needs at least 50% of database size
        -- excluding total size of indexes on the database. Note that it is
        -- just an estimation, the actual disk space needed depends on various
        -- factors. Hence we emit a warning to inform early, not an error.
        IF free_disk_space2 < ((remote_nodeinfo.dbsize - remote_nodeinfo.indexessize)/2) THEN
          RAISE WARNING USING
            MESSAGE = 'node might fail to join pgactive group as disk space required to store temporary dump is likely to be insufficient',
            DETAIL = format('pgactive.temp_dump_directory file system mount point has %s free disk space and remote database is %s in size.',
                            free_disk_space2_p, remote_dbsize_p),
            HINT = 'Ensure enough free space on pgactive.temp_dump_directory file system.',
            ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

        SELECT pgactive._pgactive_check_file_system_mount_points(data_dir, temp_dump_dir)
          INTO same_file_system_mount_point;

        IF same_file_system_mount_point THEN
          IF free_disk_space1 <
             ((1.2 * remote_nodeinfo.dbsize) + ((remote_nodeinfo.dbsize - remote_nodeinfo.indexessize)/2)) THEN
            RAISE WARNING USING
              MESSAGE = 'node might fail to join pgactive group as disk space required to store both remote database and temporary dump is likely to be insufficient',
              HINT = 'Ensure enough free space on joining node file system.',
              ERRCODE = 'object_not_in_prerequisite_state';
          END IF;
        END IF;

		-- using pg_file_settings here as pgactive.skip_ddl_replication is SET to on when entering
		-- the function.
		SELECT COALESCE((SELECT setting::boolean
						 FROM pg_file_settings
						 WHERE name = 'pgactive.skip_ddl_replication' ORDER BY seqno DESC LIMIT 1),
						 true) INTO local_skip_ddl_replication_value;

		IF local_skip_ddl_replication_value <> remote_nodeinfo.skip_ddl_replication THEN
			RAISE USING
				MESSAGE = 'joining node and pgactive group have different values for pgactive.skip_ddl_replication parameter',
				DETAIL = format('pgactive.skip_ddl_replication value for joining node is ''%s'' and remote node is ''%s''.',
								local_skip_ddl_replication_value, remote_nodeinfo.skip_ddl_replication),
				HINT = 'The parameter must be set to the same value on all pgactive members.',
				ERRCODE = 'object_not_in_prerequisite_state';
		END IF;

        IF local_max_node_value = remote_nodeinfo.cur_nodes THEN
            RAISE USING
                MESSAGE = 'cannot allow more than pgactive.max_nodes number of nodes in a pgactive group',
                HINT = 'Increase pgactive.max_nodes parameter value on joining node as well as on all other pgactive members.',
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

        SELECT datcollate, datctype FROM pg_database
          WHERE datname = current_database() INTO local_db_collation_info_r;

        IF local_db_collation_info_r.datcollate <> remote_nodeinfo.datcollate OR
           local_db_collation_info_r.datctype <> remote_nodeinfo.datctype THEN

          collation_errmsg := 'joining node and remote node have different database collation settings';
          collation_hintmsg := 'Use the same database collation settings for both nodes.';

          IF bypass_collation_check THEN
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


    IF data_only_node_init THEN
        SELECT oid FROM pg_database
          WHERE datname = current_database() INTO current_dboid;
        -- The per-db worker will reset data_only_node_init to false after the
        -- pgactive_init_replica.
        PERFORM _pgactive_set_data_only_node_init(current_dboid, true);
    END IF;

    -- Create local node record so the apply worker knows to start initializing
    -- this node with pgactive_init_replica when it's started.
    --
    -- pgactive_init_copy might've created a node entry in catchup mode already, in
    -- which case we can skip this.
    SELECT * FROM pgactive_nodes
    WHERE node_sysid = localid.sysid
      AND node_timeline = localid.timeline
      AND node_dboid = localid.dboid
    INTO cur_node;

    IF NOT FOUND THEN
        INSERT INTO pgactive_nodes (
            node_name,
            node_sysid, node_timeline, node_dboid,
            node_status, node_dsn, node_init_from_dsn
        ) VALUES (
            node_name,
            localid.sysid, localid.timeline, localid.dboid,
            pgactive.pgactive_node_status_to_char('pgactive_NODE_STATUS_BEGINNING_INIT'),
            node_dsn, remote_dsn
        );
    ELSIF pgactive.pgactive_node_status_from_char(cur_node.node_status) = 'pgactive_NODE_STATUS_CATCHUP' THEN
        RAISE DEBUG 'starting node join in pgactive_NODE_STATUS_CATCHUP';
    ELSE
        RAISE USING
            MESSAGE = 'a pgactive_nodes entry for this node already exists',
            DETAIL = format('pgactive.pgactive_nodes entry for (%s,%s,%s) named ''%s'' with status %s exists.',
                            cur_node.node_sysid, cur_node.node_timeline, cur_node.node_dboid,
                            cur_node.node_name, pgactive.pgactive_node_status_from_char(cur_node.node_status)),
            ERRCODE = 'object_not_in_prerequisite_state';
    END IF;

    PERFORM pgactive._pgactive_update_seclabel_private();
END;
$body$;

REVOKE ALL ON FUNCTION _pgactive_begin_join_private(text, text, text, text, boolean, boolean, boolean, boolean) FROM public;

CREATE FUNCTION _pgactive_set_data_only_node_init(dboid oid, val boolean)
RETURNS VOID
AS 'MODULE_PATHNAME'
LANGUAGE C;

REVOKE ALL ON FUNCTION _pgactive_set_data_only_node_init(oid, boolean) FROM public;

CREATE FUNCTION pgactive_get_replication_set_tables(r_sets text[])
RETURNS SETOF text
VOLATILE
STRICT
LANGUAGE 'sql'
AS $$
  SELECT DISTINCT objname
    FROM pg_seclabels
    WHERE provider = 'pgactive'
    AND objtype = 'table'
    AND EXISTS (
    SELECT 1
    FROM json_array_elements_text(label::json->'sets') AS elem
    WHERE elem::text = ANY (r_sets)
  );
$$;

REVOKE ALL ON FUNCTION pgactive_get_replication_set_tables(text[]) FROM public;

-- Completely de-pgactive-ize a node. Updated to fix #281.
CREATE OR REPLACE FUNCTION pgactive_remove (
  force boolean DEFAULT false)
RETURNS void
LANGUAGE plpgsql
-- SET pgactive.skip_ddl_locking = on is removed for now
-- SET pgactive.permit_unsafe_ddl_commands = on is removed for now
SET pgactive.skip_ddl_replication = on
SET search_path = 'pgactive,pg_catalog'
AS $$
DECLARE
  local_node_status "char";
  _seqschema name;
  _seqname name;
  _seqmax bigint;
  _tableoid oid;
  _truncate_tg record;
  current_dboid oid;
BEGIN

  SELECT node_status FROM pgactive.pgactive_nodes WHERE (node_sysid, node_timeline, node_dboid) = pgactive.pgactive_get_local_nodeid()
  INTO local_node_status;

  IF NOT (local_node_status = 'k' OR local_node_status IS NULL) THEN
    IF force THEN
      RAISE WARNING 'forcing deletion of possibly active pgactive node';

      UPDATE pgactive.pgactive_nodes
      SET node_status = 'k'
      WHERE (node_sysid, node_timeline, node_dboid) = pgactive.pgactive_get_local_nodeid();

      PERFORM pgactive._pgactive_pause_worker_management_private(false);

      PERFORM pg_sleep(5);

      RAISE NOTICE 'node forced to detached state, now removing';
    ELSE
      RAISE EXCEPTION 'this pgactive node might still be active, not removing';
    END IF;
  END IF;

  RAISE NOTICE 'removing pgactive from node';

   -- Strip the database security label
  EXECUTE format('SECURITY LABEL FOR pgactive ON DATABASE %I IS NULL', current_database());

  -- Suspend worker management, so when we terminate apply workers and
  -- walsenders they won't get relaunched.
  PERFORM pgactive._pgactive_pause_worker_management_private(true);

  -- Terminate WAL sender(s) associated with this database.
  PERFORM pgactive.pgactive_terminate_workers(node_sysid, node_timeline, node_dboid, 'walsender')
  FROM pgactive.pgactive_nodes
  WHERE (node_sysid, node_timeline, node_dboid) <> pgactive.pgactive_get_local_nodeid();

  -- Terminate apply worker(s) associated with this database.
  PERFORM pgactive.pgactive_terminate_workers(node_sysid, node_timeline, node_dboid, 'apply')
  FROM pgactive.pgactive_nodes
  WHERE (node_sysid, node_timeline, node_dboid) <> pgactive.pgactive_get_local_nodeid();

  -- Delete all connections and all nodes except the current one
  DELETE FROM pgactive.pgactive_connections
  WHERE (conn_sysid, conn_timeline, conn_dboid) <> pgactive.pgactive_get_local_nodeid();

  DELETE FROM pgactive.pgactive_nodes
  WHERE (node_sysid, node_timeline, node_dboid) <> pgactive.pgactive_get_local_nodeid();

  -- Let the perdb worker resume work and figure out everything's
  -- going away.
  PERFORM pgactive._pgactive_pause_worker_management_private(false);
  PERFORM pgactive.pgactive_connections_changed();

  -- Give it a few seconds
  PERFORM pg_sleep(2);

  -- Terminate per-db worker associated with this database.
  SELECT oid FROM pg_database
    WHERE datname = current_database() INTO current_dboid;
  PERFORM pgactive.pgactive_terminate_perdb_worker(current_dboid);

  -- Poke supervisor to clear the per-db worker's shared memory slot.
  PERFORM pgactive.pgactive_connections_changed();

  -- Clear out the rest of pgactive_nodes and pgactive_connections
  DELETE FROM pgactive.pgactive_nodes;
  DELETE FROM pgactive.pgactive_connections;

  -- Drop peer replication slots for this DB
  PERFORM pg_drop_replication_slot(slot_name)
  FROM pg_catalog.pg_replication_slots,
       pgactive.pgactive_parse_slot_name(slot_name) ps
  WHERE ps.local_dboid = (select oid from pg_database where datname = current_database())
       AND plugin = 'pgactive';

  -- and replication origins
  PERFORM pg_replication_origin_drop(roname)
  FROM pg_catalog.pg_replication_origin,
       pgactive.pgactive_parse_replident_name(roname) pi
  WHERE pi.local_dboid = (select oid from pg_database where datname = current_database());

  -- Strip the security labels we use for replication sets from all the tables
  FOR _tableoid IN
    SELECT objoid
    FROM pg_catalog.pg_seclabel
    INNER JOIN pg_catalog.pg_class ON (pg_seclabel.objoid = pg_class.oid)
    WHERE provider = 'pgactive'
      AND classoid = 'pg_catalog.pg_class'::regclass
      AND pg_class.relkind = 'r'
  LOOP
    -- regclass's text out adds quoting and schema qualification if needed
    EXECUTE format('SECURITY LABEL FOR pgactive ON TABLE %s IS NULL', _tableoid::regclass);
  END LOOP;

  -- Drop the on-truncate triggers. They'd otherwise get cascade-dropped when
  -- the pgactive extension was dropped, but this way the system is clean. We can't
  -- drop ones under the 'pgactive' schema.
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
      AND d2.refobjid = 'pgactive.pgactive_queue_truncate'::regproc
      AND n.nspname <> 'pgactive'
  LOOP
    EXECUTE format('DROP TRIGGER %I ON %I.%I',
         _truncate_tg.tgname, _truncate_tg.tgrelnsp, _truncate_tg.tgrelname);

    -- The trigger' dependency entry will be dangling because of how we dropped
    -- it.
    DELETE FROM pg_depend
    WHERE classid = 'pg_trigger'::regclass AND
      (objid = _truncate_tg.tgobjid
       AND (refclassid = 'pg_proc'::regclass AND refobjid = 'pgactive.pgactive_queue_truncate'::regproc)
          OR
          (refclassid = 'pg_class'::regclass AND refobjid = _truncate_tg.tgrelid)
	  );

  END LOOP;

  -- Delete the other detritus from the extension. The user should really drop it,
  -- but we should try to restore a clean state anyway.
  DELETE FROM pgactive.pgactive_queued_commands;
  DELETE FROM pgactive.pgactive_queued_drops;
  DELETE FROM pgactive.pgactive_global_locks;
  DELETE FROM pgactive.pgactive_conflict_handlers;
  DELETE FROM pgactive.pgactive_conflict_history;
  DELETE FROM pgactive.pgactive_replication_set_config;

  PERFORM pgactive._pgactive_destroy_temporary_dump_directories_private();

  -- We can't drop the pgactive extension, we just need to tell the user to do that.
  RAISE NOTICE 'pgactive removed from this node. You can now DROP EXTENSION pgactive and, if this is the last pgactive node on this PostgreSQL instance, remove pgactive from shared_preload_libraries.';
END;
$$;

REVOKE ALL ON FUNCTION pgactive_remove(boolean) FROM public;

COMMENT ON FUNCTION pgactive_remove(boolean) IS
'Remove all pgactive security labels, slots, replication origins, replication sets, etc from the local node.';

CREATE FUNCTION pgactive_terminate_perdb_worker(dboid oid)
RETURNS VOID
AS 'MODULE_PATHNAME'
LANGUAGE C VOLATILE STRICT;

REVOKE ALL ON FUNCTION pgactive_terminate_perdb_worker(oid) FROM public;

DROP FUNCTION pgactive_wait_for_node_ready(integer, integer);

CREATE FUNCTION pgactive_wait_for_node_ready(
  timeout integer DEFAULT 0,
  progress_interval integer DEFAULT 60)
RETURNS void LANGUAGE plpgsql VOLATILE
AS $body$
DECLARE
  local_node record;
  remote_node record;
  t_lp_cnt integer := 0;
  p_lp_cnt integer := 0;
  w_lp_cnt integer;
  l_db_init_sz int8;
  l_db_sz int8;
  r_db text;
  p_pct integer;
  sleep_sec integer;
  worker_timeout integer;
BEGIN

    IF timeout < 0 THEN
      RAISE EXCEPTION '''timeout'' parameter must not be 0';
    END IF;

    IF progress_interval <= 0 THEN
      RAISE EXCEPTION '''progress_interval'' parameter must be > 0';
    END IF;
    w_lp_cnt := 0;
    sleep_sec := 5;
    worker_timeout := 120;
    LOOP
      PERFORM pg_sleep( sleep_sec );
      PERFORM PID from pg_stat_activity where application_name = 'pgactive:supervisor';
      IF FOUND THEN
        EXIT;
      END IF;
      IF w_lp_cnt > worker_timeout THEN
        RAISE EXCEPTION 'pgactive supervisor is not running';
      ELSE
        RAISE NOTICE 'waiting for pgactive supervisor to start %/%', w_lp_cnt, worker_timeout;
      END IF;
      w_lp_cnt := w_lp_cnt + sleep_sec;
    END LOOP;

    IF current_setting('transaction_isolation') <> 'read committed' THEN
        RAISE EXCEPTION 'can only wait for node join in an ISOLATION LEVEL READ COMMITTED transaction, not %',
                        current_setting('transaction_isolation');
    END IF;

    SELECT * FROM pgactive.pgactive_nodes
      WHERE (node_sysid, node_timeline, node_dboid) = pgactive.pgactive_get_local_nodeid()
      INTO local_node;

    IF local_node.node_init_from_dsn is NULL THEN
      RAISE NOTICE 'checking status of pgactive.pgactive_create_group';
    ELSE
      RAISE NOTICE 'checking status of pgactive.pgactive_join_group';
      SELECT * FROM pgactive._pgactive_get_node_info_private(local_node.node_init_from_dsn)
        INTO remote_node;
      SELECT pg_size_pretty(remote_node.dbsize) INTO r_db;
      SELECT pg_database_size(local_node.node_dboid) INTO l_db_init_sz;
    END IF;
    w_lp_cnt := 0;
    sleep_sec := 10;
    worker_timeout := 300;
    LOOP
      SELECT * FROM pgactive.pgactive_nodes
      WHERE (node_sysid, node_timeline, node_dboid)
        = pgactive.pgactive_get_local_nodeid()
      INTO local_node;

      IF local_node.node_status = 'r' THEN
        IF remote_node IS NOT NULL THEN
          RAISE NOTICE
              USING MESSAGE = format('successfully joined the node and restored database ''%s'' from node %s',
                                     remote_node.dbname, remote_node.node_name);
        ELSE
          RAISE NOTICE 'successfully created first node in pgactive group';
        END IF;
        EXIT;
      END IF;

      IF timeout > 0 THEN
        t_lp_cnt := t_lp_cnt + sleep_sec;
        IF t_lp_cnt > timeout THEN
          RAISE EXCEPTION 'node % cannot reach ready state within % seconds, current state is %',
                          local_node.node_name, timeout, local_node.node_status;
        END IF;
      END IF;

      PERFORM pg_sleep( sleep_sec );
      w_lp_cnt := w_lp_cnt + sleep_sec;
      IF w_lp_cnt > worker_timeout THEN
        w_lp_cnt := 0;
        PERFORM PID FROM pg_stat_activity where application_name = 'pgactive:'|| local_node.node_sysid ||':perdb';
        IF NOT FOUND THEN
          RAISE EXCEPTION 'could not detect a running pgactive perdb worker, current node state is %',  local_node.node_status
          USING DETAIL = format( 'Either pgactive perdb worker exited due to an error or it did not start in %s seconds.', worker_timeout),
          HINT = 'Please check PostgreSQL log file for more details.';
        END IF;
      END IF;

      IF progress_interval > 0 AND local_node.node_init_from_dsn IS NOT NULL THEN
        p_lp_cnt := p_lp_cnt + sleep_sec;

        IF p_lp_cnt > progress_interval THEN
          SELECT pg_database_size(local_node.node_dboid) INTO l_db_sz;
          IF l_db_sz = 0 OR l_db_sz = l_db_init_sz THEN
            RAISE NOTICE
                USING MESSAGE = format('transferring of database ''%s'' (%s) from node %s in progress',
                                       remote_node.dbname, r_db, remote_node.node_name);
          ELSE
            SELECT (l_db_sz/remote_node.dbsize) * 100 INTO p_pct;
            RAISE NOTICE
              USING MESSAGE = format('restoring database ''%s'', %s%% of %s complete',
                                     remote_node.dbname, p_pct, r_db);
          END IF;
          p_lp_cnt := 0;
        END IF;
      END IF;
    END LOOP;
END;
$body$;

REVOKE ALL ON FUNCTION pgactive_wait_for_node_ready(integer, integer) FROM public;

-- RESET pgactive.permit_unsafe_ddl_commands; is removed for now
RESET pgactive.skip_ddl_replication;
RESET search_path;
