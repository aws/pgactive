/* pgactive--2.1.9--2.1.10.sql */

-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION pgactive UPDATE TO '2.1.10'" to load this file. \quit

SET pgactive.skip_ddl_replication = true;
SET LOCAL search_path = pgactive;
-- Start Upgrade SQLs/Functions/Procedures

-- Fresh 2.1.9 installs left this executable by public.
REVOKE ALL ON FUNCTION pgactive_get_replication_lag_info() FROM public;

-- Finish Upgrade SQLs/Functions/Procedures
RESET pgactive.skip_ddl_replication;
RESET search_path;
