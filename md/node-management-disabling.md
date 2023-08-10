  [BDR 2.1.0 Documentation](README.md)                                                                                                                       
  [Prev](node-management-removing.md "Detaching (removing) a node")   [Up](node-management.md)    Chapter 5. Node Management    [Next](node-management-rejoining.md "Rejoining a BDR node")


# 5.3. Completely removing BDR

To take a BDR node that has already been detached, or one that has been
restored from a base backup, and turn it back into a normal PostgreSQL
database you may use the
[bdr.bdr_remove](functions-node-mgmt.md#FUNCTION-BDR-REMOVE)
function.

After running `bdr.bdr_remove()` it is safe to
`DROP EXTENSION bdr;`. At this point all BDR-specific elements
will have been removed from the local database and it may be used as a
standalone database. Global sequences are converted into local sequences
and may be used normally. All BDR triggers, event triggers, security
labels, slots, replication identifiers etc are removed from the local
node.

Alternately, after `bdr.bdr_remove()`, it is
possible to
[bdr.bdr_create_group](functions-node-mgmt.md#FUNCTION-BDR-CREATE-GROUP)
a new BDR group with this database as the starting node. The new group
will be completely independent from the existing group.

+-----------------------------------------------------------------------+
| **Warning**                                                           |
+-----------------------------------------------------------------------+
| Note that local sequences are [*not*] converted back to    |
| global sequences when a new node group is created. If converted using |
|                                                                       |
| ``` PROGRAMLISTING                                                    |
|     ALTER SEQUENCE ... USING bdr;                                     |
|                                                                       |
| ```                                                                   |
|                                                                       |
| the sequence will [*not*] restart at the old local         |
| sequence startpoint. Nor can you use `setval(...)` to      |
| advance it. It is currently necessary to use a script to call         |
| `nextval()` repeatedly until the desired value is reached. |
| (See github #215).                                                    |
+-----------------------------------------------------------------------+

If BDR thinks it\'s still joined with an existing node group then
`bdr.bdr_remove()` will refuse to run as a
safety measure to prevent inconsistently removing a running node.

If you are sure the node has really been detached from its group or is a
duplicate copy of a node that\'s still running normally, you may force
removal by calling `bdr.bdr_remove(true)`. Do
[*not*] do so unless you\'re certain the node you\'re running
it on is already isolated from the group - say, if it\'s been detached
while disconnected, or has been restored from a PITR backup or disk
snapshot. Otherwise you will leave dangling replication slots etc on the
other nodes, causing problems on the remaining nodes. Always
[bdr.bdr_detach_nodes](functions-node-mgmt.md#FUNCTION-BDR-DETACH-NODES)
the node first.



  ------------------------------------------------------ ------------------------------------------- -------------------------------------------------------
  [Prev](node-management-removing.md)        [Home](README.md)        [Next](node-management-rejoining.md)  
  Detaching (removing) a node                               [Up](node-management.md)                                     Rejoining a BDR node
  ------------------------------------------------------ ------------------------------------------- -------------------------------------------------------
