::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ----------------------------------------------------------------------------------- -------------------------------------------- ------------------------------ ---------------------------------------------------------------------------------
  [Prev](replication-sets-creation.md "Creating replication sets"){accesskey="P"}   [Up](replication-sets.md){accesskey="U"}    Chapter 11. Replication Sets    [Next](replication-sets-tables.md "Table Replication Control"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [11.3. Node Replication Control]{#REPLICATION-SETS-NODES} {#node-replication-control .SECT1}

The names of the replications sets of which changes should be received
can be set when adding the node to the [BDR]{.PRODUCTNAME} cluster using
the (optional) `replication_sets`{.LITERAL} parameter to
[bdr.bdr_group_create](functions-node-mgmt.md#FUNCTION-BDR-GROUP-CREATE),
[bdr.bdr_group_join](functions-node-mgmt.md#FUNCTION-BDR-GROUP-JOIN)
and
[bdr.bdr_subscribe](functions-node-mgmt.md#FUNCTIONS-NODE-MGMT-SUBSCRIBE).
This parameter is an array of replication set names. The replication set
do not have to exist to be referenced by `replication_sets`{.LITERAL}.

To change one node\'s replication sets in a running [BDR]{.PRODUCTNAME}
cluster, the
[bdr.connection_set_replication_sets](functions-replication-sets.md#FUNCTION-BDR-CONNECTION-SET-REP-SETS-BYNAME)
functions should be used. Changes only need to be made on one node,
since BDR connection configuration is its self replicated to all other
nodes. The global DDL lock is not taken by this operation.

Changes to replication set memberships will generally take effect after
the transaction currently being processed by a node. To force the change
to take effect immediately it is safe to
`pg_terminate_backend(...)`{.FUNCTION} the BDR apply workers running on
all nodes. They will reconnect and resume work on the last transaction
they were working on, with the new replication set configuration
enabled.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------------------- -------------------------------------------- -----------------------------------------------------
  [Prev](replication-sets-creation.md){accesskey="P"}        [Home](index.md){accesskey="H"}         [Next](replication-sets-tables.md){accesskey="N"}
  Creating replication sets                                [Up](replication-sets.md){accesskey="U"}                              Table Replication Control
  ------------------------------------------------------- -------------------------------------------- -----------------------------------------------------
:::
