::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                                                       
  ---------------------------------------------------------------------------------- ------------------------------------------- ---------------------------- ------------------------------------------------------------------------------------------
  [Prev](node-management-removing.md "Parting (removing) a node"){accesskey="P"}   [Up](node-management.md){accesskey="U"}    Chapter 5. Node Management    [Next](node-management-synchronous.md "n-safe synchronous replication"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [5.3. Completely removing BDR]{#NODE-MANAGEMENT-DISABLING} {#completely-removing-bdr .SECT1}

To take a BDR node that has already been parted, or one that has been
restored from a base backup, and turn it back into a normal PostgreSQL
database you may use the
[bdr.remove_bdr_from_local_node](functions-node-mgmt.md#FUNCTION-BDR-REMOVE-BDR-FROM-LOCAL-NODE)
function.

After running `bdr.remove_bdr_from_local_node()`{.LITERAL} it is safe to
`DROP EXTENSION bdr;`{.LITERAL}. At this point all BDR-specific elements
will have been removed from the local database and it may be used as a
standalone database. Global sequences are converted into local sequences
and may be used normally. All BDR triggers, event triggers, security
labels, slots, replication identifiers etc are removed from the local
node.

Alternately, after `bdr.remove_bdr_from_local_node()`{.LITERAL}, it is
possible to
[bdr.bdr_group_create](functions-node-mgmt.md#FUNCTION-BDR-GROUP-CREATE)
a new BDR group with this database as the starting node. The new group
will be completely independent from the existing group.

::: WARNING
+-----------------------------------------------------------------------+
| **Warning**                                                           |
+-----------------------------------------------------------------------+
| Note that local sequences are [*not*]{.emphasis} converted back to    |
| global sequences when a new node group is created. If converted using |
|                                                                       |
| ``` PROGRAMLISTING                                                    |
|     ALTER SEQUENCE ... USING bdr;                                     |
|                                                                       |
| ```                                                                   |
|                                                                       |
| the sequence will [*not*]{.emphasis} restart at the old local         |
| sequence startpoint. Nor can you use `setval(...)`{.FUNCTION} to      |
| advance it. It is currently necessary to use a script to call         |
| `nextval()`{.FUNCTION} repeatedly until the desired value is reached. |
| (See github #215).                                                    |
+-----------------------------------------------------------------------+
:::

If BDR thinks it\'s still joined with an existing node group then
`bdr.remove_bdr_from_local_node()`{.LITERAL} will refuse to run as a
safety measure to prevent inconsistently removing a running node.

If you are sure the node has really been parted from its group or is a
duplicate copy of a node that\'s still running normally, you may force
removal by calling `bdr.remove_bdr_from_local_node(true)`{.LITERAL}. Do
[*not*]{.emphasis} do so unless you\'re certain the node you\'re running
it on is already isolated from the group - say, if it\'s been parted
while disconnected, or has been restored from a PITR backup or disk
snapshot. Otherwise you will leave dangling replication slots etc on the
other nodes, causing problems on the remaining nodes. Always
[bdr.bdr_part_by_node_names](functions-node-mgmt.md#FUNCTION-BDR-PART-BY-NODE-NAMES)
the node first.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------------------ ------------------------------------------- ---------------------------------------------------------
  [Prev](node-management-removing.md){accesskey="P"}        [Home](index.md){accesskey="H"}        [Next](node-management-synchronous.md){accesskey="N"}
  Parting (removing) a node                               [Up](node-management.md){accesskey="U"}                             n-safe synchronous replication
  ------------------------------------------------------ ------------------------------------------- ---------------------------------------------------------
:::
