  [BDR 2.0.7 Documentation](README.md)                                                                                                                           
  [Prev](replication-sets-creation.md "Creating replication sets")   [Up](replication-sets.md)    Chapter 11. Replication Sets    [Next](replication-sets-tables.md "Table Replication Control")  


# 11.3. Node Replication Control

The names of the replications sets of which changes should be received
can be set when adding the node to the [BDR] cluster using
the (optional) `replication_sets` parameter to
[bdr.bdr_create_group](functions-node-mgmt.md#FUNCTION-BDR-CREATE-GROUP),
[bdr.bdr_join_group](functions-node-mgmt.md#FUNCTION-BDR-JOIN-GROUP).
This parameter is an array of replication set names. The replication set
do not have to exist to be referenced by `replication_sets`.

To change one node\'s replication sets in a running [BDR]
cluster, the
[bdr.bdr_set_connection_replication_sets](functions-replication-sets.md#FUNCTION-BDR-SET-CONNECTION-REPLICATION-SETS)
functions should be used. Changes only need to be made on one node,
since BDR connection configuration is its self replicated to all other
nodes. The global DDL lock is not taken by this operation.

Changes to replication set memberships will generally take effect after
the transaction currently being processed by a node. To force the change
to take effect immediately it is safe to
`pg_terminate_backend(...)` the BDR apply workers running on
all nodes. They will reconnect and resume work on the last transaction
they were working on, with the new replication set configuration
enabled.



  ------------------------------------------------------- -------------------------------------------- -----------------------------------------------------
  [Prev](replication-sets-creation.md)        [Home](README.md)         [Next](replication-sets-tables.md)  
  Creating replication sets                                [Up](replication-sets.md)                              Table Replication Control
  ------------------------------------------------------- -------------------------------------------- -----------------------------------------------------
