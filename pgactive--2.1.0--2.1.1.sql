-- Everything should assume the 'pgactive' prefix
SET LOCAL search_path = pgactive;

DROP FUNCTION get_last_applied_xact_info(text, oid, oid);

CREATE FUNCTION pgactive_get_last_applied_xact_info(
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

COMMENT ON FUNCTION pgactive_get_last_applied_xact_info(text, oid, oid) IS
'Gets last applied transaction info of apply worker for a given node.';

DROP VIEW pgactive.pgactive_node_slots;

DROP FUNCTION get_replication_lag_info();

CREATE FUNCTION pgactive_get_replication_lag_info(
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

COMMENT ON FUNCTION pgactive_get_replication_lag_info() IS
'Gets replication lag info.';

DROP FUNCTION get_free_disk_space(text);
CREATE FUNCTION _pgactive_get_free_disk_space(
  path text,
  OUT free_disk_space int8
)
RETURNS bigint
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

REVOKE ALL ON FUNCTION _pgactive_get_free_disk_space(text) FROM public;

COMMENT ON FUNCTION _pgactive_get_free_disk_space(text) IS
'Gets free disk space in bytes of filesystem to which given path is mounted.';

DROP FUNCTION check_file_system_mount_points(text, text);

CREATE FUNCTION _pgactive_check_file_system_mount_points(
  path1 text,
  path2 text
)
RETURNS boolean
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

REVOKE ALL ON FUNCTION _pgactive_check_file_system_mount_points(text, text) FROM public;

COMMENT ON FUNCTION _pgactive_check_file_system_mount_points(text, text) IS
'Checks if given paths are on same file system mount points.';

DROP FUNCTION has_required_privs();

CREATE FUNCTION _pgactive_has_required_privs()
RETURNS boolean
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

REVOKE ALL ON FUNCTION _pgactive_has_required_privs() FROM public;

COMMENT ON FUNCTION _pgactive_has_required_privs() IS
'Checks if current user has required privileges.';


CREATE VIEW pgactive.pgactive_node_slots AS
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
 CROSS JOIN LATERAL pgactive.pgactive_parse_slot_name(s.slot_name) ps(remote_sysid, remote_timeline, remote_dboid, local_dboid, replication_name)
 INNER JOIN pgactive.pgactive_nodes n ON ((n.node_sysid = ps.remote_sysid) AND (n.node_timeline = ps.remote_timeline) AND (n.node_dboid = ps.remote_dboid))
 INNER JOIN pgactive.pgactive_get_replication_lag_info() l ON (l.slot_name = s.slot_name)
 LEFT JOIN pg_catalog.pg_stat_replication r ON (r.pid = s.active_pid)
WHERE ps.local_dboid = (select oid from pg_database where datname = current_database())
  AND s.plugin = 'pgactive';

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
    bypass_user_tables_check boolean
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

REVOKE ALL ON FUNCTION _pgactive_begin_join_private(text, text, text, text, boolean, boolean, boolean) FROM public;
REVOKE ALL ON FUNCTION pgactive_variant() FROM public;
REVOKE ALL ON FUNCTION pgactive_get_stats() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgactive_truncate_trigger_add() FROM public;
REVOKE ALL ON FUNCTION pgactive_internal_create_truncate_trigger(regclass) FROM public;
REVOKE ALL ON FUNCTION pgactive_queue_truncate() FROM public;
REVOKE ALL ON FUNCTION pgactive_apply_pause() FROM public;
REVOKE ALL ON FUNCTION pgactive_apply_resume() FROM public;
REVOKE ALL ON FUNCTION pgactive_get_local_nodeid() FROM public;
REVOKE ALL ON FUNCTION pgactive_version_num() FROM public;
REVOKE ALL ON FUNCTION pgactive_min_remote_version_num() FROM public;
REVOKE ALL ON FUNCTION _pgactive_join_node_private(text, oid, oid, text, integer, text[]) FROM public;
REVOKE ALL ON FUNCTION _pgactive_update_seclabel_private() FROM public;
REVOKE ALL ON FUNCTION pgactive_join_group(text, text, text, integer, text[], boolean, boolean, boolean) FROM public;
REVOKE ALL ON FUNCTION pgactive_create_group(text, text, integer, text[]) FROM public;
REVOKE ALL ON FUNCTION pgactive_detach_nodes(text[]) FROM public;
REVOKE ALL ON FUNCTION pgactive_wait_for_node_ready(integer, integer) FROM public;
REVOKE ALL ON FUNCTION pgactive_parse_slot_name(name) FROM public;
REVOKE ALL ON FUNCTION pgactive_format_slot_name(text, oid, oid, oid, name) FROM public;
REVOKE ALL ON FUNCTION pgactive_set_node_read_only(text, boolean) FROM public;
REVOKE ALL ON FUNCTION pgactive_terminate_workers(text, oid, oid, text) FROM public;
REVOKE ALL ON FUNCTION pgactive_skip_changes(text, oid, oid, pg_lsn) FROM public;
REVOKE ALL ON FUNCTION pgactive_get_connection_replication_sets(text) FROM public;
REVOKE ALL ON FUNCTION pgactive_set_connection_replication_sets(text[], text) FROM public;
REVOKE ALL ON FUNCTION _pgactive_pause_worker_management_private(boolean) FROM public;
REVOKE ALL ON FUNCTION pgactive_parse_replident_name(text) FROM public;
REVOKE ALL ON FUNCTION pgactive_format_replident_name(text, oid, oid, oid, name) FROM public;
REVOKE ALL ON FUNCTION pgactive_node_status_from_char("char") FROM public;
REVOKE ALL ON FUNCTION pgactive_node_status_to_char(text) FROM public;
REVOKE ALL ON FUNCTION _pgactive_snowflake_id_nextval_private(regclass, bigint) FROM public;
REVOKE ALL ON FUNCTION pgactive_assign_seq_ids_post_upgrade() FROM public;
REVOKE ALL ON FUNCTION pgactive_wait_for_slots_confirmed_flush_lsn(name,pg_lsn) FROM public;
REVOKE ALL ON FUNCTION pgactive_handle_rejoin() FROM public;
REVOKE ALL ON FUNCTION pgactive_get_node_identifier() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgactive_fdw_validator(text[], oid) FROM PUBLIC;
REVOKE ALL ON FUNCTION pgactive_conninfo_cmp(text, text) FROM PUBLIC;
