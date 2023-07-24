  [BDR 2.0.7 Documentation](README.md)                                                                                                                           
  [Prev](catalog-bdr-queued-commands.md "bdr.bdr_queued_commands")   [Up](catalogs-views.md)    Chapter 13. Catalogs and Views    [Next](upgrade.md "Upgrading BDR")

# 13.11. bdr.bdr_queued_drops

`bdr.bdr_queued_drops` is a BDR internal implementation table
used for [DDL replication](ddl-replication.md). Do not modify this
table directly.

Every table/sequence drop operation that\'s captured and replicated is
inserted in this table. Inspecting this table can be useful to determine
what schema changes were made when and by whom.



  --------------------------------------------------------- ------------------------------------------ ---------------------------------------------------------
  [Prev](catalog-bdr-queued-commands.md)       [Home](README.md)        [Next](upgrade.md)  
  bdr.bdr_queued_commands                                    [Up](catalogs-views.md)                                     Upgrading [BDR]
  --------------------------------------------------------- ------------------------------------------ ---------------------------------------------------------
