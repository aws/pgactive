::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-1.0.1.md "Release 1.0.1"){accesskey="P"}   [Up](releasenotes.md){accesskey="U"}    Appendix A. Release notes    [Next](release-0.9.3.md "Release 0.9.3"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [A.9. Release 1.0.0]{#RELEASE-1.0.0} {#a.9.-release-1.0.0 .SECT1}

The 1.0.0 release improves schema change DDL locking, the documentation,
managment tools, and more. It is [a straightforward upgrade for 0.9.x
users](upgrade.md), however they should upgrade to bdr-pg 9.4.9 before
upgrading to bdr-plugin 1.0.0.

The most important changes are:

-   improvements to DDL locking to reduce its operational impact by
    allowing write transactions a grace period before cancellation,
    blocking new write transactions instead of ERRORing, and allowing
    many DDL operations to avoid blocking row writes at all

-   the removal of UDR

-   a fix for dropped column handling when cloning new nodes via
    bdr_node_join

-   global sequence improvements to improve behavior when rapidly
    consuming sequence values

There are important compatibility changes in this release. BDR 1.0
removes UDR. If you need one-way replication on unpatched community
PostgreSQL 9.4/9.5/9.6, use
[pglogical](2ndquadrant.com/pglogical) instead. BDR 1.0
is also fully not interoperable with 0.9.x; it cannot join a 0.9.x group
and 0.9.x cannot join a 1.0 group. However individual nodes in the
existing 0.9.x cluster can be upgraded to 1.0 one by one, keeping the
cluster as a whole available during the process.

Changes in release:

-   Fix handling of dropped columns during logical node init to prevent
    \"tuple natts mismatch\" errors (git 9323f3, #113, #114)

-   Remove UDR

-   Wait up to a minute for new sequence values from an exhausted global
    sequence before ERRORing (git f7e9b4)

-   Don\'t acquire global DDL lock for non-schema-qualified temp table
    DROPs (#124)

-   Add a grace period before killing all write transactions when
    acquiring DDL lock, configured by bdr.max_ddl_lock_delay (git
    0e02cc27, 91a0d2505)

-   Make DML wait when DDL lock held, instead of ERRORing (git b754c0c4)

-   Add a DDL-lock-specific timeout, bdr.bdr_ddl_lock_timeout (git
    91a0d2505)

-   Add a new DDL lock type that only blocks other DDL, not DML
    (insert/update/delete) (git 10b331fe, 6d2a09fb)

-   Cache more values for global sequences, make cache size configurable
    with cache_chunks reloption (git 071e94)

-   Ensure sequence cache is never re-used after physical node copy
    (#101)

-   Increase default sequence chunk size to 10k (git ff0726)

-   Expand node part/join sanity checks and error messages (git 6c58df
    and others)

-   Permit DML on pg_catalog tables (#102)

-   Create BDR\'s internal TRUNCATE triggers as internal triggers (git
    0c96b9ff, #62)

-   Apply workers notice replication set changes and DSN changes without
    manual termination/restart (git 7faf648e)

-   Make bdr_apply_resume() take immediate effect (git 72eb77)

-   Ignore dangling bdr_connections rows without a corresponding
    bdr_nodes entry (git 509848, #50, #97, #126)

-   Don\'t acquire the global DDL lock when creating or dropping indexes
    on temporary tables (git aebd93, courtesy of Michael Allen)

-   Fix crash on insert into a table with an expression index (git
    06f52b, courtesy of Abdul Yadi)

-   Make bdr_supervisordb checks less strict to fix vacuumdb -a and
    other tools (git dba912, #154)

-   Group multiple replicated TRUNCATEs into a single command to fix
    TRUNCATE with foreign keys or TRUNCATE CASCADE (git fcdf1b, #48)

-   Add management functions for connection replication sets

-   Add low level apply/replay tracing via bdr.trace_replay (git
    4feb4004b, #185)

-   Add DDL lock tracing/logging via bdr.trace_ddl_locks_level (git
    beee79dd4)

-   Allow specification of connection options for all BDR connections
    with bdr.extra_apply_connection_options (git 491f5e90a, #173)

-   Enable TCP keepalives by default on apply worker (#173)

-   Backport access to \'pid\' and \'confirmed_flush_lsn\' columns of
    pg_replication_slots from 9.5 and 9.6 via new
    bdr.pg_replication_slots view (#186).

-   Add functions to terminate apply and walsender workers
    (bdr_terminate_walsender_workers and bdr_terminate_apply_workers)

-   Add a function to allow BDR workers to skip over changes (#181)

-   Add functions to control replication sets configured for a
    connection

-   Add a function to completely remove BDR from a node,
    bdr.remove_bdr_from_local_node()

-   Extensive documentation updates

The biggest changes are around [DDL
Locking](ddl-replication-advice.md#DDL-REPLICATION-LOCKING).
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-1.0.1.md){accesskey="P"}      [Home](index.md){accesskey="H"}       [Next](release-0.9.3.md){accesskey="N"}
  Release 1.0.1                                [Up](releasenotes.md){accesskey="U"}                                Release 0.9.3
  ------------------------------------------- ---------------------------------------- -------------------------------------------
:::
