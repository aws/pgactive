::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                                                   
  --------------------------------------------------------------------------- ------------------------------------------ -------------------------------- -------------------------------------------------------------------
  [Prev](catalog-bdr-connections.md "bdr.bdr_connections"){accesskey="P"}   [Up](catalogs-views.md){accesskey="U"}    Chapter 13. Catalogs and Views    [Next](catalog-pg-stat-bdr.md "bdr.pg_stat_bdr"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [13.3. bdr.bdr_node_slots]{#CATALOG-BDR-NODE-SLOTS} {#bdr.bdr_node_slots .SECT1}

The `bdr.bdr_node_slots`{.LITERAL} view provides a convenient way to see
which replication slots map to which nodes on a machine, as well as the
current activity of those replication slots.

It is a convenience join on `bdr.bdr_nodes`{.LITERAL},
`pg_catalog.pg_stat_replication`{.LITERAL} and
`pg_replication_slots`{.LITERAL}, showing which nodes have which slots,
their current walsender activity and their replay positions.

The columns are all the same as the corresponding columns in the
originating tables. An example listing might look like:

``` PROGRAMLISTING
 node_name |                slot_name                | slot_restart_lsn | slot_confirmed_lsn | walsender_active | walsender_pid | sent_location | write_location | flush_location | replay_location 
-----------+-----------------------------------------+------------------+--------------------+------------------+---------------+---------------+----------------+----------------+-----------------
 nodeA     | bdr_16385_6313760193895071967_1_16385__ | 0/1A7E680        | 0/1A7E6B8          | t                |         12359 | 0/1A7E6B8     | 0/1A7E6B8      | 0/1A7E6B8      | 0/1A7E6B8
 nodeC     | bdr_16385_6313760468754622756_1_16385__ | 0/1A7E680        | 0/1A7E6B8          | t                |         12360 | 0/1A7E6B8     | 0/1A7E6B8      | 0/1A7E6B8      | 0/1A7E6B8
(2 rows)
   
```

Note that `slot_restart_lsn`{.LITERAL} and
`slot_confirmed_lsn`{.LITERAL} are the `restart_lsn`{.LITERAL} and
`confirmed_flush_lsn`{.LITERAL} properties from
`pg_replication_slots`{.LITERAL}. The other lsn properties are from
`pg_stat_replication`{.LITERAL} and, along with the
`walsender_pid`{.LITERAL}, will be null if there\'s no currently active
replication connection for this slot.

If you want more detail from any of the joined tables, either modify the
underlying query obtained with
`SELECT pg_get_viewdef('bdr.bdr_node_slots')`{.LITERAL} or (preferably)
join on the table(s) of interest to add more columns, using the node
name, slot name, or pid as the key as appropriate.

For more on the use of this view, see [Monitoring](monitoring.md).

::: TABLE
[]{#AEN3653}

**Table 13-3. `bdr.bdr_nodes_slots`{.STRUCTNAME} Columns**

  Name                                 Type               References                                                                     Description
  ------------------------------------ ------------------ ------------------------------------------------------------------------------ ------------------------------------------------------------------------------------------------------------------------------------------------------------------
  `node_name`{.STRUCTFIELD}            `text`{.TYPE}      `bdr.bdr_nodes`{.STRUCTNAME}`.node_name`{.STRUCTFIELD}                         Name of peer node having a slot on current node.
  `slot_name`{.STRUCTFIELD}            `name`{.TYPE}      `pg_replication_slots`{.STRUCTNAME}`.slot_name`{.STRUCTFIELD}                  A unique, cluster-wide identifier for the replication slot.
  `slot_restart_lsn`{.STRUCTFIELD}     `pg_lsn`{.TYPE}    `pg_replication_slots`{.STRUCTNAME}`.restrt_lsn`{.STRUCTFIELD}                 The address (LSN) of oldest WAL which still might be required by the consumer of this slot and thus won\'t be automatically removed during checkpoints.
  `slot_confirmed_lsn`{.STRUCTFIELD}   `pg_lsn`{.TYPE}    `pg_replication_slots`{.STRUCTNAME}`.slot_confirmed_flush_lsn`{.STRUCTFIELD}   The address (LSN) up to which the logical slot\'s consumer has confirmed receiving data. Data older than this is not available anymore. NULL for physical slots.
  `walsender_active`{.STRUCTFIELD}     `boolean`{.TYPE}   `pg_replication_slots`{.STRUCTNAME}`.active`{.STRUCTFIELD}                     True if this slot is currently actively being used.
  `walsender_pid`{.STRUCTFIELD}        `integer`{.TYPE}   `pg_replication_slots`{.STRUCTNAME}`.active_pid`{.STRUCTFIELD}                 The process ID of the session using this slot if the slot is currently actively being used. NULL if inactive.
  `sent_location`{.STRUCTFIELD}        `pg_lsn`{.TYPE}    `bdr.pg_stat_replication`{.STRUCTNAME}`.sent_location`{.STRUCTFIELD}           Last transaction log position sent on this connection.
  `write_location`{.STRUCTFIELD}       `pg_lsn`{.TYPE}    `bdr.pg_stat_replication`{.STRUCTNAME}`.write_location`{.STRUCTFIELD}          Last transaction log position written to disk by this standby server.
  `flush_location`{.STRUCTFIELD}       `pg_lsn`{.TYPE}    `bdr.pg_stat_replication`{.STRUCTNAME}`.flush_location`{.STRUCTFIELD}          Last transaction log position flushed to disk by this standby server.
  `replay_location`{.STRUCTFIELD}      `pg_lsn`{.TYPE}    `bdr.pg_stat_replication`{.STRUCTNAME}`.replay_location`{.STRUCTFIELD}         Last transaction log position replayed into the database on this standby server.
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ----------------------------------------------------- ------------------------------------------ -------------------------------------------------
  [Prev](catalog-bdr-connections.md){accesskey="P"}       [Home](index.md){accesskey="H"}        [Next](catalog-pg-stat-bdr.md){accesskey="N"}
  bdr.bdr_connections                                    [Up](catalogs-views.md){accesskey="U"}                                    bdr.pg_stat_bdr
  ----------------------------------------------------- ------------------------------------------ -------------------------------------------------
:::
