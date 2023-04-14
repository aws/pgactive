::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                
  ----------------------------------------------------- ------------------------------------ ------------------------- ----------------------------------------------------------------------------------------------------------------------------------
  [Prev](overview.md "BDR overview"){accesskey="P"}   [Up](overview.md){accesskey="U"}    Chapter 1. BDR overview    [Next](logical-vs-physical.md "Differences between logical (row level) and physical (block level) replication"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [1.1. Concepts]{#BDR-CONCEPTS} {#concepts .SECT1}

BDR provides loosely-coupled asynchronous multi-master logical
replication with mesh topology. This means that you can write to any
server and the changes will, after they have been committed, be sent
row-by-row to all the other servers that are part of the same BDR
group[[\[1\]]{.footnote}](#FTN.AEN57){#AEN57}.

More specifically:

::: VARIABLELIST

Multi-master

:   Each database (\"node\") participating in a BDR group both receives
    changes from other members and can be written to directly by the
    user. This is distinct from hot or warm standby, where only the
    single master server that all others replicate from can be written
    to.

    You don\'t have to write to all the masters, it\'s possible to leave
    all nodes except one read-only, or just direct writes to only one
    master. However, if you just want one-way replication look into
    [pglogical](http://2ndquadrant.com/pglogical), which
    is more flexible.

    If you\'re interested in BDR\'s multi-master features it\'s
    important to understand some of the concepts behind multi-master,
    both in general and as BDR implements it. Application developers
    [*must*]{.emphasis} be aware that applications may need changes for
    multi-master BDR and cannot be written exactly as if they are
    talking to a standalone or single-master system. See [BDR: Weakly
    coupled multi-master replication](weak-coupled-multimaster.md).

asynchronous

:   Changes made on one BDR node are not replicated to other nodes
    before they are committed locally. As a result the data is not
    exactly the same on all nodes at any given time; some nodes will
    have data that has not yet arrived at other nodes. PostgreSQL\'s
    block-based replication solutions default to asynchronous
    replication as well.

    Support for synchronous writes may be added to a future BDR release
    and [support for 1-safe synchronous replication is
    implemented.](node-management-synchronous.md)

    When combined with multi-master, asynchronous replication is often
    called an \"eventually consistent\" architecture. At any given time
    the data can look different when viewed from different nodes, but
    over time the nodes sync with each other. If writes stop then after
    a while all nodes will be the same. In BDR this means that [*foreign
    key constraints may be temporarily violated*]{.emphasis} as data
    replicates from multiple nodes.

loosely-coupled

:   Nodes in BDR are loosely-coupled because there is not much
    inter-node co-ordination traffic. There is no global transaction
    manager, no global lock manager, etc. Locks are node-local and
    there\'s no inter-node row or relation locking; the only inter-node
    locking is done for [schema changes](ddl-replication.md).

    This means nodes run more independently and are highly tolerant of
    network interruptions and partitions. But it also means that
    [replication conflicts](weak-coupled-multimaster.md) may occur.

logical

:   Logical (row-based) replication is replication using individual row
    values. It contrasts with physical (block-based) replication where
    changes to data blocks are sent. Logical replication is at a
    different level - it\'s a lot like the difference between sending a
    set of files and sending the hard drive the files are on. Logical
    replication has both advantages and disadvantages compared to
    physical replication; see [Differences between logical and physical
    replication](logical-vs-physical.md).

    The logical replication performed by BDR is low-level and requires
    that each table have exactly the same structure down to details like
    dropped columns. If you need a more flexible model for continuous
    ETL, OLAP data collection etc, look at
    [pglogical](http://2ndquadrant.com/pglogical).

replication

:   Replication is the process of copying data from one place to
    another. In BDR refers to the fact that BDR is not a shared-storage
    architecture; each node has its own copy of the database, including
    all relevant indexes etc. Nodes can satisfy queries without needing
    to communicate with other nodes, but must also have enough storage
    space to hold all the data in the database.

mesh topology

:   BDR is structured around a [mesh network](technotes-mesh.md) where
    every node connects to every other node and all nodes exchange data
    directly with each other. There is no forwarding.
:::

BDR is built on the [logical
decoding](http://www.postgresql.org/docs/current/static/logicaldecoding.html)
features developed by the BDR project and added to PostgreSQL. It also
relies on other core PostgreSQL features that were added with BDR in
mind, like background workers.

Because BDR needed some features that didn\'t make it into the
PostgreSQL 9.4 release, it needs a modified PostgreSQL 9.4 to run. See
[BDR requirements](install-requirements.md).
:::

### Notes {#notes .FOOTNOTES}

  ----------------------------------------------------------- -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  [[\[1\]]{.footnote}](bdr-concepts.md#AEN57){#FTN.AEN57}   We\'d say \"cluster\", but PostgreSQL has historically used that term for a different and confusing purpose, to mean a particular PostgreSQL instance\'s collection of databases.
  ----------------------------------------------------------- -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

::: NAVFOOTER

------------------------------------------------------------------------

  -------------------------------------- ------------------------------------ --------------------------------------------------------------------------------
  [Prev](overview.md){accesskey="P"}    [Home](index.md){accesskey="H"}                                    [Next](logical-vs-physical.md){accesskey="N"}
  BDR overview                            [Up](overview.md){accesskey="U"}    Differences between logical (row level) and physical (block level) replication
  -------------------------------------- ------------------------------------ --------------------------------------------------------------------------------
:::
