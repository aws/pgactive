  [BDR 2.1.0 Documentation](README.md)                                                                                                                                         
  [Prev](catalog-bdr-replication-set-config.md "bdr.bdr_replication_set_config")   [Up](catalogs-views.md)    Chapter 13. Catalogs and Views    [Next](catalog-bdr-global-locks-info.md "bdr.bdr_global_locks_info")


# 13.7. bdr.bdr_conflict_handlers

`bdr.bdr_conflict_handlers` manages user-defined conflict
handlers; see [User defined conflict
handlers](conflicts-user-defined-handlers.md). Do not modify this
table directly.


**Table 13-6. `bdr.bdr_conflict_handlers` Columns**

  Name                           Type                             References   Description
  ------------------------------ -------------------------------- ------------ -------------
  `ch_name`        `name`                                  
  `ch_type`        `bdr.bdr_conflict_type`                 
  `ch_reloid`      `oid`                                   
  `ch_fun`         `text`                                  
  `ch_timeframe`   `interval`                              



  ---------------------------------------------------------------- ------------------------------------------ -----------------------------------------------
  [Prev](catalog-bdr-replication-set-config.md)       [Home](README.md)        [Next](catalog-bdr-global-locks-info.md)
  bdr.bdr_replication_set_config                                    [Up](catalogs-views.md)                                    bdr.bdr_global_locks_info
  ---------------------------------------------------------------- ------------------------------------------ -----------------------------------------------
