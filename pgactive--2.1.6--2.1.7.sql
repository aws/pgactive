/* pgactive--2.1.6--2.1.7.sql */

-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION pgactive UPDATE TO '2.1.7'" to load this file. \quit

SET pgactive.skip_ddl_replication = true;
SET LOCAL search_path = pgactive;
-- Start Upgrade SQLs/Functions/Procedures 

DROP FUNCTION pgactive_create_group (text, text, integer, text[]);

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

    -- Warn users about missing primary keys and replica identity index
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
          AND c.oid IS NULL  AND r.relreplident != 'i'
    LOOP
        RAISE WARNING USING
            MESSAGE = format('table %I.%I has no PRIMARY KEY', t.nspname, t.relname),
            HINT = 'Tables without a PRIMARY KEY and REPLICA IDENTITY INDEX cannot be UPDATED or DELETED from, only INSERTED into. Add a PRIMARY KEY or a REPLICA IDENTITY INDEX.';
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
        bypass_user_tables_check := true);
END;
$body$;

COMMENT ON FUNCTION pgactive_create_group(text, text, integer, text[]) IS
'Create a pgactive group, turning a stand-alone database into the first node in a pgactive group';

REVOKE ALL ON FUNCTION pgactive_create_group(text, text, integer, text[]) FROM public;

-- Finish Upgrade SQLs/Functions/Procedures 
RESET pgactive.skip_ddl_replication;
RESET search_path;
