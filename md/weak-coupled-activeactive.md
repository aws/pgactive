  [BDR 2.1.0 Documentation](README.md)                                                                                                                                     
  [Prev](bdr-vs-trigger-based.md "Differences between BDR and trigger-based replication")   [Up](overview.md)    Chapter 1. BDR overview    [Next](installation.md "Installation")  


# 1.4. BDR: Weakly coupled Active-Active replication

When considering Active-Active clustering or replication (with BDR or
another technology) it is important to understand what\'s involved, and
that not all Active-Active systems are equal.

> **Note:** You don\'t have to use BDR for Active-Active. It\'s quite
> reasonable to write to only one node, using BDR like an improved
> read-replica system. It\'s also possible to make sure that any given
> table/schema is only written to on one particular node, so no
> conflicts can arise. You still have to consider replication lag, but
> no more or less than with normal hot standbys. It only gets
> complicated when your application writes to the same tables on
> multiple nodes at once. If you need to do that, keep reading.
>
> BDR supports marking nodes as read-only to make this easier, and nodes
> can be set up in synchronous pairs to reduce (but not eliminate)
> conflicts on failover.

Some Active-Active systems are [*tightly-coupled*]; these
tend to make all nodes appear to be part of the same virtual database to
outside clients, complete with cross-node locking, transaction
isolation, etc. They also often - but not always - use shared storage,
where each node connects to the same underlying database files over a
SAN or similar. This makes life easier for application developers
who\'re used to working with stand-alone or single-primary databases
because they can do everything just like they did before. Like with
anything there\'s a price, though: a tightly coupled Active-Active
system does not scale out very well, especially for writes, and isn\'t
very tolerant of latency, node outages, or network partitions.

Other systems are [*loosely-coupled*]. They don\'t attempt to
appear like a single seamless virtual database, and applications can see
some differences depending on which node they are connected to. Most
loosely coupled systems don\'t share storage; instead each node has a
copy of the whole database or a subset of it locally. If they store only
a subset of the data they may support routing queries to the correct
node, or they may expect the application to determine which node to find
data on. There is generally no global lock manager or transaction
manager, so transactions on one node aren\'t affected by locks taken on
other nodes. Many loosely coupled systems are asynchronous and
eventually consistent (see: [Concepts](bdr-concepts.md)) so changes on
one node aren\'t immediately visible on all other nodes at the same
time. This can make application development more difficult, but in
exchange makes the system very tolerant of latency between nodes,
temporary network partitions or node outages, etc, and makes scale-out
more efficient.

BDR is a loosely coupled shared-nothing Active-Active design.

This is a broad and overly simplified characterisation of replication,
but it\'s enough to explain why applications that use BDR for
Active-Active writes need to be aware of the anomalies that can be
introduced by asynchronous Active-Active replication. It should also
help illustrate that applications get some significant benefits in
exchange:

-   Applications using BDR are free to write to any node so long as they
    are careful to prevent or cope with conflicts.

-   There is no complex election of a new primary if a node goes down or
    network problems arise. There is no wait for failover. Each node is
    always a Primary and always directly writeable.

-   The application can be geographically distributed so that the app
    and is close to the data and the user for better performance and
    availability. Reads can be satisfied locally.

-   Applications can be partition-tolerant: the application can keep
    keep working even if it loses communication with some or all other
    nodes, then re-sync automatically when connectivity is restored.
    Loss of a critical VPN tunnel or WAN won\'t bring the entire store
    or satellite office to a halt.

With the advantages come challenges.

Because BDR replicates asynchronously, not all nodes have the same view
of the data at any given instant. On a single node it is guaranteed that
committed transactions\' changes become visible immediately to newly
started transactions (or in `READ COMMITTED` mode,
statements). This isn\'t true in BDR - if you `COMMIT` a
transaction that changes a row on one node, then `SELECT` that
row on another node, you may well still get the old value. Applications
must therefore be designed to be tolerant of stale data or to be
\"sticky\" to a node, where they prefer read data from the same node
they write it to. This is also true of applications using PostgreSQL\'s
physical replication feature unless it is used in synchronous mode with
only one replica, so it\'s a challenge that\'s far from unique to BDR.

Locking operations are not replicated to other nodes. If you lock a row
or table in one node the other nodes have no idea it is locked
elsewhere. Applications that rely on row or table locking for
correctness will only work correctly if all their writes and locked
reads occur on a single node. Applications might rely on locking
explicitly via `LOCK TABLE` or
`SELECT ... FOR UPDATE / SHARE`, but most applications rely on
it implicitly via `UPDATE` and `DELETE` row-locking,
so the absence of explicit locking does not mean an app is automatically
Active-Active safe.

Because of asynchronous replication and the lack of global locking, it
is possible for transactions on different nodes to perform actions that
could not happen if both transactions ran on a single node. These are
called [*conflicts*] and are discussed in detail separately;
see [Active-Active conflicts](conflicts.md). BDR can resolve conflicts
using a simple last-update-wins strategy or using user-defined conflict
handlers. Either way the application design needs to consider that
conflicts may occur, and where possible minimise them. Naïive
applications that ignore the the possibility of conflicts when writing
to multiple nodes may suffer from lost-updates and other undesirable
data anomalies.

BDR provides some tools to help make application design easier. The most
important is [Global sequences](global-sequences.md), which provide a
BDR-group-wide generator of unique values for use in synthetic keys.
Others are discussed in the [Active-Active conflicts](conflicts.md)
section.



  ------------------------------------------------------- ------------------------------------ ------------------------------------------
  [Prev](bdr-vs-trigger-based.md)         [Home](README.md)     [Next](installation.md)  
  Differences between BDR and trigger-based replication    [Up](overview.md)                                Installation
  ------------------------------------------------------- ------------------------------------ ------------------------------------------
