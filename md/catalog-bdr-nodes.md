::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ----------------------------------------------------------------- ------------------------------------------ -------------------------------- ---------------------------------------------------------------------------
  [Prev](catalogs-views.md "Catalogs and Views"){accesskey="P"}   [Up](catalogs-views.md){accesskey="U"}    Chapter 13. Catalogs and Views    [Next](catalog-bdr-connections.md "bdr.bdr_connections"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [13.1. bdr.bdr_nodes]{#CATALOG-BDR-NODES} {#bdr.bdr_nodes .SECT1}

The `bdr.bdr_nodes`{.LITERAL} table keeps track of a node\'s membership
in a [BDR]{.PRODUCTNAME} group. A row is inserted or updated in the
table during the node join process, and during node removal.

The \'status\' column may have the following values, which are subject
to change:

-   `r`{.LITERAL}- Ready: The node can function fully. Slots may be
    created on this node and it can participate with the
    [BDR]{.PRODUCTNAME} group.

-   `b`{.LITERAL}- Joining: The node is bootstrapping. This state occurs
    when join has been initiated but a copy has not yet been begun.

-   `i`{.LITERAL}- Joining: The node is doing initial slot creation or
    an initial dump and load

-   `c`{.LITERAL}- Joining: The node is catching up to the target node
    and is not yet ready to participate with the [BDR]{.PRODUCTNAME}
    group.

-   `o`{.LITERAL}- Joining: The node has caught up with the target node
    and is waiting for all inbound and outbound slots to be created.

-   `k`{.LITERAL}- Parting/Parted: The node has been \'killed\' or
    removed by the user with the function
    `bdr.bdr_part_by_node_names`{.LITERAL}.

Note that the status doesn\'t indicate whether the node is actually up
right now. A node may be shut down, isolated from the network, or
crashed and still appear as `r`{.LITERAL} in `bdr.bdr_nodes`{.LITERAL}
because it\'s still conceptually part of the BDR group. Check
[pg_stat_replication](http://www.postgresql.org/docs/current/static/monitoring-stats.html#PG-STAT-REPLICATION-VIEW){target="_top"}
and
[pg_replication_slots](http://www.postgresql.org/docs/current/static/catalog-pg-replication-slots.html){target="_top"}
for the connection and replay status of a node. See
[Monitoring](monitoring.md).

Avoid directly modifying `bdr.bdr_nodes`{.LITERAL}. Use the provided
node management functions instead. See [Node management
functions](functions-node-mgmt.md). It is safe to delete nodes entries
that have `node_state`{.LITERAL} `'k'`{.LITERAL} to re-use their node
names.

::: TABLE
[]{#AEN3419}

**Table 13-1. `bdr.bdr_nodes`{.STRUCTNAME} Columns**

  Name                                 Type                References                                                                  Description
  ------------------------------------ ------------------- --------------------------------------------------------------------------- -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  `node_sysid`{.STRUCTFIELD}           `text`{.TYPE}                                                                                   system_identifier from the control file of the node
  `node_timeline`{.STRUCTFIELD}        `oid`{.TYPE}                                                                                    timeline ID of this node
  `node_dboid`{.STRUCTFIELD}           `oid`{.TYPE}                                                                                    local database oid on the cluster (node_sysid, node_timeline)
  `node_status`{.STRUCTFIELD}          `char`{.TYPE}                                                                                   Readiness of the node: \[b\]eginning setup, \[i\]nitializing, \[c\]atchup, creating \[o\]utbound slots, \[r\]eady, \[k\]illed. Doesn\'t indicate connected/disconnected.
  `node_name`{.STRUCTFIELD}            `text`{.TYPE}                                                                                   Name of the node
  `node_local_dsn`{.STRUCTFIELD}       `text`{.TYPE}       `Node management function:`{.STRUCTNAME}`node_local_dsn`{.STRUCTFIELD}      A local loopback or unix socket connection string that the node can use to connect to its self; this is only used during initial setup to make the database restore faster.
  `node_init_from_dsn`{.STRUCTFIELD}   `text`{.TYPE}       `Node management function:`{.STRUCTNAME}`node_external_dsn`{.STRUCTFIELD}   Connection string of the node chosen as join target. Not used after we\'ve joined the node.
  `node_read_only`{.STRUCTFIELD}       `boolean`{.TYPE}                                                                                False unless read-only mode for a node is turned ON.
  `node_seq_id`{.STRUCTFIELD}          `smallint`{.TYPE}                                                                                
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  -------------------------------------------- ------------------------------------------ -----------------------------------------------------
  [Prev](catalogs-views.md){accesskey="P"}       [Home](index.md){accesskey="H"}        [Next](catalog-bdr-connections.md){accesskey="N"}
  Catalogs and Views                            [Up](catalogs-views.md){accesskey="U"}                                    bdr.bdr_connections
  -------------------------------------------- ------------------------------------------ -----------------------------------------------------
:::
