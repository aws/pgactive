  [BDR 2.1.0 Documentation](README.md)                                                                                                                     
  [Prev](catalog-bdr-global-locks.md "bdr.bdr_global_locks")   [Up](catalogs-views.md)    Chapter 13. Catalogs and Views    [Next](catalog-bdr-queued-drops.md "bdr.bdr_queued_drops")  


# 13.10. bdr.bdr_queued_commands

`bdr.bdr_queued_commands` is a BDR internal implementation
table used for [DDL replication](ddl-replication.md). Do not modify
this table directly.

Every DDL operation (except table/sequence drops) that\'s captured and
replicated is inserted in this table, as is every operation manually
queued via `bdr.bdr_queue_ddl_commands()`. Inspecting this
table can be useful to determine what schema changes were made when and
by whom.



  ------------------------------------------------------ ------------------------------------------ ------------------------------------------------------
  [Prev](catalog-bdr-global-locks.md)       [Home](README.md)        [Next](catalog-bdr-queued-drops.md)  
  bdr.bdr_global_locks                                    [Up](catalogs-views.md)                                    bdr.bdr_queued_drops
  ------------------------------------------------------ ------------------------------------------ ------------------------------------------------------
