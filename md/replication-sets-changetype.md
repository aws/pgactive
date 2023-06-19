  [BDR 2.0.7 Documentation](README.md)                                                                                                                         
  [Prev](replication-sets-tables.md "Table Replication Control")   [Up](replication-sets.md)    Chapter 11. Replication Sets    [Next](functions.md "Functions")  


# [11.5. Change-type replication sets]

In addition to table- and node-level replication set control, it\'s also
possible to configure which [*operations*] replication sets
replicate. A replication set can be configured to replicate only
`INSERT`s, for example. New rows inserted in the table will be
replicated, but `UPDATE`s of existing rows will not be, and
when a row is `DELETE`d the remote copies of the row won\'t be
deleted. Obviously this creates node-to-node inconsistencies, so it must
be used with extreme caution.

The main use of operation-level replication set control is maintaining
archive and DW nodes, where data removed from other nodes is retained on
the archive/DW node.

Operation-level replication set control is a low-level advanced feature
that doesn\'t yet have any management functions for it. To customise
which operations a replication set syncs, `INSERT` a row into
`bdr.bdr_replication_set_config`, like:

``` PROGRAMLISTING
    INSERT INTO bdr.bdr_replication_set_config(set_name, replicate_inserts, replicate_updates, replicate_deletes)
    VALUES ('set_name', 't', 't', 't');
   
```

Adjust the replication flags as desired for the intended replication set
function.

Like all replication set changes, changes to the operations replicated
by a replication set take effect only for new data changes; no
already-replicated rows will be retroactively changed.

  **Warning**
  Currently the `TRUNCATE` operation is [*always*] replicated, even if a table is not a member of any active replication set. Use `DELETE FROM tablename;` if this is not desired.



  ----------------------------------------------------- -------------------------------------------- ---------------------------------------
  [Prev](replication-sets-tables.md)        [Home](README.md)         [Next](functions.md)  
  Table Replication Control                              [Up](replication-sets.md)                                Functions
  ----------------------------------------------------- -------------------------------------------- ---------------------------------------
