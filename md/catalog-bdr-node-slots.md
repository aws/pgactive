  [BDR 2.0.7 Documentation](README.md)                                                                                                                   
  [Prev](catalog-bdr-connections.md "bdr.bdr_connections")   [Up](catalogs-views.md)    Chapter 13. Catalogs and Views    [Next](catalog-pg-stat-bdr.md "bdr.pg_stat_bdr")  


# [13.3. bdr.bdr_node_slots]

The `bdr.bdr_node_slots` view provides a convenient way to see
which replication slots map to which nodes on a machine, as well as the
current activity of those replication slots.

It is a convenience join on `bdr.bdr_nodes`,
`pg_catalog.pg_stat_replication` and
`pg_replication_slots`, showing which nodes have which slots,
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

Note that `slot_restart_lsn` and
`slot_confirmed_lsn` are the `restart_lsn` and
`confirmed_flush_lsn` properties from
`pg_replication_slots`. The other lsn properties are from
`pg_stat_replication` and, along with the
`walsender_pid`, will be null if there\'s no currently active
replication connection for this slot.

If you want more detail from any of the joined tables, either modify the
underlying query obtained with
`SELECT pg_get_viewdef('bdr.bdr_node_slots')` or (preferably)
join on the table(s) of interest to add more columns, using the node
name, slot name, or pid as the key as appropriate.

For more on the use of this view, see [Monitoring](monitoring.md).


**Table 13-3. `bdr.bdr_nodes_slots` Columns**

  Name                                 Type               References                                                                     Description
  `node_name`            `text`      `bdr.bdr_nodes``.node_name`                         Name of peer node having a slot on current node.
  `slot_name`            `name`      `pg_replication_slots``.slot_name`                  A unique, cluster-wide identifier for the replication slot.
  `slot_restart_lsn`     `pg_lsn`    `pg_replication_slots``.restrt_lsn`                 The address (LSN) of oldest WAL which still might be required by the consumer of this slot and thus won\'t be automatically removed during checkpoints.
  `slot_confirmed_lsn`   `pg_lsn`    `pg_replication_slots``.slot_confirmed_flush_lsn`   The address (LSN) up to which the logical slot\'s consumer has confirmed receiving data. Data older than this is not available anymore. NULL for physical slots.
  `walsender_active`     `boolean`   `pg_replication_slots``.active`                     True if this slot is currently actively being used.
  `walsender_pid`        `integer`   `pg_replication_slots``.active_pid`                 The process ID of the session using this slot if the slot is currently actively being used. NULL if inactive.
  `sent_location`        `pg_lsn`    `bdr.pg_stat_replication``.sent_location`           Last transaction log position sent on this connection.
  `write_location`       `pg_lsn`    `bdr.pg_stat_replication``.write_location`          Last transaction log position written to disk by this standby server.
  `flush_location`       `pg_lsn`    `bdr.pg_stat_replication``.flush_location`          Last transaction log position flushed to disk by this standby server.
  `replay_location`      `pg_lsn`    `bdr.pg_stat_replication``.replay_location`         Last transaction log position replayed into the database on this standby server.



  ----------------------------------------------------- ------------------------------------------ -------------------------------------------------
  [Prev](catalog-bdr-connections.md)       [Home](README.md)        [Next](catalog-pg-stat-bdr.md)  
  bdr.bdr_connections                                    [Up](catalogs-views.md)                                    bdr.pg_stat_bdr
  ----------------------------------------------------- ------------------------------------------ -------------------------------------------------
