::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ------------------------------------------------------------------------------- ---------------------------------- -- ----------------------------------------------------------------------------------
  [Prev](global-sequences-bdr10.md "BDR 1.0 global sequences"){accesskey="P"}   [Up](manual.md){accesskey="U"}        [Next](replication-sets-concepts.md "Replication Set Concepts"){accesskey="N"}

------------------------------------------------------------------------
:::

::: CHAPTER
# []{#REPLICATION-SETS}Chapter 11. Replication Sets

::: TOC
**Table of Contents**

11.1. [Replication Set Concepts](replication-sets-concepts.md)

11.2. [Creating replication sets](replication-sets-creation.md)

11.3. [Node Replication Control](replication-sets-nodes.md)

11.4. [Table Replication Control](replication-sets-tables.md)

11.5. [Change-type replication sets](replication-sets-changetype.md)
:::

Sometimes it is not desirable to replicate all changes that happen in a
database to all other nodes in a BDR group. E.g. it might not be
convenient and efficient to replicate a table containing session data.

In simple cases, application developers may simply use
`UNLOGGED`{.LITERAL} tables (a stock PostgreSQL feature). Since such
tables don\'t generate write-ahead-log (WAL) activity, they are
completely invisible to logical decoding and thus to BDR. No changes to
unlogged tables are replicated.

Where more complex arrangements are needed, replication sets are
available.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ---------------------------------------------------- ----------------------------------- -------------------------------------------------------
  [Prev](global-sequences-bdr10.md){accesskey="P"}    [Home](index.md){accesskey="H"}    [Next](replication-sets-concepts.md){accesskey="N"}
  BDR 1.0 global sequences                              [Up](manual.md){accesskey="U"}                                  Replication Set Concepts
  ---------------------------------------------------- ----------------------------------- -------------------------------------------------------
:::
