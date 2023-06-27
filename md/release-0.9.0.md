  [BDR 2.0.7 Documentation](README.md)                                                                                            
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-0.9.1.md "Release 0.9.1")   [Up](releasenotes.md)    Appendix A. Release notes    [Next](release-0.8.0.md "Release 0.8.0")  


# A.13. Release 0.9.0

The 0.9.0 release was tagged on Tue Mar 24 2015 as git tag
`bdr-plugin/0.9.0`. This release requires bdr-Pg
`bdr-pg/REL9_4_1-1` to support full BDR functionality.

Development of BDR 0.9.0 was performed by the
[2ndQuadrant](http://2ndquadrant.com) BDR team. Multiple
customers contributed funding and other resources to make this release
possible. 2ndQuadrant continues to fund the ongoing development of BDR
to meet internal needs and those of customers.

Significant features and improvements to [BDR] in this
release include:

-   Dynamic SQL-level configuration of connections between nodes

-   Joining new nodes no longer requires restarting all nodes

-   Easy node removal

-   [bdr_init_copy](command-bdr-init-copy.md) performs its own
    [pg_basebackup]

-   Many helper and information functions

-   Documentation expanded and moved into the source tree

-   FreeBSD compatibility

## A.13.1. Dynamic configuration

The biggest change with 0.9.0 is that connections between nodes are now
configured using the
[bdr.bdr_group_create](functions-node-mgmt.md#FUNCTION-BDR-GROUP-CREATE)
and
[bdr.bdr_group_join](functions-node-mgmt.md#FUNCTION-BDR-GROUP-JOIN)
SQL function calls. It is no longer necessary to restart any existing
nodes when joining a node. Even the newly joining node can join without
a restart if it was initially configured with the settings required for
running [BDR] (see [PostgreSQL settings for
BDR](settings-prerequisite.md)).

`bdr.connections` is now unused and ignored. If it remains in
`postgresql.conf` a warning will be issued in the PostgreSQL
log on startup.

For details, see the [Node management](node-management.md) chapter.

## A.13.2. Easy node removal

The new
[bdr.bdr_detach_by_node_names](functions-node-mgmt.md#FUNCTION-BDR-DETACH-BY-NODE-NAMES)
function allows easy online node removal. There is no need to restart
nodes or to manually delete replication identifiers and slots. Multiple
nodes may be removed at once.

## A.13.3. [bdr_init_copy] makes its own base backup

[bdr_init_copy] can now make its own base backup of the
target node, and does so by default. Its user interface has also been
overhauled as part of the update to support dynamic configuration.

## A.13.4. Documentation in the source tree

Formal documentation has been written and added to the
[BDR] source tree to replace the prior ad-hoc wiki based
documentation. This allows users to more easily refer to documentation
specific to their particular version and permits the documentation to be
updated at the same time as the source code.

## A.13.5. FreeBSD compatibility

[BDR] is now tested and built automatically on FreeBSD.

## A.13.6. New helper functions

Many new helper functions have been added, see [SQL
functions](functions.md).



  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-0.9.1.md)      [Home](README.md)       [Next](release-0.8.0.md)  
  Release 0.9.1                                [Up](releasenotes.md)                                Release 0.8.0
  ------------------------------------------- ---------------------------------------- -------------------------------------------
