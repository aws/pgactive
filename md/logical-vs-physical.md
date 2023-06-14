::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ----------------------------------------------------- ------------------------------------ ------------------------- ----------------------------------------------------------------------------------------------------------
  [Prev](bdr-concepts.md "Concepts"){accesskey="P"}   [Up](overview.md){accesskey="U"}    Chapter 1. BDR overview    [Next](bdr-vs-trigger-based.md "Differences between BDR and trigger-based replication"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [1.2. Differences between logical (row level) and physical (block level) replication]{#LOGICAL-VS-PHYSICAL} {#differences-between-logical-row-level-and-physical-block-level-replication .SECT1}

BDR uses [PostgreSQL\'s logical decoding
feature](http://www.postgresql.org/docs/current/static/logicaldecoding.html){target="_top"}
to implement a low overhead logical replication solution. It has
significant advantages - and some disadvantages - when compared to
PostgreSQL\'s older physical (block-based)
[streaming](http://www.postgresql.org/docs/current/static/warm-standby.html#STREAMING-REPLICATION){target="_top"}
or
[archive-based](http://www.postgresql.org/docs/current/static/warm-standby.html){target="_top"}
replication with warm or [hot
standby](http://www.postgresql.org/docs/current/static/hot-standby.html){target="_top"}

Logical replication has a different set of trade-offs to physical
block-based replication. It isn\'t clearly better or worse. Physical
replication is a lot simpler, has less lag for big transactions, is
supported by older versions and may require less disk I/O, but generally
consumes more network bandwidth, can\'t replicate a subset of databases
or tables, and can\'t support multi-master or cross-version/cross-arch
replication. Which solution you should use depends on what you need to
do.

The major differences between physical replication and logical
replication as implemented by BDR are:

-   Multi-master replication is possible. All members are writable nodes
    that replicate changes.

-   Data from index writes, `VACUUM`{.LITERAL}, hint bits, etc are not
    sent over the network, so bandwidth requirements may be reduced -
    especially when compared to physical replication with
    `full_page_writes`{.LITERAL}.

-   There is no need to use
    [`hot_standby_feedback`{.LITERAL}](http://www.postgresql.org/docs/current/static/runtime-config-replication.html#GUC-HOT-STANDBY-FEEDBACK){target="_top"}
    or to cancel long running queries on hot standbys, so there aren\'t
    any [\"cancelling statement due to conflict with recovery\"]{.QUOTE}
    errors.

-   Temporary tables may be used on replicas.

-   Tables that aren\'t being replicated from elsewhere may be written
    to BDR.

-   Replication across major versions (e.g. 9.4 to 9.5) can be supported
    (though BDR imposes limitations on that,
    [pglogical](http://2ndquadrant.com/pglogical){target="_top"}
    supports it well).

-   Replication across architectures and OSes (e.g. PPC64 Linux to
    x86_64 OS X) is supported.

-   Replication is per-database (or even table-level), whereas physical
    replication can and must replicate all databases.
    ([pglogical](http://2ndquadrant.com/pglogical){target="_top"} even
    supports row- and column-level filtering of replication).

-   BDR\'s logical replication implementation imposes some restrictions
    on supported DDL (see: [DDL replication](ddl-replication.md)) that
    do not apply for physical replication

-   Because it\'s database-level not cluster-level, commands that affect
    all databases, like `ALTER SYSTEM`{.LITERAL} or
    `CREATE ROLE`{.LITERAL} are [*not*]{.emphasis} replicated by BDR and
    must be managed by the administrator.

-   Disk random I/O requirements and flush frequency may be higher than
    for physical replication.

-   Only completed transactions are replicated. Big transactions may
    have longer replication delays because replication doesn\'t start
    until the transaction completes. Aborted transactions\' writes are
    never replicated at all.

-   Logical replication requires at least PostgreSQL 9.4.

-   Logical replication cannot be used for point-in-time recovery
    (though it can support a replication delay). It\'s technically
    possible to add this capability if someone needs it, though.

-   Logical replication only works via streaming, not WAL file
    archiving, and requires the use of a [replication
    slot](http://www.postgresql.org/docs/current/static/logicaldecoding-explanation.html){target="_top"}.

-   Cascading replication is not (yet) supported by logical replication.

-   Large objects (pg_largeobject, lo_create, and so on) are not handled
    by logical decoding, so it cannot be replicated by BDR

-   Sequence updates are not replicated by logical replication, as the
    underlying logical decoding facility does not support them.
    Traditional sequences don\'t work in a multimaster environment
    anyway, so BDR offers alternatives.

Most users will want to use physical replication and/or WAL archiving
for redundancy, high availability, backup and PITR. Logical replication
is well suited to data integration, data movement and data aggregation
(often as an alternative to or combined with ETL), for scale-out and for
distributed multi-master deployments.

It\'s possible to replicate between different PostgreSQL versions,
operating systems and/or processor architectures using logical
replication because it can fall back to sending data in text form - just
like SQL. Where the servers are compatible it can use the
faster-to-process binary representation or an intermediate form. Logical
replication cannot prevent all possible incompatibilities though - for
example, it isn\'t possible to replicate a type added in PostgreSQL 9.5
to PostgreSQL 9.4 because 9.4 has no way to store and represent it.

Unlike physical replication, which replicates all databases on a
PostgreSQL install, logical decoding permits (and requires) separate
replication of each database. It can also replicate a subset of tables
within a database. It is not possible to configure wildcard replication
of all databases on a server in logical replication. You can replicate
multiple databases, but each database must be configured separately.

Temporary tables may always be created on all nodes, even if they are
also receiving replicated data. There\'s no prohibition against doing so
like it exists for PostgreSQL\'s block-level replication features.

Local writes are not limited to temporary tables. `UNLOGGED`{.LITERAL}
tables may be created even on nodes that are receiving changes from
upstream/peer nodes. Additionally, replication sets allow changes to
only a subset of tables to be replicated if desired, so some normal
tables may be excluded from replication. This makes BDR very useful for
use cases where significant work is done on nodes that also receive
replicated data from elsewhere.

Logical replication doesn\'t start replicating a transaction until it
commits. This can cause longer replication delays for big transactions
than physical replication, where the transaction\'s changes get
replicated as soon as they\'re written. It also lets logical replication
entirely skip replication of writes by aborted transactions. Future
enhancements to logical decoding may permit streaming of transactions
before they\'re committed.

Because logical replication is only supported in streaming mode (rather
than WAL archiving) it isn\'t suitable for point-in-time recovery.
Logical replication may be used in conjunction with streaming physical
replication and/or PITR, though; it is not necessary to choose one or
the other.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------ ------------------------------------ -------------------------------------------------------
  [Prev](bdr-concepts.md){accesskey="P"}    [Home](index.md){accesskey="H"}          [Next](bdr-vs-trigger-based.md){accesskey="N"}
  Concepts                                    [Up](overview.md){accesskey="U"}    Differences between BDR and trigger-based replication
  ------------------------------------------ ------------------------------------ -------------------------------------------------------
:::
