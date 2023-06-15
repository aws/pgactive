::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ----------------------------------------------------------------- ------------------------------------- ----------------------------------- ----------------------------------------------------------------------------------------------
  [Prev](conflicts-types.md "Types of conflict"){accesskey="P"}   [Up](conflicts.md){accesskey="U"}    Chapter 9. Multi-master conflicts    [Next](conflicts-user-defined-handlers.md "User defined conflict handlers"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [9.3. Avoiding or tolerating conflicts]{#CONFLICTS-AVOIDANCE} {#avoiding-or-tolerating-conflicts .SECT1}

In most cases appropriate application design can be used to avoid
conflicts and/or the application can be made tolerant of conflicts.

Conflicts can only happen if there are things happening at the same time
on multiple nodes, so the simplest way to avoid conflicts is to only
ever write to one node, or to only ever write to independent subsets of
the database on each node. For example, each node might have a separate
schema, and while they all exchange data with each other, writes are
only ever performed on the node that \"owns\" a given schema.

For `INSERT`{.LITERAL} vs `INSERT`{.LITERAL} conflicts, use of [Global
sequences](global-sequences.md) can completely prevent conflicts.

BDR users may sometimes find it useful to perform distributed locking at
the application level in cases where conflicts are not acceptable.

The best course of action is frequently to allow conflicts to occur and
design the application to work with [BDR]{.PRODUCTNAME}\'s conflict
resolution mechansisms to cope with the conflict. See [Types of
conflict](conflicts-types.md).
:::

::: NAVFOOTER

------------------------------------------------------------------------

  --------------------------------------------- ------------------------------------- -------------------------------------------------------------
  [Prev](conflicts-types.md){accesskey="P"}     [Home](index.md){accesskey="H"}     [Next](conflicts-user-defined-handlers.md){accesskey="N"}
  Types of conflict                              [Up](conflicts.md){accesskey="U"}                                 User defined conflict handlers
  --------------------------------------------- ------------------------------------- -------------------------------------------------------------
:::
