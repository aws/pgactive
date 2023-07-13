  [BDR 2.0.7 Documentation](README.md)                                                                                                       
  [Prev](functions-node-mgmt.md "Node management functions")   [Up](functions.md)    Chapter 12. Functions    [Next](functions-conflict-handlers.md "Conflict handler management functions")  


# 12.2. Replication Set functions

The following functions exist to manage [Replication
Sets](replication-sets.md).


**Table 12-2. Replication Set functions**

Function

Return Type

Description


`bdr.bdr_set_table_replication_sets(`*`p_relation regclass`*`, `*`p_sets text[]`*`)`

void

Sets the replication sets of a table. The previous assignment will be
overwritten (not merged or added to). Setting a table\'s replication
sets does [*not*] cause the table to be synchronized to nodes
that will newly start receiving changes to the table, nor does it cause
it to be emptied on nodes that will newly stop receiving changes. See
[Replication Set Concepts](replication-sets-concepts.md). Pass
`NULL` (not the empty array) as the replication sets to
restore a table\'s replication sets to defaults.

If `bdr.skip_ddl_replication` is set to true the replication set is created
locally (means it would need to be executed on all the BDR nodes).

`bdr.bdr_get_table_replication_sets(`*`relation regclass`*`)`

text\[\]

Returns the replication sets the specified table is part of.


`bdr.bdr_set_connection_replication_sets(`*`replication_sets text[]`*`, `*`node_name text`*`)`

void

Sets the replication sets of the default connection for the named node.
The previous assignment will be overwritten. Any override connections
for individual nodes (where the `origin_sysid` etc in
`bdr.bdr_connections` are nonzero) are left unchanged; a
warning will be emitted if any are present.


`bdr.bdr_get_connection_replication_sets(`*`node_name text`*`)`

text\[\]

Returns the replication sets configured for the the default connection
to the named node. Any override connections for individual nodes (where
the `origin_sysid` etc in `bdr.bdr_connections` are
nonzero) are ignored and a warning is issued if any are present.


`bdr.bdr_set_connection_replication_sets(`*`replication_sets text[]`*`, `*`sysid text`*`, `*`timeline oid`*`, `*`dboid oid`*`, `*`origin_sysid text default '0'`*`, `*`origin_timeline oid default 0`*`, `*`origin_dboid oid default 0`*`)`

void

Sets the replication sets of the connection with the given (sysid,
timeline, dboid) identity tuple. If no (origin_sysid, origin_timeline,
origin_dboid) are specified, the default connection for the node is
modified. Otherwise the override connection for the given node is
updated instead. In almost all cases it\'s simpler to just use the
node-name variant; this is for advanced uses.


`bdr.bdr_get_connection_replication_sets(`*`sysid text`*`, `*`timeline oid`*`, `*`dboid oid`*`, `*`origin_sysid text default '0'`*`, `*`origin_timeline oid default 0`*`, `*`origin_dboid oid default 0`*`)`

text\[\]

Returns the replication sets of the connection with the given (sysid,
timeline, dboid) identity tuple. If no (origin_sysid, origin_timeline,
origin_dboid) are specified, the default connection for the node is
returned. Otherwise the override connection for the given node is
returned instead. In almost all cases it\'s simpler to just use the
node-name variant; this is for advanced uses.



  ------------------------------------------------- ------------------------------------- ---------------------------------------------------------
  [Prev](functions-node-mgmt.md)     [Home](README.md)     [Next](functions-conflict-handlers.md)  
  Node management functions                          [Up](functions.md)                      Conflict handler management functions
  ------------------------------------------------- ------------------------------------- ---------------------------------------------------------
