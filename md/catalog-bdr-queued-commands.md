::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                                                     
  ----------------------------------------------------------------------------- ------------------------------------------ -------------------------------- -----------------------------------------------------------------------------
  [Prev](catalog-bdr-global-locks.md "bdr.bdr_global_locks"){accesskey="P"}   [Up](catalogs-views.md){accesskey="U"}    Chapter 13. Catalogs and Views    [Next](catalog-bdr-queued-drops.md "bdr.bdr_queued_drops"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [13.10. bdr.bdr_queued_commands]{#CATALOG-BDR-QUEUED-COMMANDS} {#bdr.bdr_queued_commands .SECT1}

`bdr.bdr_queued_commands`{.LITERAL} is a BDR internal implementation
table used for [DDL replication](ddl-replication.md). Do not modify
this table directly.

Every DDL operation (except table/sequence drops) that\'s captured and
replicated is inserted in this table, as is every operation manually
queued via `bdr.bdr_queue_ddl_commands()`{.FUNCTION}. Inspecting this
table can be useful to determine what schema changes were made when and
by whom.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------------------ ------------------------------------------ ------------------------------------------------------
  [Prev](catalog-bdr-global-locks.md){accesskey="P"}       [Home](index.md){accesskey="H"}        [Next](catalog-bdr-queued-drops.md){accesskey="N"}
  bdr.bdr_global_locks                                    [Up](catalogs-views.md){accesskey="U"}                                    bdr.bdr_queued_drops
  ------------------------------------------------------ ------------------------------------------ ------------------------------------------------------
:::
