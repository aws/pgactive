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

REVOKE ALL ON FUNCTION pgactive_set_connection_replication_sets(text[], text, oid, oid, text, oid, oid) FROM public;

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

CREATE OR REPLACE FUNCTION pgactive.pgactive_join_group (
    node_name text,
    node_dsn text,
    join_using_dsn text,
    apply_delay integer DEFAULT NULL,
    replication_sets text[] DEFAULT ARRAY['default'],
    bypass_collation_check boolean DEFAULT false,
    bypass_node_identifier_creation boolean DEFAULT false,
    bypass_user_tables_check boolean DEFAULT false
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

    PERFORM pgactive._pgactive_begin_join_private(
        caller := '',
        node_name := node_name,
        node_dsn := node_dsn,
        remote_dsn := join_using_dsn,
        bypass_collation_check := bypass_collation_check,
        bypass_node_identifier_creation := bypass_node_identifier_creation,
        bypass_user_tables_check := bypass_user_tables_check);

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

COMMENT ON FUNCTION pgactive.pgactive_join_group(text, text, text, integer, text[], boolean, boolean, boolean) IS
'Join an existing pgactive group by connecting to a member node and copying its contents';

REVOKE ALL ON FUNCTION pgactive.pgactive_join_group(text, text, text, integer, text[], boolean, boolean, boolean) FROM public;

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

-- RESET pgactive.permit_unsafe_ddl_commands; is removed for now
RESET pgactive.skip_ddl_replication;
RESET search_path;
