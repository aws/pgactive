  [BDR 2.0.7 Documentation](README.md)                                                                                                                       
  [Prev](replication-sets-nodes.md "Node Replication Control")   [Up](replication-sets.md)    Chapter 11. Replication Sets    [Next](replication-sets-changetype.md "Change-type replication sets")  


# 11.4. Table Replication Control

A newly created table is initially part of replication set
`default`. It is assigned to more or different sets by
[bdr.table_set_replication_sets](functions-replication-sets.md#FUNCTION-BDR-TABLE-SET-REPLICATION-SETS).
This operation aquires a DDL lock and can be used in a fully functional
[BDR] cluster with no down/missing members.

The array of sets a table is part of is retrieved by
[bdr.table_get_replication_sets](functions-replication-sets.md#FUNCTION-BDR-TABLE-GET-REPLICATION-SETS).

Adding a table to a replication set does [*not*] synchronize
the table\'s contents to nodes that were not previously receiving
changes for that table and will now do so. This means the table remains
inconsistent across nodes. It is generally necessary for the
administrator to manually synchronize the table after adding it to a
replication set. The simplest way to do this, albeit inefficiently and
only if there are no foreign keys references to the table, is to start a
transaction, copy the table\'s contents to a temp table, truncate the
original table, copy the table\'s contents back to the original table,
and commit. Alternately, the admin may use [psql]\'s
`\copy` with
[bdr.do_not_replicate](bdr-configuration-variables.md#GUC-BDR-DO-NOT-REPLICATE)
to (carefully!) sync the newly-replicated table\'s contents to the
receiving node, typically by joining two [psql] sessions
with a pipe. A future BDR release will add a built-in facility to
consistently resynchronize a table.

Removing a table from a replication set does not cause it to be emptied
on nodes that will no longer receive changes to it. On those nodes the
table just stops getting new changes, so it\'s frozen in time.

Table replication set membership changes take effect only for writes
performed after the set change. Any nodes still replaying old changes
due to replication lag/delay will continue to receive them with the
prior replication set memberships until they replay up to the point in
the logical change history where the replication set memberships
changed. This means you can\'t change a table\'s replication sets to get
a node to skip over a lot of write activity after the fact. (However, if
the table was already in some different replication set, you
[*can*] change which replication sets the node replays from
to skip that set, since node replication set memberships take immediate
effect).



  ---------------------------------------------------- -------------------------------------------- ---------------------------------------------------------
  [Prev](replication-sets-nodes.md)        [Home](README.md)         [Next](replication-sets-changetype.md)  
  Node Replication Control                              [Up](replication-sets.md)                               Change-type replication sets
  ---------------------------------------------------- -------------------------------------------- ---------------------------------------------------------
