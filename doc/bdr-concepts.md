  [BDR 2.1.0 Documentation](README.md)                                                                                
  [Prev](overview.md "BDR overview")   [Up](overview.md)    Chapter 1. BDR overview    [Next](logical-vs-physical.md "Differences between logical (row level) and physical (block level) replication")  


# 1.1. Concepts

BDR provides loosely-coupled asynchronous Active-Active logical
replication with mesh topology. This means that you can write to any
server and the changes will, after they have been committed, be sent
row-by-row to all the other servers that are part of the same BDR
group[[\[1\]]{.footnote}](#FTN.AEN57){#AEN57}.

More specifically:


Active-Active

    Each database (\"node\") participating in a BDR group both receives
    changes from other members and can be written to directly by the
    user. This is distinct from hot or warm standby, where only the
    single primary server that all others replicate from can be written
    to.

    You don\'t have to write to all the Active-Active nodes, it\'s
    possible to leave all nodes except one read-only, or just direct
    writes to only one Active node. However, if you just want one-way
    replication look into
    [pglogical](https://github.com/2ndQuadrant/pglogical),
    which is more flexible.

    If you\'re interested in BDR\'s Active-Active features it\'s
    important to understand some of the concepts behind Active-Active,
    both in general and as BDR implements it. Application developers
    [*must*] be aware that applications may need changes for
    Active-Active BDR and cannot be written exactly as if they are
    talking to a standalone or single-primary system. See [BDR: Weakly
    coupled Active-Active replication](weak-coupled-activeactive.md).

asynchronous

    Changes made on one BDR node are not replicated to other nodes
    before they are committed locally. As a result the data is not
    exactly the same on all nodes at any given time; some nodes will
    have data that has not yet arrived at other nodes. PostgreSQL\'s
    block-based replication solutions default to asynchronous
    replication as well.

    Support for synchronous writes may be added to a future BDR release
    and [support for 1-safe synchronous replication is
    implemented.](node-management-synchronous.md)

    When combined with Active-Active, asynchronous replication is often
    called an \"eventually consistent\" architecture. At any given time
    the data can look different when viewed from different nodes, but
    over time the nodes sync with each other. If writes stop then after
    a while all nodes will be the same. In BDR this means that [*foreign
    key constraints may be temporarily violated*] as data
    replicates from multiple nodes.

loosely-coupled

    Nodes in BDR are loosely-coupled because there is not much
    inter-node co-ordination traffic. There is no global transaction
    manager, no global lock manager, etc. Locks are node-local and
    there\'s no inter-node row or relation locking; the only inter-node
    locking is done for [schema changes](ddl-replication.md).

    This means nodes run more independently and are highly tolerant of
    network interruptions and partitions. But it also means that
    [replication conflicts](weak-coupled-activeactive.md) may occur.

logical

    Logical (row-based) replication is replication using individual row
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
    [pglogical](https://github.com/2ndQuadrant/pglogical).

replication

    Replication is the process of copying data from one place to
    another. In BDR refers to the fact that BDR is not a shared-storage
    architecture; each node has its own copy of the database, including
    all relevant indexes etc. Nodes can satisfy queries without needing
    to communicate with other nodes, but must also have enough storage
    space to hold all the data in the database.

mesh topology

    BDR is structured around a [mesh network](technotes-mesh.md) where
    every node connects to every other node and all nodes exchange data
    directly with each other. There is no forwarding.

BDR is built on the [logical
decoding](http://www.postgresql.org/docs/current/static/logicaldecoding.html)
features developed by the BDR project and added to PostgreSQL. It also
relies on other core PostgreSQL features that were added with BDR in
mind, like background workers.

Because BDR needed some features that didn\'t make it into the
PostgreSQL 9.4 release, it needs a modified PostgreSQL 9.4 to run. See
[BDR requirements](install-requirements.md).

### Notes {#notes .FOOTNOTES}

  [[\[1\]]{.footnote}](bdr-concepts.md#AEN57){#FTN.AEN57}   We\'d say \"cluster\", but PostgreSQL has historically used that term for a different and confusing purpose, to mean a particular PostgreSQL instance\'s collection of databases.



  [Prev](overview.md)    [Home](README.md)                                    [Next](logical-vs-physical.md)  
  BDR overview                            [Up](overview.md)    Differences between logical (row level) and physical (block level) replication
