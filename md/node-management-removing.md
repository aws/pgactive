::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                                           
  ---------------------------------------------------------------------- ------------------------------------------- ---------------------------- ---------------------------------------------------------------------------------
  [Prev](node-management-joining.md "Joining a node"){accesskey="P"}   [Up](node-management.md){accesskey="U"}    Chapter 5. Node Management    [Next](node-management-disabling.md "Completely removing BDR"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [5.2. Parting (removing) a node]{#NODE-MANAGEMENT-REMOVING} {#parting-removing-a-node .SECT1}

Because BDR can recover from extended node outages it is necessary to
explicitly tell the system if you are removing a node permanently. If
you permanently shut down a node and don\'t tell the other nodes then
performance will suffer and eventually the whole system will stop
working.

Each node saves up change information (using one [replication
slot](http://www.postgresql.org/docs/current/static/logicaldecoding-explanation.html)
for each peer node) so it can replay changes to a temporarily
unreachable node. If a peer node remains offline indefinitely this
accumulating change information will cause the node to run out of
storage space for PostgreSQL transaction logs (WAL, in
`pg_xlog`{.FILENAME}), likely causing the database server to shut down
with an error like:

``` PROGRAMLISTING
    PANIC: could not write to file "pg_xlog/xlogtemp.559": No space left on device
   
```

or report other out-of-disk related symptoms.

::: NOTE
> **Note:** Administrators should monitor for node outages (see:
> [Monitoring](monitoring.md) and make sure nodes have sufficient free
> disk space.
:::

A node is removed with the
[bdr.bdr_part_by_node_names](functions-node-mgmt.md#FUNCTION-BDR-PART-BY-NODE-NAMES)
function. You must specify the node name (as passed during node
creation) to remove a node. You should call
`bdr.bdr_part_by_node_names`{.LITERAL} from a node that will remain in
the BDR group, not the node to be removed. Multiple nodes may be removed
at once. No value is returned; the removal status may be seen by
checking the `status`{.LITERAL} field in `bdr.bdr_nodes`{.LITERAL} for
that node.

To remove `node1`{.LITERAL}:

``` PROGRAMLISTING
    SELECT bdr.bdr_part_by_node_names(ARRAY['node-1']);
   
```

or to remove multiple nodes at once:

``` PROGRAMLISTING
    SELECT bdr.bdr_part_by_node_names(ARRAY['node-1', 'node-2', 'node-3']);
   
```

If you only know the slot name from `pg_replication_slots`{.LITERAL} and
not the node name from `bdr.bdr_nodes`{.LITERAL} you can either
`SELECT`{.LITERAL}
[bdr.bdr_get_local_node_name()](functions-information.md#FUNCTIONS-BDR-GET-LOCAL-NODE-NAME)
on the node you plan to remove, or look it up from the slot name using
the `bdr.bdr_node_slots`{.LITERAL} view.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ----------------------------------------------------- ------------------------------------------- -------------------------------------------------------
  [Prev](node-management-joining.md){accesskey="P"}        [Home](index.md){accesskey="H"}        [Next](node-management-disabling.md){accesskey="N"}
  Joining a node                                         [Up](node-management.md){accesskey="U"}                                  Completely removing BDR
  ----------------------------------------------------- ------------------------------------------- -------------------------------------------------------
:::
