  [BDR 2.1.0 Documentation](README.md)                                                                         
  ---------------------------------------------------------------------- ----------------------------------- -- ---
  [Prev](technotes-rewrites.md "Full table rewrites")   [Home](README.md)         


# Index

[A](bookindex.md#AEN5174) \| [B](bookindex.md#AEN5218) \|
[C](bookindex.md#AEN5376) \| [D](bookindex.md#AEN5468) \|
[F](bookindex.md#AEN5509) \| [G](bookindex.md#AEN5514) \|
[L](bookindex.md#AEN5520) \| [M](bookindex.md#AEN5531) \|
[N](bookindex.md#AEN5546) \| [P](bookindex.md#AEN5551) \|
[R](bookindex.md#AEN5559) \| [S](bookindex.md#AEN5570) \|
[T](bookindex.md#AEN5584) \| [U](bookindex.md#AEN5589) \|
[W](bookindex.md#AEN5594)

## [A]{#AEN5174}

ALTER DATABASE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

ALTER FOREIGN DATA WRAPPER, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

ALTER GROUP, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

ALTER INDEX, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

ALTER OPERATOR FAMILY, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

ALTER ROLE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

ALTER SEQUENCE, [DDL statements with
restrictions](ddl-replication-statements.md#DDL-REPLICATION-RESTRICTED-COMMANDS)

ALTER SERVER, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

ALTER TABLE, [DDL statements with
restrictions](ddl-replication-statements.md#DDL-REPLICATION-RESTRICTED-COMMANDS)

ALTER TABLESPACE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

ALTER TEXT SEARCH CONFIGURATION, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

ALTER TEXT SEARCH DICTIONARY, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

ALTER USER, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

ALTER USER MAPPING, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

## [B]{#AEN5218}

bdr.bdr_acquire_global_lock, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_is_apply_paused, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_apply_pause, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_apply_resume, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_create_conflict_handler, [Conflict handler management
functions](functions-conflict-handlers.md)

bdr.bdr_drop_conflict_handler, [Conflict handler management
functions](functions-conflict-handlers.md)

bdr.bdr_generate_node_identifier(), [Node management
functions](functions-node-mgmt.md)

bdr.bdr_get_local_nodeid, [Information
functions](functions-information.md)

bdr.bdr_get_local_node_name, [Information
functions](functions-information.md)

bdr.bdr_get_node_identifier(), [Node management
functions](functions-node-mgmt.md)

bdr.bdr_get_remote_nodeinfo, [Information
functions](functions-information.md)

bdr.bdr_get_workers_info(text,oid,oid), [Node management
functions](functions-node-mgmt.md)

bdr.bdr_create_group, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_join_group, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_is_active_in_db, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_min_remote_version_num, [Information
functions](functions-information.md)

bdr.bdr_wait_for_node_ready, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_set_node_read_only, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_detach_nodes, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_replicate_ddl_command, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_terminate_workers(text,oid,oid,text), [Node management
functions](functions-node-mgmt.md)

bdr.bdr_test_remote_connectback, [Information
functions](functions-information.md)

bdr.bdr_assign_seq_ids_post_upgrade, [Upgrade functions](functions-upgrade.md)

bdr.bdr_version, [Information functions](functions-information.md)

bdr.bdr_version_num, [Information functions](functions-information.md)

bdr.conflict_logging_include_tuples configuration parameter, [BDR
specific configuration variables](bdr-configuration-variables.md)

bdr.bdr_get_connection_replication_sets, [Replication Set
functions](functions-replication-sets.md)

bdr.bdr_get_connection_replication_sets(text), [Replication Set
functions](functions-replication-sets.md)

bdr.bdr_set_connection_replication_sets, [Replication Set
functions](functions-replication-sets.md)

bdr.bdr_set_connection_replication_sets(text\[\],text), [Replication Set
functions](functions-replication-sets.md)

bdr.ddl_lock_timeout configuration parameter, [BDR specific
configuration variables](bdr-configuration-variables.md)

bdr.default_apply_delay configuration parameter, [Less common or
internal configuration
variables](bdr-configuration-variables.md#AEN783)

bdr.discard_mismatched_row_attributes configuration parameter, [Less
common or internal configuration
variables](bdr-configuration-variables.md#AEN783)

bdr.do_not_replicate configuration parameter, [Less common or internal
configuration variables](bdr-configuration-variables.md#AEN783)

bdr.extra_apply_connection_options configuration parameter, [Less common
or internal configuration
variables](bdr-configuration-variables.md#AEN783)

bdr.init_node_parallel_jobs configuration parameter, [Less common or
internal configuration
variables](bdr-configuration-variables.md#AEN783)

bdr.log_conflicts_to_table configuration parameter, [BDR specific
configuration variables](bdr-configuration-variables.md)

bdr.max_ddl_lock_delay configuration parameter, [BDR specific
configuration variables](bdr-configuration-variables.md)

bdr.bdr_remove, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_skip_changes, [Node management
functions](functions-node-mgmt.md)

bdr.skip_ddl_replication configuration parameter, [Less common or
internal configuration
variables](bdr-configuration-variables.md#AEN783)

bdr.synchronous_commit configuration parameter, [BDR specific
configuration variables](bdr-configuration-variables.md)

bdr.bdr_get_table_replication_sets, [Replication Set
functions](functions-replication-sets.md)

bdr.bdr_set_table_replication_sets, [Replication Set
functions](functions-replication-sets.md)

bdr.temp_dump_directory configuration parameter, [BDR specific
configuration variables](bdr-configuration-variables.md)

bdr.trace_ddl_locks_level configuration parameter, [BDR specific
configuration variables](bdr-configuration-variables.md)

bdr.trace_replay configuration parameter, [Less common or internal
configuration variables](bdr-configuration-variables.md#AEN783)

bdr.bdr_wait_for_slots_confirmed_flush_lsn, [Node management
functions](functions-node-mgmt.md)

## [C]{#AEN5376}

Catalogs

Views, [Catalogs and Views](catalogs-views.md)

see also Monitoring

Command-line Utilities, [Command-line Utilities](commands.md)

Configuration Settings

of the server, [Configuration Settings](settings.md)

    

Conflicts, [Active-Active conflicts](conflicts.md)

CREATE CAST, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE COLLATION, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE CONVERSION, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE DATABASE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

CREATE FOREIGN DATA WRAPPER, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE GROUP, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

CREATE INDEX, [DDL statements with
restrictions](ddl-replication-statements.md#DDL-REPLICATION-RESTRICTED-COMMANDS)

CREATE INDEX CONCURRENTLY, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

CREATE LANGUAGE, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE MATERIALIZED VIEW, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE OPERATOR CLASS, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE OPERATOR FAMILY, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE ROLE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

CREATE SEQUENCE, [DDL statements with
restrictions](ddl-replication-statements.md#DDL-REPLICATION-RESTRICTED-COMMANDS)

CREATE SERVER, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE TABLE, [DDL statements with
restrictions](ddl-replication-statements.md#DDL-REPLICATION-RESTRICTED-COMMANDS)

CREATE TABLE \... OF TYPE, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE TABLE AS, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE TABLESPACE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

CREATE TEXT SEARCH CONFIGURATION, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE TEXT SEARCH DICTIONARY, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE TEXT SEARCH PARSER, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE TEXT SEARCH TEMPLATE, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE USER, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

CREATE USER MAPPING, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

## [D]{#AEN5468}

DDL Replication, [DDL Replication](ddl-replication.md)

divergence, [INSERTs that violate multiple UNIQUE
constraints](conflicts-types.md#CONFLICTS-INSERT-UNIQUE-MULTIPLE-INDEX),
[UPDATE conflicts on the PRIMARY
KEY](conflicts-types.md#CONFLICTS-UPDATE-PK), [UPDATEs that violate
multiple UNIQUE
constraints](conflicts-types.md#CONFLICTS-UPDATE-UNIQUE-MULTIPLE-INDEX),
[UPDATE/DELETE conflicts](conflicts-types.md#CONFLICTS-UPDATE-DELETE),
[INSERT/UPDATE conflicts](conflicts-types.md#CONFLICTS-INSERT-UPDATE),
[Global data conflicts](conflicts-types.md#AEN2404), [Divergent
conflicts](conflicts-types.md#CONFLICTS-DIVERGENT)

DROP DATABASE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

DROP GROUP, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

DROP INDEX CONCURRENTLY, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

DROP OWNED, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

DROP ROLE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

DROP TABLESPACE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

DROP USER, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1536)

DROP USER MAPPING, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

## [F]{#AEN5509}

Functions, [Functions](functions.md)

## [G]{#AEN5514}

Global Sequence

    

    Distributed sequence, [Global Sequences](global-sequences.md)

    :   

## [L]{#AEN5520}

limitations, [INSERTs that violate multiple UNIQUE
constraints](conflicts-types.md#CONFLICTS-INSERT-UNIQUE-MULTIPLE-INDEX),
[UPDATE conflicts on the PRIMARY
KEY](conflicts-types.md#CONFLICTS-UPDATE-PK), [UPDATEs that violate
multiple UNIQUE
constraints](conflicts-types.md#CONFLICTS-UPDATE-UNIQUE-MULTIPLE-INDEX),
[UPDATE/DELETE conflicts](conflicts-types.md#CONFLICTS-UPDATE-DELETE),
[INSERT/UPDATE conflicts](conflicts-types.md#CONFLICTS-INSERT-UPDATE),
[Exclusion constraint
conflicts](conflicts-types.md#CONFLICTS-EXCLUSION), [Global data
conflicts](conflicts-types.md#AEN2404)

## [M]{#AEN5531}

max_replication_slots configuration parameter, [Prerequisite PostgreSQL
parameters](settings-prerequisite.md)

max_wal_senders configuration parameter, [Prerequisite PostgreSQL
parameters](settings-prerequisite.md)

max_worker_processes configuration parameter, [Prerequisite PostgreSQL
parameters](settings-prerequisite.md)

Monitoring, [Monitoring](monitoring.md)

see also Catalogs

## [N]{#AEN5546}

Node Management, [Node Management](node-management.md)

## [P]{#AEN5551}

pg_xlog_wait_remote_apply, [Node management
functions](functions-node-mgmt.md)

pg_xlog_wait_remote_receive, [Node management
functions](functions-node-mgmt.md)

## [R]{#AEN5559}

REFRESH MATERIALIZED VIEW, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

Release notes, [Release notes](releasenotes.md)

Replication Sets, [Replication Sets](replication-sets.md)

## [S]{#AEN5570}

SECURITY LABEL, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

SELECT INTO, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

shared_preload_libraries configuration parameter, [Prerequisite
PostgreSQL parameters](settings-prerequisite.md)

## [T]{#AEN5584}

track_commit_timestamp configuration parameter, [Prerequisite PostgreSQL
parameters](settings-prerequisite.md)

## [U]{#AEN5589}

Upgrading BDR, [Upgrading BDR](upgrade.md)

## [W]{#AEN5594}

wal_level configuration parameter, [Prerequisite PostgreSQL
parameters](settings-prerequisite.md)



  ------------------------------------------------ ----------------------------------- ---
  [Prev](technotes-rewrites.md)    [Home](README.md)     
  Full table rewrites                                                                     
  ------------------------------------------------ ----------------------------------- ---
