/* pgactive--2.1.8--2.1.9.sql */

-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION pgactive UPDATE TO '2.1.9'" to load this file. \quit

SET pgactive.skip_ddl_replication = true;
SET LOCAL search_path = pgactive;
-- Start Upgrade SQLs/Functions/Procedures

REVOKE ALL ON FUNCTION pgactive_version() FROM public;
REVOKE ALL ON FUNCTION pgactive_is_active_in_db() FROM public;
REVOKE ALL ON FUNCTION pgactive_is_apply_paused() FROM public;
REVOKE ALL ON FUNCTION pgactive_get_local_node_name() FROM public;
REVOKE ALL ON FUNCTION pgactive_get_global_locks_info() FROM public;
REVOKE ALL ON FUNCTION pgactive_get_last_applied_xact_info(text, oid, oid) FROM public;
REVOKE ALL ON FUNCTION pgactive_get_replication_lag_info() FROM public;
REVOKE ALL ON FUNCTION pgactive_get_table_replication_sets(regclass) FROM public;
REVOKE ALL ON FUNCTION pgactive_set_table_replication_sets(regclass, boolean) FROM public;
REVOKE ALL ON FUNCTION pgactive_include_table_replication_set(regclass) FROM public;
REVOKE ALL ON FUNCTION pgactive_exclude_table_replication_set(regclass) FROM public;
REVOKE ALL ON FUNCTION pgactive_create_conflict_handler(regclass, name, regprocedure, pgactive.pgactive_conflict_type, interval) FROM public;
REVOKE ALL ON FUNCTION pgactive_drop_conflict_handler(regclass, name) FROM public;
REVOKE ALL ON FUNCTION pgactive_replicate_ddl_command(text) FROM public;

-- Finish Upgrade SQLs/Functions/Procedures
RESET pgactive.skip_ddl_replication;
RESET search_path;
