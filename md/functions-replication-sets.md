::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                                       
  ----------------------------------------------------------------------------- ------------------------------------- ----------------------- -------------------------------------------------------------------------------------------------
  [Prev](functions-node-mgmt.md "Node management functions"){accesskey="P"}   [Up](functions.md){accesskey="U"}    Chapter 12. Functions    [Next](functions-conflict-handlers.md "Conflict handler management functions"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [12.2. Replication Set functions]{#FUNCTIONS-REPLICATION-SETS} {#replication-set-functions .SECT1}

The following functions exist to manage [Replication
Sets](replication-sets.md).

::: TABLE
[]{#AEN3162}

**Table 12-2. Replication Set functions**

Function
:::
:::

Return Type

Description

[]{#FUNCTION-BDR-TABLE-SET-REPLICATION-SETS}

`bdr.table_set_replication_sets(`{.FUNCTION}*`p_relation regclass`{.REPLACEABLE}*`, `{.FUNCTION}*`p_sets text[]`{.REPLACEABLE}*`)`{.FUNCTION}

void

Sets the replication sets of a table. The previous assignment will be
overwritten (not merged or added to). Setting a table\'s replication
sets does [*not*]{.emphasis} cause the table to be synchronized to nodes
that will newly start receiving changes to the table, nor does it cause
it to be emptied on nodes that will newly stop receiving changes. See
[Replication Set Concepts](replication-sets-concepts.md). Pass
`NULL`{.LITERAL} (not the empty array) as the replication sets to
restore a table\'s replication sets to defaults.

[]{#FUNCTION-BDR-TABLE-GET-REPLICATION-SETS}

`bdr.table_get_replication_sets(`{.FUNCTION}*`relation regclass`{.REPLACEABLE}*`)`{.FUNCTION}

text\[\]

Returns the replication sets the specified table is part of.

[]{#FUNCTION-BDR-CONNECTION-SET-REP-SETS-BYNAME}

`bdr.connection_set_replication_sets(`{.FUNCTION}*`replication_sets text[]`{.REPLACEABLE}*`, `{.FUNCTION}*`node_name text`{.REPLACEABLE}*`)`{.FUNCTION}

void

Sets the replication sets of the default connection for the named node.
The previous assignment will be overwritten. Any override connections
for individual nodes (where the `origin_sysid`{.LITERAL} etc in
`bdr.bdr_connections`{.LITERAL} are nonzero) are left unchanged; a
warning will be emitted if any are present.

[]{#FUNCTION-BDR-CONNECTION-GET-REP-SETS-BYNAME}

`bdr.connection_get_replication_sets(`{.FUNCTION}*`node_name text`{.REPLACEABLE}*`)`{.FUNCTION}

text\[\]

Returns the replication sets configured for the the default connection
to the named node. Any override connections for individual nodes (where
the `origin_sysid`{.LITERAL} etc in `bdr.bdr_connections`{.LITERAL} are
nonzero) are ignored and a warning is issued if any are present.

[]{#FUNCTION-BDR-CONNECTION-SET-REP-SETS-BYID}

`bdr.connection_set_replication_sets(`{.FUNCTION}*`replication_sets text[]`{.REPLACEABLE}*`, `{.FUNCTION}*`sysid text`{.REPLACEABLE}*`, `{.FUNCTION}*`timeline oid`{.REPLACEABLE}*`, `{.FUNCTION}*`dboid oid`{.REPLACEABLE}*`, `{.FUNCTION}*`origin_sysid text default '0'`{.REPLACEABLE}*`, `{.FUNCTION}*`origin_timeline oid default 0`{.REPLACEABLE}*`, `{.FUNCTION}*`origin_dboid oid default 0`{.REPLACEABLE}*`)`{.FUNCTION}

void

Sets the replication sets of the connection with the given (sysid,
timeline, dboid) identity tuple. If no (origin_sysid, origin_timeline,
origin_dboid) are specified, the default connection for the node is
modified. Otherwise the override connection for the given node is
updated instead. In almost all cases it\'s simpler to just use the
node-name variant; this is for advanced uses.

[]{#FUNCTION-BDR-CONNECTION-GET-REP-SETS-BYID}

`bdr.connection_get_replication_sets(`{.FUNCTION}*`sysid text`{.REPLACEABLE}*`, `{.FUNCTION}*`timeline oid`{.REPLACEABLE}*`, `{.FUNCTION}*`dboid oid`{.REPLACEABLE}*`, `{.FUNCTION}*`origin_sysid text default '0'`{.REPLACEABLE}*`, `{.FUNCTION}*`origin_timeline oid default 0`{.REPLACEABLE}*`, `{.FUNCTION}*`origin_dboid oid default 0`{.REPLACEABLE}*`)`{.FUNCTION}

text\[\]

Returns the replication sets of the connection with the given (sysid,
timeline, dboid) identity tuple. If no (origin_sysid, origin_timeline,
origin_dboid) are specified, the default connection for the node is
returned. Otherwise the override connection for the given node is
returned instead. In almost all cases it\'s simpler to just use the
node-name variant; this is for advanced uses.

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------------- ------------------------------------- ---------------------------------------------------------
  [Prev](functions-node-mgmt.md){accesskey="P"}     [Home](index.md){accesskey="H"}     [Next](functions-conflict-handlers.md){accesskey="N"}
  Node management functions                          [Up](functions.md){accesskey="U"}                      Conflict handler management functions
  ------------------------------------------------- ------------------------------------- ---------------------------------------------------------
:::
