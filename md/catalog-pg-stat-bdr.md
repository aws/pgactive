  [BDR 2.0.7 Documentation](README.md)                                                                                                                 
  [Prev](catalog-bdr-node-slots.md "bdr.bdr_node_slots")   [Up](catalogs-views.md)    Chapter 13. Catalogs and Views    [Next](catalog-bdr-conflict-history.md "bdr.bdr_conflict_history")  


# [13.4. bdr.pg_stat_bdr]

Performance and conflict statistics are maintained for each node by
[BDR] in the `bdr.pg_stat_bdr` table. This table
is [*not replicated*] between nodes, so each node has
separate stats. Each row represents the [BDR] apply
statistics for a different peer node.

An example listing from this table might look like:

``` PROGRAMLISTING
   SELECT * FROM bdr.pg_stat_bdr;
    rep_node_id | rilocalid |               riremoteid               | nr_commit | nr_rollback | nr_insert | nr_insert_conflict | nr_update | nr_update_conflict | nr_delete | nr_delete_conflict | nr_disconnect
   -------------+-----------+----------------------------------------+-----------+-------------+-----------+--------------------+-----------+--------------------+-----------+--------------------+---------------
              1 |         1 | bdr_6127682459268878512_1_16386_16386_ |         4 |           0 |         6 |                  0 |         1 |                  0 |         0 |                  3 |             0
              2 |         2 | bdr_6127682494973391064_1_16386_16386_ |         1 |           0 |         0 |                  0 |         1 |                  0 |         0 |                  0 |             0
   (2 rows)
   
```


**Table 13-4. `bdr.pg_stat_bdr` Columns**

  Name                                 Type              References                              Description
  ------------------------------------ ----------------- --------------------------------------- ---------------------------------------------------------------
  `rep_node_id`          `oid`      `bdr.pg_stat_get_bdr()`   The replication identifier for the connection with peer node.
  `relocalid`            `oid`      `bdr.pg_stat_get_bdr()`   The replication identifier for the connection with peer node.
  `riremoteid`           `text`     `bdr.pg_stat_get_bdr()`   A unique, cluster-wide identifier for the replication slot.
  `nr_commit`            `bigint`   `bdr.pg_stat_get_bdr()`   Number of commits on the peer node.
  `nr_rollback`          `bigint`   `bdr.pg_stat_get_bdr()`   Number of rollbacks on the peer node.
  `nr_insert`            `bigint`   `bdr.pg_stat_get_bdr()`   Number of inserts on the peer node.
  `nr_insert_conflict`   `bigint`   `bdr.pg_stat_get_bdr()`   Number of conflicts occured during inserts on the peer node.
  `nr_update`            `bigint`   `bdr.pg_stat_get_bdr()`   Number of updates on the peer node.
  `nr_update_conflict`   `bigint`   `bdr.pg_stat_get_bdr()`   Number of conflicts occured during updates on the peer node.
  `nr_delete`            `bigint`   `bdr.pg_stat_get_bdr()`   Number of deletes on the peer node.
  `nr_delete_conflict`   `bigint`   `bdr.pg_stat_get_bdr()`   Number of conflicts occured during deletes on the peer node.
  `nr_disconnect`        `bigint`   `bdr.pg_stat_get_bdr()`    



  ---------------------------------------------------- ------------------------------------------ ----------------------------------------------------------
  [Prev](catalog-bdr-node-slots.md)       [Home](README.md)        [Next](catalog-bdr-conflict-history.md)  
  bdr.bdr_node_slots                                    [Up](catalogs-views.md)                                    bdr.bdr_conflict_history
  ---------------------------------------------------- ------------------------------------------ ----------------------------------------------------------
