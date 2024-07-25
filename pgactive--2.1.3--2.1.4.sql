/* pgactive--2.1.3--2.1.4.sql */

-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION pgactive UPDATE TO '2.1.4'" to load this file. \quit

SET pgactive.skip_ddl_replication = true;
-- Everything should assume the 'pgactive' prefix
SET LOCAL search_path = pgactive;

-- Fix quoting for format() arguments by directly using regclass with %s
-- instead of %I
CREATE OR REPLACE FUNCTION pgactive_set_table_replication_sets(p_relation regclass, p_sets text[])
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
BEGIN
    -- emulate STRICT for p_relation parameter
    IF p_relation IS NULL THEN
        RETURN;
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
        WHERE p_sets IS NOT NULL
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

-- RESET pgactive.permit_unsafe_ddl_commands; is removed for now
RESET pgactive.skip_ddl_replication;
RESET search_path;
