/* pgactive--2.1.3--2.1.4.sql */

-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION pgactive UPDATE TO '2.1.4'" to load this file. \quit

SET pgactive.skip_ddl_replication = true;
-- Everything should assume the 'pgactive' prefix
SET LOCAL search_path = pgactive;

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
        -- pgactive_init_replica option.
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

--
-- The public interface for node join/addition, to be run to join a currently
-- unconnected node with a blank database to a pgactive group.
--
DROP FUNCTION pgactive_join_group(text, text, text, integer, text[], boolean, boolean, boolean);
CREATE FUNCTION pgactive_join_group (
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
BEGIN

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

    -- Now ensure the per-db worker is started if it's not already running.
    -- This won't actually take effect until commit time, it just adds a commit
    -- hook to start the worker when we commit.
    PERFORM pgactive.pgactive_connections_changed();
END;
$body$;

COMMENT ON FUNCTION pgactive_join_group(text, text, text, integer, text[], boolean, boolean, boolean, boolean) IS
'Join an existing pgactive group by connecting to a member node and copying its contents';

DROP FUNCTION pgactive_create_group(text, text, integer, text[]);
CREATE FUNCTION pgactive_create_group (
    node_name text,
    node_dsn text,
    apply_delay integer DEFAULT NULL,
    replication_sets text[] DEFAULT ARRAY['default']
    )
RETURNS void LANGUAGE plpgsql VOLATILE
SET search_path = pgactive, pg_catalog
-- SET pgactive.permit_unsafe_ddl_commands = on is removed for now
SET pgactive.skip_ddl_replication = on
-- SET pgactive.skip_ddl_locking = on is removed for now
AS $body$
DECLARE
	t record;
BEGIN

    -- Prohibit enabling pgactive where exclusion constraints exist
    FOR t IN
        SELECT n.nspname, r.relname, c.conname, c.contype
        FROM pg_constraint c
          INNER JOIN pg_namespace n ON c.connamespace = n.oid
          INNER JOIN pg_class r ON c.conrelid = r.oid
          INNER JOIN LATERAL unnest(pgactive.pgactive_get_table_replication_sets(c.conrelid)) rs(rsname) ON (rs.rsname = ANY(replication_sets))
        WHERE c.contype = 'x'
          AND r.relpersistence = 'p'
          AND r.relkind = 'r'
          AND n.nspname NOT IN ('pg_catalog', 'pgactive', 'information_schema')
    LOOP
        RAISE USING
            MESSAGE = 'pgactive can''t be enabled because exclusion constraints exist on persistent tables that are not excluded from replication',
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
          INNER JOIN LATERAL unnest(pgactive.pgactive_get_table_replication_sets(c.conrelid)) rs(rsname) ON (rs.rsname = ANY(replication_sets))
        WHERE c.contype = 'u'
          AND r.relpersistence = 'p'
          AND r.relkind = 'r'
          AND n.nspname NOT IN ('pg_catalog', 'pgactive', 'information_schema')
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
        WHERE n.nspname NOT IN ('pg_catalog', 'pgactive', 'information_schema')
          AND relkind = 'r'
          AND relpersistence = 'p'
          AND c.oid IS NULL
    LOOP
        RAISE WARNING USING
            MESSAGE = format('table %I.%I has no PRIMARY KEY', t.nspname, t.relname),
            HINT = 'Tables without a PRIMARY KEY cannot be UPDATEd or DELETEd from, only INSERTed into. Add a PRIMARY KEY.';
    END LOOP;

    -- Create ON TRUNCATE triggers for pgactive on existing tables
    -- See pgactive_truncate_trigger_add for the matching event trigger for tables
    -- created after join.
    --
    -- The triggers may be created already because the pgactive event trigger
    -- runs when the pgactive extension is created, even if there's no active
    -- pgactive connections yet, so tables created after the extension is created
    -- will get the trigger already. So skip tables that have a tg named
    -- 'truncate_trigger' calling proc 'pgactive.pgactive_queue_truncate'.
    FOR t IN
        SELECT r.oid AS relid
        FROM pg_class r
          INNER JOIN pg_namespace n ON (r.relnamespace = n.oid)
          LEFT JOIN pg_trigger tg ON (r.oid = tg.tgrelid AND tgname = 'truncate_trigger')
          LEFT JOIN pg_proc p ON (p.oid = tg.tgfoid AND p.proname = 'pgactive_queue_truncate')
          LEFT JOIN pg_namespace pn ON (pn.oid = p.pronamespace AND pn.nspname = 'pgactive')
        WHERE r.relpersistence = 'p'
          AND r.relkind = 'r'
          AND n.nspname NOT IN ('pg_catalog', 'pgactive', 'information_schema')
          AND tg.oid IS NULL AND p.oid IS NULL and pn.oid IS NULL
    LOOP
        -- We use a C function here because in addition to trigger creation
        -- we must also mark it tgisinternal.
        PERFORM pgactive.pgactive_internal_create_truncate_trigger(t.relid);
    END LOOP;

    PERFORM pgactive.pgactive_join_group(
        node_name := node_name,
        node_dsn := node_dsn,
        join_using_dsn := null,
        apply_delay := apply_delay,
        replication_sets := replication_sets,
        bypass_user_tables_check := true,
        data_only_node_init := false);
END;
$body$;

COMMENT ON FUNCTION pgactive_create_group(text, text, integer, text[]) IS
'Create a pgactive group, turning a stand-alone database into the first node in a pgactive group';

CREATE FUNCTION pgactive_get_last_error_info()
RETURNS TEXT
AS 'MODULE_PATHNAME'
LANGUAGE C;

DROP FUNCTION pgactive_wait_for_node_ready(integer, integer);
CREATE FUNCTION pgactive_wait_for_node_ready(
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
  last_error_info text;
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
      SELECT * FROM pgactive.pgactive_nodes
      WHERE (node_sysid, node_timeline, node_dboid)
        = pgactive.pgactive_get_local_nodeid()
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

      SELECT * FROM pgactive.pgactive_get_last_error_info() INTO last_error_info;
      IF last_error_info IS NOT NULL THEN
        RAISE EXCEPTION USING
            MESSAGE = format('previous init of node %s failed with error message: %s',
                             r1.node_name, last_error_info),
            HINT = format('Check server logs for more details.'),
            ERRCODE = 'object_not_in_prerequisite_state';
      END IF;

      IF progress_interval > 0 AND r1.node_init_from_dsn IS NOT NULL THEN
        p_lp_cnt := p_lp_cnt + 1;

        IF first_time THEN
          SELECT * FROM pgactive._pgactive_get_node_info_private(r1.node_init_from_dsn)
            INTO r2;
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

CREATE FUNCTION _pgactive_set_data_only_node_init(dboid oid, val boolean)
RETURNS VOID
AS 'MODULE_PATHNAME'
LANGUAGE C;

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

REVOKE ALL ON FUNCTION _pgactive_begin_join_private(text, text, text, text, boolean, boolean, boolean, boolean) FROM public;
REVOKE ALL ON FUNCTION pgactive_join_group(text, text, text, integer, text[], boolean, boolean, boolean, boolean) FROM public;
REVOKE ALL ON FUNCTION pgactive_create_group(text, text, integer, text[]) FROM public;
REVOKE ALL ON FUNCTION pgactive_get_last_error_info() FROM public;
REVOKE ALL ON FUNCTION pgactive_wait_for_node_ready(integer, integer) FROM public;
REVOKE ALL ON FUNCTION _pgactive_set_data_only_node_init(oid, boolean) FROM public;
REVOKE ALL ON FUNCTION pgactive_get_replication_set_tables(text[]) FROM public;

RESET pgactive.skip_ddl_replication;
RESET search_path;
