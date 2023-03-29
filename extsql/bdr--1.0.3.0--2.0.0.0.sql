--\echo Use "CREATE EXTENSION bdr" to load this file. \quit
--
-- This extension script adds compatibility for 9.6 DDL replication
--
-- Columns are added to some tables in a prior script,
-- bdr--1.0.2.0--1.0.3.0.sql, since we need them to be present
-- on both 1.0 and 2.0 for upgrades to work.

SET LOCAL bdr.permit_unsafe_ddl_commands = true;
SET LOCAL bdr.skip_ddl_replication = true;
SET LOCAL search_path = bdr;

-- Marking this immutable is technically a bit cheeky as we could add
-- new statuses. But for index use we need it, and it's safe since
-- any unrecognised entries will result in ERRORs and can thus never
-- exist in an index.
CREATE FUNCTION bdr.node_status_from_char("char")
RETURNS text LANGUAGE c STRICT IMMUTABLE AS 'MODULE_PATHNAME','bdr_node_status_from_char';

CREATE FUNCTION bdr.node_status_to_char(text)
RETURNS "char" LANGUAGE c STRICT IMMUTABLE AS 'MODULE_PATHNAME','bdr_node_status_to_char';

--
-- DDL replication limitations in 9.6bdr mean that we can't directly
-- EXECUTE ddl in function bodies, and must use bdr.bdr_replicate_ddl_command
-- with a fully qualified relation name.
--
CREATE OR REPLACE FUNCTION bdr.table_set_replication_sets(p_relation regclass, p_sets text[])
  RETURNS void
  VOLATILE
  LANGUAGE 'plpgsql'
  SET bdr.permit_unsafe_ddl_commands = true
  SET search_path = ''
  AS $$
DECLARE
    v_label json;
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
    PERFORM bdr.bdr_replicate_ddl_command(format('SECURITY LABEL FOR bdr ON TABLE %s IS %L', p_relation, v_label)) ;
END;
$$;

-- BDR doesn't like partial unique indexes. We'd really like
-- an index like:
--
--   CREATE UNIQUE INDEX ON bdr.bdr_nodes(node_seq_id) WHERE (node_status IN (bdr.node_status_to_char('BDR_NODE_STATUS_READY')));
--
-- but the simple way we do updates to those catalogs doesn't support partial
-- or expression indexes. So no constraint enforces node ID uniqueness.

CREATE FUNCTION bdr.global_seq_nextval(regclass)
RETURNS bigint
LANGUAGE c STRICT VOLATILE AS 'MODULE_PATHNAME','global_seq_nextval_oid';

COMMENT ON FUNCTION bdr.global_seq_nextval(regclass)
IS 'generate sequence values unique to this node using a local sequence as a seed';

-- For testing purposes we sometimes want to be able to override the
-- timestamp etc.
CREATE FUNCTION bdr.global_seq_nextval_test(regclass, bigint)
RETURNS bigint
LANGUAGE c STRICT VOLATILE AS 'MODULE_PATHNAME','global_seq_nextval_oid';

COMMENT ON FUNCTION bdr.global_seq_nextval_test(regclass, bigint)
IS 'function for BDR testing only, do not use in application code';

-- Add "node_status" to remote_nodeinfo result
DROP FUNCTION bdr.bdr_get_remote_nodeinfo(
	dsn text, sysid OUT text, timeline OUT oid, dboid OUT oid,
	variant OUT text, version OUT text, version_num OUT integer,
	min_remote_version_num OUT integer, is_superuser OUT boolean);

CREATE FUNCTION bdr.bdr_get_remote_nodeinfo(
	dsn text, sysid OUT text, timeline OUT oid, dboid OUT oid,
	variant OUT text, version OUT text, version_num OUT integer,
	min_remote_version_num OUT integer, is_superuser OUT boolean,
	node_status OUT "char")
RETURNS record LANGUAGE c AS 'MODULE_PATHNAME';

-- Update join to check node status of remote during join
CREATE OR REPLACE FUNCTION bdr.internal_begin_join(
    caller text, local_node_name text, node_local_dsn text, remote_dsn text,
    remote_sysid OUT text, remote_timeline OUT oid, remote_dboid OUT oid
)
RETURNS record LANGUAGE plpgsql VOLATILE
SET search_path = bdr, pg_catalog
SET bdr.permit_unsafe_ddl_commands = on
SET bdr.skip_ddl_replication = on
SET bdr.skip_ddl_locking = on
AS $body$
DECLARE
    localid RECORD;
    localid_from_dsn RECORD;
    remote_nodeinfo RECORD;
    remote_nodeinfo_r RECORD;
	cur_node RECORD;
BEGIN
    -- Only one tx can be adding connections
    LOCK TABLE bdr.bdr_connections IN EXCLUSIVE MODE;
    LOCK TABLE bdr.bdr_nodes IN EXCLUSIVE MODE;
    LOCK TABLE pg_catalog.pg_shseclabel IN EXCLUSIVE MODE;

    SELECT sysid, timeline, dboid INTO localid
    FROM bdr.bdr_get_local_nodeid();

    RAISE LOG USING MESSAGE = format('node identity of node being created is (%s,%s,%s)', localid.sysid, localid.timeline, localid.dboid);

    -- If there's already an entry for ourselves in bdr.bdr_connections
    -- then we know this node is part of an active BDR group and cannot
    -- be joined to another group. Unidirectional connections are ignored.
    PERFORM 1 FROM bdr_connections
    WHERE conn_sysid = localid.sysid
      AND conn_timeline = localid.timeline
      AND conn_dboid = localid.dboid
      AND (conn_origin_sysid = '0'
           AND conn_origin_timeline = 0
           AND conn_origin_dboid = 0)
      AND conn_is_unidirectional = 'f';

    IF FOUND THEN
        RAISE USING
            MESSAGE = 'This node is already a member of a BDR group',
            HINT = 'Connect to the node you wish to add and run '||caller||' from it instead',
            ERRCODE = 'object_not_in_prerequisite_state';
    END IF;

    -- Validate that the local connection is usable and matches
    -- the node identity of the node we're running on.
    --
    -- For BDR this will NOT check the 'dsn' if 'node_local_dsn'
    -- gets supplied. We don't know if 'dsn' is even valid
    -- for loopback connections and can't assume it is. That'll
    -- get checked later by BDR specific code.
    --
    -- We'll get a null node name back at this point since we haven't
    -- inserted our nodes record (and it wouldn't have committed yet
    -- if we had).
    ---
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
            HINT = 'The node_local_dsn (or, for bdr, dsn if node_local_dsn is null) parameter must refer to the node you''re running this function from',
            ERRCODE = 'object_not_in_prerequisite_state';
    END IF;

    IF NOT localid_from_dsn.is_superuser THEN
        RAISE USING
            MESSAGE = 'local dsn does not have superuser rights',
            DETAIL = format($$The dsn '%s' connects successfully but does not grant superuser rights$$, node_local_dsn),
            ERRCODE = 'object_not_in_prerequisite_state';
    END IF;

    -- Now interrogate the remote node, if specified, and sanity
    -- check its connection too. The discovered node identity is
    -- returned if found.
    --
    -- This will error out if there are issues with the remote
    -- node.
    IF remote_dsn IS NOT NULL THEN
        SELECT * INTO remote_nodeinfo
        FROM bdr_get_remote_nodeinfo(remote_dsn);

        remote_sysid := remote_nodeinfo.sysid;
        remote_timeline := remote_nodeinfo.timeline;
        remote_dboid := remote_nodeinfo.dboid;

        IF NOT remote_nodeinfo.is_superuser THEN
            RAISE USING
                MESSAGE = 'connection to remote node does not have superuser rights',
                DETAIL = format($$The dsn '%s' connects successfully but does not grant superuser rights$$, remote_dsn),
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

        IF remote_nodeinfo.version_num < bdr_min_remote_version_num() THEN
            RAISE USING
                MESSAGE = 'remote node''s BDR version is too old',
                DETAIL = format($$The dsn '%s' connects successfully but the remote node version %s is less than the required version %s$$,
                    remote_dsn, remote_nodeinfo.version_num, bdr_min_remote_version_num()),
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

        IF remote_nodeinfo.min_remote_version_num > bdr_version_num() THEN
            RAISE USING
                MESSAGE = 'remote node''s BDR version is too new or this node''s version is too old',
                DETAIL = format($$The dsn '%s' connects successfully but the remote node version %s requires this node to run at least bdr %s, not the current %s$$,
                    remote_dsn, remote_nodeinfo.version_num, remote_nodeinfo.min_remote_version_num,
                    bdr_min_remote_version_num()),
                ERRCODE = 'object_not_in_prerequisite_state';

        END IF;

        IF remote_nodeinfo.node_status IS NULL THEN
            RAISE USING
                MESSAGE = 'remote node does not appear to be a fully running BDR node',
                DETAIL = format($$The dsn '%s' connects successfully but the target node has no entry in bdr.bdr_nodes$$, remote_dsn),
                ERRCODE = 'object_not_in_prerequisite_state';
        ELSIF remote_nodeinfo.node_status IS DISTINCT FROM bdr.node_status_to_char('BDR_NODE_STATUS_READY') THEN
            RAISE USING
                MESSAGE = 'remote node does not appear to be a fully running BDR node',
                DETAIL = format($$The dsn '%s' connects successfully but the target node has bdr.bdr_nodes node_status=%s instead of expected 'r'$$, remote_dsn, remote_nodeinfo.node_status),
                ERRCODE = 'object_not_in_prerequisite_state';
        END IF;

    END IF;

    -- Verify that we can make a replication connection to the remote node
    -- so that pg_hba.conf issues get caught early.
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
            -- This just shouldn't happen, so no fancy error.
            -- The all-NULLs case only arises when we're connecting to a 0.7.x
            -- peer, where we can't get the sysid etc from SQL.
            RAISE USING
                MESSAGE = 'Replication and non-replication connections to remote node reported different node id';
        END IF;

        -- In case they're NULL because of bdr_get_remote_nodeinfo
        -- due to an old upstream
        remote_sysid := remote_nodeinfo_r.sysid;
        remote_timeline := remote_nodeinfo_r.timeline;
        remote_dboid := remote_nodeinfo_r.dboid;

    END IF;

    -- Create local node record so the apply worker knows
    -- to start initializing this node with bdr_init_replica
    -- when it's started.
    --
    -- bdr_init_copy might've created a node entry in catchup
    -- mode already, in which case we can skip this.
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
            bdr.node_status_to_char('BDR_NODE_STATUS_BEGINNING_INIT'),
            node_local_dsn, remote_dsn
        );
    ELSIF bdr.node_status_from_char(cur_node.node_status) = 'BDR_NODE_STATUS_CATCHUP' THEN
        RAISE DEBUG 'starting node join in BDR_NODE_STATUS_CATCHUP';
    ELSE
        RAISE USING
            MESSAGE = 'a bdr_nodes entry for this node already exists',
            DETAIL = format('bdr.bdr_nodes entry for (%s,%s,%s) named ''%s'' with status %s exists',
                            cur_node.node_sysid, cur_node.node_timeline, cur_node.node_dboid,
                            cur_node.node_name, bdr.node_status_from_char(cur_node.node_status)),
            ERRCODE = 'object_not_in_prerequisite_state';
    END IF;

    PERFORM bdr.internal_update_seclabel();
END;
$body$;

CREATE FUNCTION bdr.acquire_global_lock(lockmode text)
RETURNS void LANGUAGE c VOLATILE STRICT
AS 'MODULE_PATHNAME','bdr_acquire_global_lock_sql';

REVOKE ALL ON FUNCTION bdr.acquire_global_lock(text) FROM public;

COMMENT ON FUNCTION bdr.acquire_global_lock(text) IS
'Acquire bdr global lock ("ddl lock") in specified mode';

CREATE OR REPLACE FUNCTION bdr.bdr_part_by_node_names(p_nodes text[])
RETURNS void LANGUAGE plpgsql VOLATILE
SET search_path = bdr, pg_catalog
SET bdr.permit_unsafe_ddl_commands = on
SET bdr.skip_ddl_replication = on
SET bdr.skip_ddl_locking = on
AS $body$
DECLARE
    unknown_node_names text := NULL;
    r record;
BEGIN
    -- concurrency
    LOCK TABLE bdr.bdr_connections IN EXCLUSIVE MODE;
    LOCK TABLE bdr.bdr_nodes IN EXCLUSIVE MODE;
    LOCK TABLE pg_catalog.pg_shseclabel IN EXCLUSIVE MODE;

    -- Ensure we're not running on the node being parted.
    -- We can't safely ensure that the change gets replicated
    -- to peer nodes before we cut off our local connections
    -- if running on the node being parted.
    --
    -- (This restriction can be lifted later if we add
    --  multi-phase negotiated part).
    --
    IF bdr.bdr_get_local_node_name() = ANY(p_nodes) THEN
        -- One exception is if we're the only live node
        IF (SELECT count(node_status) FROM bdr.bdr_nodes WHERE node_status IN (bdr.node_status_to_char('BDR_NODE_STATUS_READY'))) > 1 THEN
            RAISE USING
                MESSAGE = 'cannot part a node from its self',
                DETAIL = 'Attempted to bdr_part_by_node_names(...) on node '||bdr.bdr_get_local_node_name()||' which is one of the nodes being parted',
                HINT = 'You must call call bdr_part_by_node_names on a node that is not being removed',
                ERRCODE = 'object_in_use';
        ELSE
            RAISE WARNING USING
                MESSAGE = 'parting last node',
                HINT = 'Marking last node as parted. To remove BDR completely use bdr.remove_bdr_from_local_node(...)';
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
            MESSAGE = format('No node(s) named %s found', unknown_node_names),
            ERRCODE = 'no_data_found';
    END IF;

    FOR r IN
        SELECT
            node_name, node_status
        FROM
            bdr.bdr_nodes
            INNER JOIN unnest(p_nodes) AS to_remove(remove_node_name)
            ON (bdr_nodes.node_name = to_remove.remove_node_name)
        WHERE bdr_nodes.node_status <> bdr.node_status_to_char('BDR_NODE_STATUS_READY')
    LOOP
        IF r.node_status = bdr.node_status_to_char('BDR_NODE_STATUS_KILLED') THEN
            RAISE INFO 'Node %i is already parted, ignoring', r.node_name;
        ELSE
            RAISE WARNING 'Node % is in state % not expected ''r'' (BDR_NODE_STATUS_READY). Attempting to remove anyway.',
                r.node_name, r.node_status;
        END IF;
    END LOOP;

    UPDATE bdr.bdr_nodes
    SET node_status = bdr.node_status_to_char('BDR_NODE_STATUS_KILLED')
    WHERE node_name = ANY(p_nodes);

    -- Notify local perdb worker to kill nodes.
    PERFORM bdr.bdr_connections_changed();
END;
$body$;

--
-- Completely de-BDR-ize a node. Updated to fix #281.
--
CREATE OR REPLACE FUNCTION bdr.remove_bdr_from_local_node(force boolean DEFAULT false, convert_global_sequences boolean DEFAULT true)
RETURNS void
LANGUAGE plpgsql
SET bdr.skip_ddl_locking = on
SET bdr.permit_unsafe_ddl_commands = on
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

      PERFORM bdr._test_pause_worker_management(false);

      PERFORM pg_sleep(5);

      RAISE NOTICE 'node forced to parted state, now removing';
    ELSE
      RAISE EXCEPTION 'this BDR node might still be active, not removing';
    END IF;
  END IF;

  RAISE NOTICE 'removing BDR from node';

  -- Alter all global sequences to become local sequences.  That alone won't
  -- they're in the right position, since another node might've had numerically
  -- higher global sequence values. So we need to then move it up to the
  -- highest allocated chunk for any node and setval to it.
  IF convert_global_sequences THEN 
    IF bdr.have_global_sequences() THEN
      FOR _seqschema, _seqname, _seqmax IN
        SELECT
          n.nspname,
          c.relname,
          (
            SELECT max(upper(seqrange))
            FROM bdr.bdr_sequence_values
            WHERE seqschema = n.nspname
              AND seqname = c.relname
              AND in_use
          ) AS seqmax
        FROM pg_class c
        INNER JOIN pg_namespace n ON (c.relnamespace = n.oid)
        WHERE c.relkind = 'S'
          AND c.relam = (SELECT s.oid FROM pg_seqam s WHERE s.seqamname = 'bdr')
      LOOP
        EXECUTE format('ALTER SEQUENCE %I.%I USING local;', _seqschema, _seqname);
        -- This shouldn't be necessary, see bug #215
        IF _seqmax IS NOT NULL THEN
          EXECUTE format('SELECT setval(%L, $1)', quote_ident(_seqschema)||'.'||quote_ident(_seqname)) USING (_seqmax);
        END IF;
      END LOOP;
    ELSE
      RAISE INFO 'BDR 1.0 global sequences not supported, nothing to convert';
    END IF;
  ELSE
    RAISE NOTICE 'BDR 1.0 global sequences not converted to local; they will not work until a new nodegroup is created';
  END IF;

  -- Strip the database security label
  EXECUTE format('SECURITY LABEL FOR bdr ON DATABASE %I IS NULL', current_database());

  -- Suspend worker management, so when we terminate apply workers and
  -- walsenders they won't get relaunched.
  PERFORM bdr._test_pause_worker_management(true);

  -- Terminate every worker associated with this DB
  PERFORM bdr.terminate_walsender_workers(node_sysid, node_timeline, node_dboid)
  FROM bdr.bdr_nodes
  WHERE (node_sysid, node_timeline, node_dboid) <> bdr.bdr_get_local_nodeid();

  PERFORM bdr.terminate_apply_workers(node_sysid, node_timeline, node_dboid)
  FROM bdr.bdr_nodes
  WHERE (node_sysid, node_timeline, node_dboid) <> bdr.bdr_get_local_nodeid();

  -- Delete all connections and all nodes except the current one
  DELETE FROM bdr.bdr_connections
  WHERE (conn_sysid, conn_timeline, conn_dboid) <> bdr.bdr_get_local_nodeid();

  DELETE FROM bdr.bdr_nodes
  WHERE (node_sysid, node_timeline, node_dboid) <> bdr.bdr_get_local_nodeid();

  -- Let the perdb worker resume work and figure out everything's
  -- going away.
  PERFORM bdr._test_pause_worker_management(false);
  PERFORM bdr.bdr_connections_changed();

  -- Give it a few seconds
  PERFORM pg_sleep(2);

  -- Shut down the perdb worker
  PERFORM pg_terminate_backend(pid)
  FROM pg_stat_activity, bdr.bdr_get_local_nodeid() ni
  WHERE datname = current_database()
    AND application_name = format('bdr: (%s,%s,%s,):perdb', ni.sysid, ni.timeline, ni.dboid);

  -- Clear out the rest of bdr_nodes and bdr_connections
  DELETE FROM bdr.bdr_nodes;
  DELETE FROM bdr.bdr_connections;

  -- Drop peer replication slots for this DB
  PERFORM pg_drop_replication_slot(slot_name)
  FROM pg_catalog.pg_replication_slots,
       bdr.bdr_parse_slot_name(slot_name) ps
  WHERE ps.local_dboid = (select oid from pg_database where datname = current_database())
       AND plugin = 'bdr';

  -- and replication origins/identifiers
  CASE current_setting('server_version_num')::int / 100
  WHEN 904 THEN
    PERFORM pg_replication_identifier_drop(riname)
    FROM pg_catalog.pg_replication_identifier,
         bdr.bdr_parse_replident_name(riname) pi
    WHERE pi.local_dboid = (select oid from pg_database where datname = current_database());
  WHEN 906 THEN
    PERFORM pg_replication_origin_drop(roname)
    FROM pg_catalog.pg_replication_origin,
         bdr.bdr_parse_replident_name(roname) pi
    WHERE pi.local_dboid = (select oid from pg_database where datname = current_database());
  WHEN 1100 THEN
    PERFORM pg_replication_origin_drop(roname)
    FROM pg_catalog.pg_replication_origin,
         bdr.bdr_parse_replident_name(roname) pi
    WHERE pi.local_dboid = (select oid from pg_database where datname = current_database());
  ELSE
    RAISE EXCEPTION 'Only PostgreSQL 9.4bdr and 9.6 are supported';
  END CASE;

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

  -- Drop the on-truncate triggers. They'd otherwise get cascade-dropped
  -- when the BDR extension was dropped, but this way the system is clean. We
  -- can't drop ones under the 'bdr' schema.
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
      AND d2.refobjid = 'bdr.queue_truncate'::regproc
      AND n.nspname <> 'bdr'
  LOOP
    EXECUTE format('DROP TRIGGER %I ON %I.%I',
         _truncate_tg.tgname, _truncate_tg.tgrelnsp, _truncate_tg.tgrelname);

    -- The trigger' dependency entry will be dangling because of how we
    -- dropped it
    DELETE FROM pg_depend
    WHERE classid = 'pg_trigger'::regclass
      AND objid = _truncate_tg.tgobjid
      AND (refclassid = 'pg_proc'::regclass AND refobjid = 'bdr.queue_truncate'::regproc)
          OR
          (refclassid = 'pg_class'::regclass AND refobjid = _truncate_tg.tgrelid);

  END LOOP;

  -- Delete the other detritus from the extension. The user should really drop it,
  -- but we should try to restore a clean state anyway.
  DELETE FROM bdr.bdr_queued_commands;
  DELETE FROM bdr.bdr_queued_drops;
  DELETE FROM bdr.bdr_global_locks;
  DELETE FROM bdr.bdr_conflict_handlers;
  DELETE FROM bdr.bdr_conflict_history;
  DELETE FROM bdr.bdr_replication_set_config;
  DELETE FROM bdr.bdr_sequence_elections;
  DELETE FROM bdr.bdr_sequence_values;
  DELETE FROM bdr.bdr_votes;

  -- We can't drop the BDR extension, we just need to tell the
  -- user to do that.
  RAISE NOTICE 'BDR removed from this node. You can now DROP EXTENSION bdr and, if this is the last BDR node on this PostgreSQL instance, remove bdr from shared_preload_libraries.';
END;
$$;

-- bdr.bdr_nodes gets synced by bdr_sync_nodes(), it shouldn't be
-- dumped and applied.
SELECT pg_catalog.pg_extension_config_dump('bdr_nodes', 'WHERE false');

--
-- An oversight in introducing commit timestamps and replication origins
-- to 9.6 means that on 9.6 we don't have any equivalent to 9.4bdr's
-- pg_get_transaction_extradata(xid) function. Add a wrapper in BDR
-- for now.
--
CREATE FUNCTION bdr.get_transaction_replorigin(xid) RETURNS oid
LANGUAGE c AS 'MODULE_PATHNAME','bdr_get_transaction_replorigin';

REVOKE ALL ON FUNCTION bdr.get_transaction_replorigin(xid) FROM public;

COMMENT ON FUNCTION bdr.get_transaction_replorigin(xid)
IS 'get the replication origin id for a given transaction if still known';

--
-- When upgrading an existing cluster we must assign node sequence IDs.
--
-- We can't do that safely during the upgrade script run since the changes
-- won't get replicated, so we have do it as a user-initiated post-upgrade
-- step.
--
CREATE FUNCTION bdr.upgrade_to_200() RETURNS void LANGUAGE plpgsql AS
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
      bdr.node_status_to_char('BDR_NODE_STATUS_READY'),
      bdr.node_status_to_char('BDR_NODE_STATUS_KILLED')
    )
  LOOP
    errd := errd || 'node % has status % (%);', n.node_name, n.node_status;
    dofail := true;
  END LOOP;
  IF dofail THEN
    RAISE USING
        MESSAGE = 'cannot upgrade BDR extension because some nodes are not ready',
        DETAIL = errd,
        HINT = 'Make sure no nodes are joining or partially joined in bdr.bdr_nodes',
        ERRCODE = 'object_not_in_prerequisite_state';
  END IF;

  -- if all nodes look sensible, generate sequence IDs, skipping over any
  -- already-assigned values, and start counting from the lowest assigned
  -- value. In theory there shouldn't be one, but we don't actively stop
  -- users joining nodes when some other nodes have no node_seq_id, so there
  -- could be...
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
          WHERE n3.node_status NOT IN (bdr.node_status_to_char('BDR_NODE_STATUS_KILLED'))
        ), 0)
      ) AS node_seq_id
    FROM bdr.bdr_nodes n2
    WHERE n2.node_status NOT IN (bdr.node_status_to_char('BDR_NODE_STATUS_KILLED'))
    AND n2.node_seq_id IS NULL
  ) n(sysid, timeline, dboid, seqid)
  WHERE (node_sysid, node_timeline, node_dboid) =
	(sysid, timeline, dboid)
  AND node_seq_id IS NULL;

END;
$$;

CREATE OR REPLACE FUNCTION bdr.global_lock_info(
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
RETURNS record VOLATILE
LANGUAGE c AS 'MODULE_PATHNAME','bdr_ddl_lock_info';

COMMENT ON FUNCTION bdr.global_lock_info() IS
'backing function for bdr.bdr_locks view';

CREATE VIEW bdr.bdr_locks AS
SELECT
 owner_replorigin = 0 AS owner_is_my_node,
 owner_sysid, owner_timeline, owner_dboid,
 (SELECT node_name FROM bdr.bdr_nodes WHERE (node_sysid,node_timeline,node_dboid) = (owner_sysid, owner_timeline, owner_dboid)) AS owner_node_name,
 lock_mode, lock_state, owner_local_pid,
 coalesce(owner_local_pid = pg_backend_pid(),'f') AS owner_is_my_backend,
 owner_replorigin,
 lockcount, npeers, npeers_confirmed, npeers_declined, npeers_replayed,
 replay_upto
FROM bdr.global_lock_info();

COMMENT ON VIEW bdr.bdr_locks IS
'diagnostic information on BDR global locking state, see manual';

RESET bdr.permit_unsafe_ddl_commands;
RESET bdr.skip_ddl_replication;
RESET search_path;
