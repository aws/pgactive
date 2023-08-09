  [BDR 2.1.0 Documentation](README.md)                                                                                                       
  [Prev](catalog-bdr-nodes.md "bdr.bdr_nodes")   [Up](catalogs-views.md)    Chapter 13. Catalogs and Views    [Next](catalog-bdr-node-slots.md "bdr.bdr_node_slots")  


# 13.2. bdr.bdr_connections

The `bdr.bdr_connections` table keeps track of the connection
strings used for each node to connect to each other node.

Avoid directly modifying `bdr.bdr_connections`. Use the
provided node management functions instead. See [Node management
functions](functions-node-mgmt.md). It is safe to modify the
connection string.


**Table 13-2. `bdr.bdr_connections` Columns**

  Name                                     Type               References                                                                 Description
  `conn_sysid`               `text`      `bdr.bdr_nodes``.node_sysid`                    System identifer for the node this entry\'s dsn refers to.
  `conn_timeline`            `oid`       `bdr.bdr_nodes``.node_timeline`                 System timeline ID for the node this entry\'s dsn refers to.
  `conn_dboid`               `oid`       `bdr.bdr_nodes``.node_dboid`                    System database OID for the node this entry\'s dsn refers to
  `conn_origin_sysid`        `text`                                                                                 If set, ignore this entry unless the local sysid is this.
  `conn_origin_timeline`     `oid`                                                                                  If set, ignore this entry unless the local timeline is this.
  `conn_origin_dboid`        `oid`                                                                                  If set, ignore this entry unless the local dboid is this.
  `conn_is_unidirectional`   `boolean`                                                                              Indicates that this connection is unidirectional; there won\'t be a corresponding inbound connection from the peer node.
  `conn_dsn`                 `text`      `bdr.bdr_nodes``.node_local_dsn`                A libpq-style connection string specifying how to make a connection to this node from other nodes.
  `conn_apply_delay`         `integer`                                                                              If set, milliseconds to wait before applying each transaction from the remote node. Mainly for debugging. If null, the global default applies.
  `conn_replication_sets`    `text[]`    `Node management function:``replication_sets`   Replication sets this connection should participate in, if non-default.



  ----------------------------------------------- ------------------------------------------ ----------------------------------------------------
  [Prev](catalog-bdr-nodes.md)       [Home](README.md)        [Next](catalog-bdr-node-slots.md)  
  bdr.bdr_nodes                                    [Up](catalogs-views.md)                                    bdr.bdr_node_slots
  ----------------------------------------------- ------------------------------------------ ----------------------------------------------------
