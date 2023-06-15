::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ---------------------------------------------------------------------- ----------------------------------- -- ---
  [Prev](technotes-rewrites.md "Full table rewrites"){accesskey="P"}   [Home](index.md){accesskey="H"}         

------------------------------------------------------------------------
:::

::: INDEX
# []{#BOOKINDEX}Index

[A](bookindex.md#AEN5129) \| [B](bookindex.md#AEN5173) \|
[C](bookindex.md#AEN5319) \| [D](bookindex.md#AEN5411) \|
[F](bookindex.md#AEN5452) \| [G](bookindex.md#AEN5457) \|
[L](bookindex.md#AEN5463) \| [M](bookindex.md#AEN5474) \|
[N](bookindex.md#AEN5489) \| [P](bookindex.md#AEN5494) \|
[R](bookindex.md#AEN5502) \| [S](bookindex.md#AEN5513) \|
[T](bookindex.md#AEN5527) \| [U](bookindex.md#AEN5532) \|
[W](bookindex.md#AEN5537)

::: INDEXDIV
## [A]{#AEN5129} {#a .INDEXDIV}

ALTER DATABASE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

ALTER FOREIGN DATA WRAPPER, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

ALTER GROUP, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

ALTER INDEX, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

ALTER OPERATOR FAMILY, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

ALTER ROLE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

ALTER SEQUENCE, [DDL statements with
restrictions](ddl-replication-statements.md#DDL-REPLICATION-RESTRICTED-COMMANDS)

ALTER SERVER, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

ALTER TABLE, [DDL statements with
restrictions](ddl-replication-statements.md#DDL-REPLICATION-RESTRICTED-COMMANDS)

ALTER TABLESPACE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

ALTER TEXT SEARCH CONFIGURATION, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

ALTER TEXT SEARCH DICTIONARY, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

ALTER USER, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

ALTER USER MAPPING, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)
:::

::: INDEXDIV
## [B]{#AEN5173} {#b .INDEXDIV}

bdr.acquire_global_lock, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_apply_is_paused, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_apply_pause, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_apply_resume, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_create_conflict_handler, [Conflict handler management
functions](functions-conflict-handlers.md)

bdr.bdr_drop_conflict_handler, [Conflict handler management
functions](functions-conflict-handlers.md)

bdr.bdr_get_local_nodeid, [Information
functions](functions-information.md)

bdr.bdr_get_local_node_name, [Information
functions](functions-information.md)

bdr.bdr_get_remote_nodeinfo, [Information
functions](functions-information.md)

bdr.bdr_get_workers_info(text,oid,oid), [Node management
functions](functions-node-mgmt.md)

bdr.bdr_group_create, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_group_join, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_is_active_in_db, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_min_remote_version_num, [Information
functions](functions-information.md)

bdr.bdr_node_join_wait_for_ready, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_node_set_read_only, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_part_by_node_names, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_replicate_ddl_command, [Node management
functions](functions-node-mgmt.md)

bdr.bdr_terminate_workers(text,oid,oid,text), [Node management
functions](functions-node-mgmt.md)

bdr.bdr_test_remote_connectback, [Information
functions](functions-information.md)

bdr.bdr_upgrade_to_090, [Upgrade functions](functions-upgrade.md)

bdr.bdr_version, [Information functions](functions-information.md)

bdr.bdr_version_num, [Information functions](functions-information.md)

bdr.conflict_logging_include_tuples configuration parameter, [BDR
specific configuration variables](bdr-configuration-variables.md)

bdr.connection_get_replication_sets, [Replication Set
functions](functions-replication-sets.md)

bdr.connection_get_replication_sets(text), [Replication Set
functions](functions-replication-sets.md)

bdr.connection_set_replication_sets, [Replication Set
functions](functions-replication-sets.md)

bdr.connection_set_replication_sets(text\[\],text), [Replication Set
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

bdr.log_conflicts_to_table configuration parameter, [BDR specific
configuration variables](bdr-configuration-variables.md)

bdr.max_ddl_lock_delay configuration parameter, [BDR specific
configuration variables](bdr-configuration-variables.md)

bdr.permit_ddl_locking configuration parameter, [BDR specific
configuration variables](bdr-configuration-variables.md)

bdr.permit_unsafe_ddl_commands configuration parameter, [Less common or
internal configuration
variables](bdr-configuration-variables.md#AEN783)

bdr.remove_bdr_from_local_node, [Node management
functions](functions-node-mgmt.md)

bdr.skip_changes_upto, [Node management
functions](functions-node-mgmt.md)

bdr.skip_ddl_locking configuration parameter, [Less common or internal
configuration variables](bdr-configuration-variables.md#AEN783)

bdr.skip_ddl_replication configuration parameter, [Less common or
internal configuration
variables](bdr-configuration-variables.md#AEN783)

bdr.synchronous_commit configuration parameter, [BDR specific
configuration variables](bdr-configuration-variables.md)

bdr.table_get_replication_sets, [Replication Set
functions](functions-replication-sets.md)

bdr.table_set_replication_sets, [Replication Set
functions](functions-replication-sets.md)

bdr.temp_dump_directory configuration parameter, [BDR specific
configuration variables](bdr-configuration-variables.md)

bdr.trace_ddl_locks_level configuration parameter, [BDR specific
configuration variables](bdr-configuration-variables.md)

bdr.trace_replay configuration parameter, [Less common or internal
configuration variables](bdr-configuration-variables.md#AEN783)

bdr.wait_slot_confirm_lsn, [Node management
functions](functions-node-mgmt.md)
:::

::: INDEXDIV
## [C]{#AEN5319} {#c .INDEXDIV}

Catalogs

Views, [Catalogs and Views](catalogs-views.md)

see also Monitoring

Command-line Utilities, [Command-line Utilities](commands.md)

Configuration Settings

of the server, [Configuration Settings](settings.md)

:

Conflicts, [Multi-master conflicts](conflicts.md)

CREATE CAST, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE COLLATION, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE CONVERSION, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE DATABASE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

CREATE FOREIGN DATA WRAPPER, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE GROUP, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

CREATE INDEX, [DDL statements with
restrictions](ddl-replication-statements.md#DDL-REPLICATION-RESTRICTED-COMMANDS)

CREATE INDEX CONCURRENTLY, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

CREATE LANGUAGE, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE MATERIALIZED VIEW, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE OPERATOR CLASS, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE OPERATOR FAMILY, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE ROLE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

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
statements](ddl-replication-statements.md#AEN1519)

CREATE TEXT SEARCH CONFIGURATION, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE TEXT SEARCH DICTIONARY, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE TEXT SEARCH PARSER, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE TEXT SEARCH TEMPLATE, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

CREATE USER, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

CREATE USER MAPPING, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)
:::

::: INDEXDIV
## [D]{#AEN5411} {#d .INDEXDIV}

DDL Replication, [DDL Replication](ddl-replication.md)

default_sequenceam configuration parameter, [Prerequisite PostgreSQL
parameters](settings-prerequisite.md)

divergence, [INSERTs that violate multiple UNIQUE
constraints](conflicts-types.md#CONFLICTS-INSERT-UNIQUE-MULTIPLE-INDEX),
[UPDATE conflicts on the PRIMARY
KEY](conflicts-types.md#CONFLICTS-UPDATE-PK), [UPDATEs that violate
multiple UNIQUE
constraints](conflicts-types.md#CONFLICTS-UPDATE-UNIQUE-MULTIPLE-INDEX),
[UPDATE/DELETE conflicts](conflicts-types.md#CONFLICTS-UPDATE-DELETE),
[INSERT/UPDATE conflicts](conflicts-types.md#CONFLICTS-INSERT-UPDATE),
[Global data conflicts](conflicts-types.md#AEN2387), [Divergent
conflicts](conflicts-types.md#CONFLICTS-DIVERGENT)

DROP DATABASE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

DROP GROUP, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

DROP INDEX CONCURRENTLY, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

DROP OWNED, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

DROP ROLE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

DROP TABLESPACE, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

DROP USER, [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

DROP USER MAPPING, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)
:::

::: INDEXDIV
## [F]{#AEN5452} {#f .INDEXDIV}

Functions, [Functions](functions.md)
:::

::: INDEXDIV
## [G]{#AEN5457} {#g .INDEXDIV}

Global Sequence

:

    Distributed sequence, [Global Sequences](global-sequences.md)

    :
:::

::: INDEXDIV
## [L]{#AEN5463} {#l .INDEXDIV}

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
conflicts](conflicts-types.md#AEN2387)
:::

::: INDEXDIV
## [M]{#AEN5474} {#m .INDEXDIV}

max_replication_slots configuration parameter, [Prerequisite PostgreSQL
parameters](settings-prerequisite.md)

max_wal_senders configuration parameter, [Prerequisite PostgreSQL
parameters](settings-prerequisite.md)

max_worker_processes configuration parameter, [Prerequisite PostgreSQL
parameters](settings-prerequisite.md)

Monitoring, [Monitoring](monitoring.md)

see also Catalogs
:::

::: INDEXDIV
## [N]{#AEN5489} {#n .INDEXDIV}

Node Management, [Node Management](node-management.md)
:::

::: INDEXDIV
## [P]{#AEN5494} {#p .INDEXDIV}

pg_xlog_wait_remote_apply, [Node management
functions](functions-node-mgmt.md)

pg_xlog_wait_remote_receive, [Node management
functions](functions-node-mgmt.md)
:::

::: INDEXDIV
## [R]{#AEN5502} {#r .INDEXDIV}

REFRESH MATERIALIZED VIEW, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

Release notes, [Release notes](releasenotes.md)

Replication Sets, [Replication Sets](replication-sets.md)
:::

::: INDEXDIV
## [S]{#AEN5513} {#s .INDEXDIV}

SECURITY LABEL, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

SELECT INTO, [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

shared_preload_libraries configuration parameter, [Prerequisite
PostgreSQL parameters](settings-prerequisite.md)

subscribe,
[bdr.bdr_subscribe](functions-node-mgmt.md#FUNCTIONS-NODE-MGMT-SUBSCRIBE)
:::

::: INDEXDIV
## [T]{#AEN5527} {#t .INDEXDIV}

track_commit_timestamp configuration parameter, [Prerequisite PostgreSQL
parameters](settings-prerequisite.md)
:::

::: INDEXDIV
## [U]{#AEN5532} {#u .INDEXDIV}

Upgrading BDR, [Upgrading BDR](upgrade.md)
:::

::: INDEXDIV
## [W]{#AEN5537} {#w .INDEXDIV}

wal_level configuration parameter, [Prerequisite PostgreSQL
parameters](settings-prerequisite.md)
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------------ ----------------------------------- ---
  [Prev](technotes-rewrites.md){accesskey="P"}    [Home](index.md){accesskey="H"}     
  Full table rewrites                                                                     
  ------------------------------------------------ ----------------------------------- ---
:::
