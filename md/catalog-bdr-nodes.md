  [BDR 2.0.7 Documentation](README.md)                                                                                                         
  [Prev](catalogs-views.md "Catalogs and Views")   [Up](catalogs-views.md)    Chapter 13. Catalogs and Views    [Next](catalog-bdr-connections.md "bdr.bdr_connections")  


# 13.1. bdr.bdr_nodes

The `bdr.bdr_nodes` table keeps track of a node\'s membership
in a [BDR] group. A row is inserted or updated in the
table during the node join process, and during node removal.

The \'status\' column may have the following values, which are subject
to change:

-   `r`- Ready: The node can function fully. Slots may be
    created on this node and it can participate with the
    [BDR] group.

-   `b`- Joining: The node is bootstrapping. This state occurs
    when join has been initiated but a copy has not yet been begun.

-   `i`- Joining: The node is doing initial slot creation or
    an initial dump and load

-   `c`- Joining: The node is catching up to the target node
    and is not yet ready to participate with the [BDR]
    group.

-   `o`- Joining: The node has caught up with the target node
    and is waiting for all inbound and outbound slots to be created.

-   `k`- Parting/Parted: The node has been \'killed\' or
    removed by the user with the function
    `bdr.bdr_part_by_node_names`.

Note that the status doesn\'t indicate whether the node is actually up
right now. A node may be shut down, isolated from the network, or
crashed and still appear as `r` in `bdr.bdr_nodes`
because it\'s still conceptually part of the BDR group. Check
[pg_stat_replication](http://www.postgresql.org/docs/current/static/monitoring-stats.html#PG-STAT-REPLICATION-VIEW)
and
[pg_replication_slots](http://www.postgresql.org/docs/current/static/catalog-pg-replication-slots.html)
for the connection and replay status of a node. See
[Monitoring](monitoring.md).

Avoid directly modifying `bdr.bdr_nodes`. Use the provided
node management functions instead. See [Node management
functions](functions-node-mgmt.md). It is safe to delete nodes entries
that have `node_state` `'k'` to re-use their node
names.


**Table 13-1. `bdr.bdr_nodes` Columns**

  Name                                 Type                References                                                                  Description
  `node_sysid`           `text`                                                                                   BDR generated node identifier from the BDR control file of the node
  `node_timeline`        `oid`                                                                                    timeline ID of this node
  `node_dboid`           `oid`                                                                                    local database oid on the cluster (node_sysid, node_timeline)
  `node_status`          `char`                                                                                   Readiness of the node: \[b\]eginning setup, \[i\]nitializing, \[c\]atchup, creating \[o\]utbound slots, \[r\]eady, \[k\]illed. Doesn\'t indicate connected/disconnected.
  `node_name`            `text`                                                                                   Name of the node
  `node_local_dsn`       `text`       `Node management function:``node_local_dsn`      A local loopback or unix socket connection string that the node can use to connect to its self; this is only used during initial setup to make the database restore faster.
  `node_init_from_dsn`   `text`       `Node management function:``node_external_dsn`   Connection string of the node chosen as join target. Not used after we\'ve joined the node.
  `node_read_only`       `boolean`                                                                                False unless read-only mode for a node is turned ON.
  `node_seq_id`          `smallint`                                                                                



  -------------------------------------------- ------------------------------------------ -----------------------------------------------------
  [Prev](catalogs-views.md)       [Home](README.md)        [Next](catalog-bdr-connections.md)  
  Catalogs and Views                            [Up](catalogs-views.md)                                    bdr.bdr_connections
  -------------------------------------------- ------------------------------------------ -----------------------------------------------------
