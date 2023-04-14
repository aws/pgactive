::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                         
  --------------------------------------------------------- ------------------------------------- ----------------------------- -------------------------------------------------------------------------
  [Prev](technotes.md "Technical notes"){accesskey="P"}   [Up](technotes.md){accesskey="U"}    Appendix C. Technical notes    [Next](technotes-ddl-locking.md "DDL locking details"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [C.1. BDR network structure]{#TECHNOTES-MESH} {#c.1.-bdr-network-structure .SECT1}

BDR uses a mesh topology, where every node can communicate directly with
every other node. It doesn\'t support circular replication, forwarding,
cascading, etc.

Each pair of nodes communicates over a pair of (mostly) uni-directional
channels, one to stream data from node A=\>B and one to stream data from
node B=\>A. This means each node must be able to connect directly to
each other node. Firewalls, NAT, etc must be configured accordingly.

Every BDR node must have a [replication
slot](https://www.postgresql.org/docs/current/static/logicaldecoding-explanation.html)
on every other BDR node so it can replay changes from the node, and
every node must have a replication origin for each other node so it can
keep track of replay progress. If nodes were allowed to join while
another was offline or unreachable due to a network partition, it would
have no way to replay any changes made on that node and the BDR group
would get out of sync. Since bdr does no change forwarding during normal
operation, that desynchronisation would not get fixed.

The addition of enhanced change forwarding support could allow for
cascading nodes isolated from the rest of the mesh, allow new nodes to
join and lazily switch over to directly receiving data from a node when
it becomes reachable, etc. It\'s not fundamentally necessary for all
nodes to be reachable during node join, it\'s just a requirement for the
current implementation. There\'s already limited change forwarding
support in place and used for initial node clone.

DDL locking enhancements would also be required; see [DDL
replication](ddl-replication.md) and [DDL locking
details](technotes-ddl-locking.md).
:::

::: NAVFOOTER

------------------------------------------------------------------------

  --------------------------------------- ------------------------------------- ---------------------------------------------------
  [Prev](technotes.md){accesskey="P"}     [Home](index.md){accesskey="H"}     [Next](technotes-ddl-locking.md){accesskey="N"}
  Technical notes                          [Up](technotes.md){accesskey="U"}                                  DDL locking details
  --------------------------------------- ------------------------------------- ---------------------------------------------------
:::
