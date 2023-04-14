::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                                                           
  ----------------------------------------------------------------------------------- ------------------------------------------ -------------------------------- -----------------------------------------------------------------------------------
  [Prev](catalog-bdr-queued-commands.md "bdr.bdr_queued_commands"){accesskey="P"}   [Up](catalogs-views.md){accesskey="U"}    Chapter 13. Catalogs and Views    [Next](catalog-bdr-sequence-values.md "bdr.bdr_sequence_values"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [13.11. bdr.bdr_queued_drops]{#CATALOG-BDR-QUEUED-DROPS} {#bdr.bdr_queued_drops .SECT1}

`bdr.bdr_queued_drops`{.LITERAL} is a BDR internal implementation table
used for [DDL replication](ddl-replication.md). Do not modify this
table directly.

Every table/sequence drop operation that\'s captured and replicated is
inserted in this table. Inspecting this table can be useful to determine
what schema changes were made when and by whom.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  --------------------------------------------------------- ------------------------------------------ ---------------------------------------------------------
  [Prev](catalog-bdr-queued-commands.md){accesskey="P"}       [Home](index.md){accesskey="H"}        [Next](catalog-bdr-sequence-values.md){accesskey="N"}
  bdr.bdr_queued_commands                                    [Up](catalogs-views.md){accesskey="U"}                                    bdr.bdr_sequence_values
  --------------------------------------------------------- ------------------------------------------ ---------------------------------------------------------
:::
