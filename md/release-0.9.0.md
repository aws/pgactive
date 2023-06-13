::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-0.9.1.md "Release 0.9.1"){accesskey="P"}   [Up](releasenotes.md){accesskey="U"}    Appendix A. Release notes    [Next](release-0.8.0.md "Release 0.8.0"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [A.13. Release 0.9.0]{#RELEASE-0.9.0} {#a.13.-release-0.9.0 .SECT1}

The 0.9.0 release was tagged on Tue Mar 24 2015 as git tag
`bdr-plugin/0.9.0`{.LITERAL}. This release requires bdr-Pg
`bdr-pg/REL9_4_1-1`{.LITERAL} to support full BDR functionality.

Development of BDR 0.9.0 was performed by the
[2ndQuadrant](http://2ndquadrant.com) BDR team. Multiple
customers contributed funding and other resources to make this release
possible. 2ndQuadrant continues to fund the ongoing development of BDR
to meet internal needs and those of customers.

Significant features and improvements to [BDR]{.PRODUCTNAME} in this
release include:

-   Dynamic SQL-level configuration of connections between nodes

-   Joining new nodes no longer requires restarting all nodes

-   Easy node removal

-   [bdr_init_copy](command-bdr-init-copy.md) performs its own
    [pg_basebackup]{.APPLICATION}

-   Many helper and information functions

-   Documentation expanded and moved into the source tree

-   FreeBSD compatibility

::: SECT2
## [A.13.1. Dynamic configuration]{#RELEASE-0.9.0-DYNCONF} {#a.13.1.-dynamic-configuration .SECT2}

The biggest change with 0.9.0 is that connections between nodes are now
configured using the
[bdr.bdr_group_create](functions-node-mgmt.md#FUNCTION-BDR-GROUP-CREATE)
and
[bdr.bdr_group_join](functions-node-mgmt.md#FUNCTION-BDR-GROUP-JOIN)
SQL function calls. It is no longer necessary to restart any existing
nodes when joining a node. Even the newly joining node can join without
a restart if it was initially configured with the settings required for
running [BDR]{.PRODUCTNAME} (see [PostgreSQL settings for
BDR](settings-prerequisite.md)).

`bdr.connections`{.LITERAL} is now unused and ignored. If it remains in
`postgresql.conf`{.FILENAME} a warning will be issued in the PostgreSQL
log on startup.

For details, see the [Node management](node-management.md) chapter.
:::

::: SECT2
## [A.13.2. Easy node removal]{#RELEASE-0.9.0-NODE-REMOVAL} {#a.13.2.-easy-node-removal .SECT2}

The new
[bdr.bdr_part_by_node_names](functions-node-mgmt.md#FUNCTION-BDR-PART-BY-NODE-NAMES)
function allows easy online node removal. There is no need to restart
nodes or to manually delete replication identifiers and slots. Multiple
nodes may be removed at once.
:::

::: SECT2
## [A.13.3. [bdr_init_copy]{.APPLICATION} makes its own base backup]{#RELEASE-0.9.0-INIT-COPY} {#a.13.3.-bdr_init_copy-makes-its-own-base-backup .SECT2}

[bdr_init_copy]{.APPLICATION} can now make its own base backup of the
target node, and does so by default. Its user interface has also been
overhauled as part of the update to support dynamic configuration.
:::

::: SECT2
## [A.13.4. Documentation in the source tree]{#RELEASE-0.9.0-DOCS} {#a.13.4.-documentation-in-the-source-tree .SECT2}

Formal documentation has been written and added to the
[BDR]{.PRODUCTNAME} source tree to replace the prior ad-hoc wiki based
documentation. This allows users to more easily refer to documentation
specific to their particular version and permits the documentation to be
updated at the same time as the source code.
:::

::: SECT2
## [A.13.5. FreeBSD compatibility]{#RELEASE-0.9.0-FREEBSD} {#a.13.5.-freebsd-compatibility .SECT2}

[BDR]{.PRODUCTNAME} is now tested and built automatically on FreeBSD.
:::

::: SECT2
## [A.13.6. New helper functions]{#RELEASE-0.9.0-FUNCTIONS} {#a.13.6.-new-helper-functions .SECT2}

Many new helper functions have been added, see [SQL
functions](functions.md).
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-0.9.1.md){accesskey="P"}      [Home](index.md){accesskey="H"}       [Next](release-0.8.0.md){accesskey="N"}
  Release 0.9.1                                [Up](releasenotes.md){accesskey="U"}                                Release 0.8.0
  ------------------------------------------- ---------------------------------------- -------------------------------------------
:::
