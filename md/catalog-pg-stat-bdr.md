::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                                                 
  ------------------------------------------------------------------------- ------------------------------------------ -------------------------------- -------------------------------------------------------------------------------------
  [Prev](catalog-bdr-node-slots.md "bdr.bdr_node_slots"){accesskey="P"}   [Up](catalogs-views.md){accesskey="U"}    Chapter 13. Catalogs and Views    [Next](catalog-bdr-conflict-history.md "bdr.bdr_conflict_history"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [13.4. bdr.pg_stat_bdr]{#CATALOG-PG-STAT-BDR} {#bdr.pg_stat_bdr .SECT1}

Performance and conflict statistics are maintained for each node by
[BDR]{.PRODUCTNAME} in the `bdr.pg_stat_bdr`{.LITERAL} table. This table
is [*not replicated*]{.emphasis} between nodes, so each node has
separate stats. Each row represents the [BDR]{.PRODUCTNAME} apply
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

::: TABLE
[]{#AEN3763}

**Table 13-4. `bdr.pg_stat_bdr`{.STRUCTNAME} Columns**

  Name                                 Type              References                              Description
  ------------------------------------ ----------------- --------------------------------------- ---------------------------------------------------------------
  `rep_node_id`{.STRUCTFIELD}          `oid`{.TYPE}      `bdr.pg_stat_get_bdr()`{.STRUCTFIELD}   The replication identifier for the connection with peer node.
  `relocalid`{.STRUCTFIELD}            `oid`{.TYPE}      `bdr.pg_stat_get_bdr()`{.STRUCTFIELD}   The replication identifier for the connection with peer node.
  `riremoteid`{.STRUCTFIELD}           `text`{.TYPE}     `bdr.pg_stat_get_bdr()`{.STRUCTFIELD}   A unique, cluster-wide identifier for the replication slot.
  `nr_commit`{.STRUCTFIELD}            `bigint`{.TYPE}   `bdr.pg_stat_get_bdr()`{.STRUCTFIELD}   Number of commits on the peer node.
  `nr_rollback`{.STRUCTFIELD}          `bigint`{.TYPE}   `bdr.pg_stat_get_bdr()`{.STRUCTFIELD}   Number of rollbacks on the peer node.
  `nr_insert`{.STRUCTFIELD}            `bigint`{.TYPE}   `bdr.pg_stat_get_bdr()`{.STRUCTFIELD}   Number of inserts on the peer node.
  `nr_insert_conflict`{.STRUCTFIELD}   `bigint`{.TYPE}   `bdr.pg_stat_get_bdr()`{.STRUCTFIELD}   Number of conflicts occured during inserts on the peer node.
  `nr_update`{.STRUCTFIELD}            `bigint`{.TYPE}   `bdr.pg_stat_get_bdr()`{.STRUCTFIELD}   Number of updates on the peer node.
  `nr_update_conflict`{.STRUCTFIELD}   `bigint`{.TYPE}   `bdr.pg_stat_get_bdr()`{.STRUCTFIELD}   Number of conflicts occured during updates on the peer node.
  `nr_delete`{.STRUCTFIELD}            `bigint`{.TYPE}   `bdr.pg_stat_get_bdr()`{.STRUCTFIELD}   Number of deletes on the peer node.
  `nr_delete_conflict`{.STRUCTFIELD}   `bigint`{.TYPE}   `bdr.pg_stat_get_bdr()`{.STRUCTFIELD}   Number of conflicts occured during deletes on the peer node.
  `nr_disconnect`{.STRUCTFIELD}        `bigint`{.TYPE}   `bdr.pg_stat_get_bdr()`{.STRUCTFIELD}    
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ---------------------------------------------------- ------------------------------------------ ----------------------------------------------------------
  [Prev](catalog-bdr-node-slots.md){accesskey="P"}       [Home](index.md){accesskey="H"}        [Next](catalog-bdr-conflict-history.md){accesskey="N"}
  bdr.bdr_node_slots                                    [Up](catalogs-views.md){accesskey="U"}                                    bdr.bdr_conflict_history
  ---------------------------------------------------- ------------------------------------------ ----------------------------------------------------------
:::
