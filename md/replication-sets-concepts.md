::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                                         
  ----------------------------------------------------------------- -------------------------------------------- ------------------------------ -----------------------------------------------------------------------------------
  [Prev](replication-sets.md "Replication Sets"){accesskey="P"}   [Up](replication-sets.md){accesskey="U"}    Chapter 11. Replication Sets    [Next](replication-sets-creation.md "Creating replication sets"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [11.1. Replication Set Concepts]{#REPLICATION-SETS-CONCEPTS} {#replication-set-concepts .SECT1}

In a BDR group, each table belongs to one or more replication sets. The
replication sets `all`{.LITERAL} and `default`{.LITERAL} are created
implicitly by [BDR]{.PRODUCTNAME}. As the names suggest, all tables are
in replication set `all`{.LITERAL}, and every table is also initially in
the replication set `default`{.LITERAL}.

Replication set membership is its self replicated, so tables\'
membership in replication sets is the same on all nodes in a group. To
achieve different data replication to different nodes, some nodes\'
connections must be configured to receive non-default replication sets.

Each node\'s connection settings specify the replication sets that it
receives from its peers. These settings may be further overridden on a
node-to-node basis, specifying the replication sets one node should
receive from some other node.

This means that the changes actually received by a given node from some
other node are determined by the replication set memberships of the
table being changed and which replication sets the node the node
receiving the changes is a member of, unless overridden by more specific
replication sets specified for the connection from the receiving node to
the sending node. A change is received if the changed table is in at
least one replication set that the receiver is also accepting changes
from, i.e. the table replication sets intersect the connection
replication sets.

For example, if table A is in replication sets
`{'X','default'}`{.LITERAL} is modified on some node N0, some other node
N1 with replication sets `{'default'}`{.LITERAL} will receive the
changes. If some other node N2 has replication sets `{'X'}`{.LITERAL} it
will still receive the change because table A is still in replication
set `X`{.LITERAL}. If another node N3 has replication sets
`{'Y'}`{.LITERAL} it will [*not*]{.emphasis} receive the change, because
it isn\'t receiving either of the replication sets that table A is a
member of.

When a new [BDR]{.PRODUCTNAME} node is created or connected to the
[BDR]{.PRODUCTNAME} cluster, it defaults to replicating all changes in
the `default`{.LITERAL} replication set. This behaviour can be
overridden at node join time, or changed later via the replication set
control functions.

::: NOTE
> **Note:** Replication set membership does [*not*]{.emphasis} affect
> which tables\' schemas are synced, either at initial node join or on
> subsequent DDL replication operations. DDL on a table always affects
> all nodes, regardless of replication set membership. Similarly,
> replication set membership does not affect which tables\' content data
> are copied in an initial node join (though this may be subject to
> change in a future release).
:::

::: WARNING
  ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  **Warning**
  Currently `TRUNCATE`{.LITERAL} is [*always*]{.emphasis} replicated, even if a table is not a member of any active replication set. Use `DELETE FROM tablename;`{.LITERAL} if this is not desired.
  ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ---------------------------------------------- -------------------------------------------- -------------------------------------------------------
  [Prev](replication-sets.md){accesskey="P"}        [Home](index.md){accesskey="H"}         [Next](replication-sets-creation.md){accesskey="N"}
  Replication Sets                                [Up](replication-sets.md){accesskey="U"}                                Creating replication sets
  ---------------------------------------------- -------------------------------------------- -------------------------------------------------------
:::
