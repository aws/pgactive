::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  --------------------------------------------------------------- ------------------------------------------ -------------------------------- -------------------------------------------------------------------------
  [Prev](catalog-bdr-nodes.md "bdr.bdr_nodes"){accesskey="P"}   [Up](catalogs-views.md){accesskey="U"}    Chapter 13. Catalogs and Views    [Next](catalog-bdr-node-slots.md "bdr.bdr_node_slots"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [13.2. bdr.bdr_connections]{#CATALOG-BDR-CONNECTIONS} {#bdr.bdr_connections .SECT1}

The `bdr.bdr_connections`{.LITERAL} table keeps track of the connection
strings used for each node to connect to each other node.

Avoid directly modifying `bdr.bdr_connections`{.LITERAL}. Use the
provided node management functions instead. See [Node management
functions](functions-node-mgmt.md). It is safe to modify the
connection string.

::: TABLE
[]{#AEN3509}

**Table 13-2. `bdr.bdr_connections`{.STRUCTNAME} Columns**

  Name                                     Type               References                                                                 Description
  ---------------------------------------- ------------------ -------------------------------------------------------------------------- ------------------------------------------------------------------------------------------------------------------------------------------------
  `conn_sysid`{.STRUCTFIELD}               `text`{.TYPE}      `bdr.bdr_nodes`{.STRUCTNAME}`.node_sysid`{.STRUCTFIELD}                    System identifer for the node this entry\'s dsn refers to.
  `conn_timeline`{.STRUCTFIELD}            `oid`{.TYPE}       `bdr.bdr_nodes`{.STRUCTNAME}`.node_timeline`{.STRUCTFIELD}                 System timeline ID for the node this entry\'s dsn refers to.
  `conn_dboid`{.STRUCTFIELD}               `oid`{.TYPE}       `bdr.bdr_nodes`{.STRUCTNAME}`.node_dboid`{.STRUCTFIELD}                    System database OID for the node this entry\'s dsn refers to
  `conn_origin_sysid`{.STRUCTFIELD}        `text`{.TYPE}                                                                                 If set, ignore this entry unless the local sysid is this.
  `conn_origin_timeline`{.STRUCTFIELD}     `oid`{.TYPE}                                                                                  If set, ignore this entry unless the local timeline is this.
  `conn_origin_dboid`{.STRUCTFIELD}        `oid`{.TYPE}                                                                                  If set, ignore this entry unless the local dboid is this.
  `conn_is_unidirectional`{.STRUCTFIELD}   `boolean`{.TYPE}                                                                              Indicates that this connection is unidirectional; there won\'t be a corresponding inbound connection from the peer node.
  `conn_dsn`{.STRUCTFIELD}                 `text`{.TYPE}      `bdr.bdr_nodes`{.STRUCTNAME}`.node_local_dsn`{.STRUCTFIELD}                A libpq-style connection string specifying how to make a connection to this node from other nodes.
  `conn_apply_delay`{.STRUCTFIELD}         `integer`{.TYPE}                                                                              If set, milliseconds to wait before applying each transaction from the remote node. Mainly for debugging. If null, the global default applies.
  `conn_replication_sets`{.STRUCTFIELD}    `text[]`{.TYPE}    `Node management function:`{.STRUCTNAME}`replication_sets`{.STRUCTFIELD}   Replication sets this connection should participate in, if non-default.
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ----------------------------------------------- ------------------------------------------ ----------------------------------------------------
  [Prev](catalog-bdr-nodes.md){accesskey="P"}       [Home](index.md){accesskey="H"}        [Next](catalog-bdr-node-slots.md){accesskey="N"}
  bdr.bdr_nodes                                    [Up](catalogs-views.md){accesskey="U"}                                    bdr.bdr_node_slots
  ----------------------------------------------- ------------------------------------------ ----------------------------------------------------
:::
