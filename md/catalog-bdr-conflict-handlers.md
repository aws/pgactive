::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ------------------------------------------------------------------------------------------------- ------------------------------------------ -------------------------------- ---------------------------------------------------------------
  [Prev](catalog-bdr-replication-set-config.md "bdr.bdr_replication_set_config"){accesskey="P"}   [Up](catalogs-views.md){accesskey="U"}    Chapter 13. Catalogs and Views    [Next](catalog-bdr-locks.md "bdr.bdr_locks"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [13.7. bdr.bdr_conflict_handlers]{#CATALOG-BDR-CONFLICT-HANDLERS} {#bdr.bdr_conflict_handlers .SECT1}

`bdr.bdr_conflict_handlers`{.LITERAL} manages user-defined conflict
handlers; see [User defined conflict
handlers](conflicts-user-defined-handlers.md). Do not modify this
table directly.

::: TABLE
[]{#AEN4096}

**Table 13-6. `bdr.bdr_conflict_handlers`{.STRUCTNAME} Columns**

  Name                           Type                             References   Description
  ------------------------------ -------------------------------- ------------ -------------
  `ch_name`{.STRUCTFIELD}        `name`{.TYPE}                                  
  `ch_type`{.STRUCTFIELD}        `bdr.bdr_conflict_type`{.TYPE}                 
  `ch_reloid`{.STRUCTFIELD}      `oid`{.TYPE}                                   
  `ch_fun`{.STRUCTFIELD}         `text`{.TYPE}                                  
  `ch_timeframe`{.STRUCTFIELD}   `interval`{.TYPE}                              
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ---------------------------------------------------------------- ------------------------------------------ -----------------------------------------------
  [Prev](catalog-bdr-replication-set-config.md){accesskey="P"}       [Home](index.md){accesskey="H"}        [Next](catalog-bdr-locks.md){accesskey="N"}
  bdr.bdr_replication_set_config                                    [Up](catalogs-views.md){accesskey="U"}                                    bdr.bdr_locks
  ---------------------------------------------------------------- ------------------------------------------ -----------------------------------------------
:::
