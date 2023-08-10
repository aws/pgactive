  [BDR 2.1.0 Documentation](README.md)                                                                                                         
  [Prev](replication-sets.md "Replication Sets")   [Up](replication-sets.md)    Chapter 11. Replication Sets    [Next](replication-sets-creation.md "Creating replication sets")  


# 11.1. Replication Set Concepts

In a BDR group, each table belongs to one or more replication sets. The
replication sets `all` and `default` are created
implicitly by [BDR]. As the names suggest, all tables are
in replication set `all`, and every table is also initially in
the replication set `default`.

User defined replication set membership is its self replicated (unless
`bdr.skip_ddl_replication` is set to on), so tables\'
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
`{'X','default'}` is modified on some node N0, some other node
N1 with replication sets `{'default'}` will receive the
changes. If some other node N2 has replication sets `{'X'}` it
will still receive the change because table A is still in replication
set `X`. If another node N3 has replication sets
`{'Y'}` it will [*not*] receive the change, because
it isn\'t receiving either of the replication sets that table A is a
member of.

When a new [BDR] node is created or connected to the
[BDR] cluster, it defaults to replicating all changes in
the `default` replication set. This behaviour can be
overridden at node join time, or changed later via the replication set
control functions.

> **Note:** Replication set membership does [*not*] affect
> which tables\' schemas are synced, either at initial node join or on
> subsequent DDL replication operations. DDL on a table always affects
> all nodes, regardless of replication set membership. Similarly,
> replication set membership does not affect which tables\' content data
> are copied in an initial node join (though this may be subject to
> change in a future release).

  **Warning**
  Currently `TRUNCATE` is [*always*] replicated, even if a table is not a member of any active replication set. Use `DELETE FROM tablename;` if this is not desired.



  ---------------------------------------------- -------------------------------------------- -------------------------------------------------------
  [Prev](replication-sets.md)        [Home](README.md)         [Next](replication-sets-creation.md)  
  Replication Sets                                [Up](replication-sets.md)                                Creating replication sets
  ---------------------------------------------- -------------------------------------------- -------------------------------------------------------
