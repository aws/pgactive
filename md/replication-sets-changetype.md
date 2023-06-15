::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  --------------------------------------------------------------------------------- -------------------------------------------- ------------------------------ ---------------------------------------------------
  [Prev](replication-sets-tables.md "Table Replication Control"){accesskey="P"}   [Up](replication-sets.md){accesskey="U"}    Chapter 11. Replication Sets    [Next](functions.md "Functions"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [11.5. Change-type replication sets]{#REPLICATION-SETS-CHANGETYPE} {#change-type-replication-sets .SECT1}

In addition to table- and node-level replication set control, it\'s also
possible to configure which [*operations*]{.emphasis} replication sets
replicate. A replication set can be configured to replicate only
`INSERT`{.LITERAL}s, for example. New rows inserted in the table will be
replicated, but `UPDATE`{.LITERAL}s of existing rows will not be, and
when a row is `DELETE`{.LITERAL}d the remote copies of the row won\'t be
deleted. Obviously this creates node-to-node inconsistencies, so it must
be used with extreme caution.

The main use of operation-level replication set control is maintaining
archive and DW nodes, where data removed from other nodes is retained on
the archive/DW node.

Operation-level replication set control is a low-level advanced feature
that doesn\'t yet have any management functions for it. To customise
which operations a replication set syncs, `INSERT`{.LITERAL} a row into
`bdr.bdr_replication_set_config`{.LITERAL}, like:

``` PROGRAMLISTING
    INSERT INTO bdr.bdr_replication_set_config(set_name, replicate_inserts, replicate_updates, replicate_deletes)
    VALUES ('set_name', 't', 't', 't');

```

Adjust the replication flags as desired for the intended replication set
function.

Like all replication set changes, changes to the operations replicated
by a replication set take effect only for new data changes; no
already-replicated rows will be retroactively changed.

::: WARNING
  -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  **Warning**
  Currently the `TRUNCATE`{.LITERAL} operation is [*always*]{.emphasis} replicated, even if a table is not a member of any active replication set. Use `DELETE FROM tablename;`{.LITERAL} if this is not desired.
  -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ----------------------------------------------------- -------------------------------------------- ---------------------------------------
  [Prev](replication-sets-tables.md){accesskey="P"}        [Home](index.md){accesskey="H"}         [Next](functions.md){accesskey="N"}
  Table Replication Control                              [Up](replication-sets.md){accesskey="U"}                                Functions
  ----------------------------------------------------- -------------------------------------------- ---------------------------------------
:::
