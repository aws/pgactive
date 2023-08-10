  [BDR 2.1.0 Documentation](README.md)                                                                                                                      
  [Prev](node-management-disabling.md "Completely removing BDR")   [Up](node-management.md)    Chapter 5. Node Management    [Next](node-management-synchronous.md "n-safe synchronous replication")  


# 5.4. Rejoining a BDR node

It\'s possible to rejoin a BDR node that has already been detached and
locally removed by using the
[bdr.bdr_join_group](functions-node-mgmt.md#FUNCTION-BDR-JOIN-GROUP)
function.

If the node to rejoin does contain relations that already exist in the
other BDR group members then the rejoin would fail.

Typically, one wants to rejoin the same node to the same BDR group 1) To
avoid re-creating the whole database instance, 2) Associated replication
slots are dropped on the upstream node when the node goes down.



  ------------------------------------------------------- ------------------------------------------- ---------------------------------------------------------
  [Prev](node-management-disabling.md)        [Home](README.md)        [Next](node-management-synchronous.md)  
  Completely removing BDR                                  [Up](node-management.md)                             n-safe synchronous replication
  ------------------------------------------------------- ------------------------------------------- ---------------------------------------------------------
