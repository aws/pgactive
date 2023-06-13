::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ---------------------------------------------------------------------------------- -------------------------------------------- ------------------------------ -------------------------------------------------------------------------------
  [Prev](replication-sets-concepts.md "Replication Set Concepts"){accesskey="P"}   [Up](replication-sets.md){accesskey="U"}    Chapter 11. Replication Sets    [Next](replication-sets-nodes.md "Node Replication Control"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [11.2. Creating replication sets]{#REPLICATION-SETS-CREATION} {#creating-replication-sets .SECT1}

Replication sets are not created or dropped explicitly. Rather, a
replication set exists if it has one or more tables assigned to it or
one or more connections consuming it. The `default`{.LITERAL}
replication set always exists, and contains all tables that have not
been explicitly assigned to another replication set. Adding a table to
some non-default replication set [*removes it from the
`default`{.LITERAL} replication set*]{.emphasis} unless you also
explicitly name the `default`{.LITERAL} replication set in its set
memberships.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------------------- -------------------------------------------- ----------------------------------------------------
  [Prev](replication-sets-concepts.md){accesskey="P"}        [Home](index.md){accesskey="H"}         [Next](replication-sets-nodes.md){accesskey="N"}
  Replication Set Concepts                                 [Up](replication-sets.md){accesskey="U"}                              Node Replication Control
  ------------------------------------------------------- -------------------------------------------- ----------------------------------------------------
:::
