::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ------------------------------------------------------------------- ------------------------------------------ -------------------------------- -------------------------------------------------------------------------------------------------
  [Prev](catalog-pg-stat-bdr.md "bdr.pg_stat_bdr"){accesskey="P"}   [Up](catalogs-views.md){accesskey="U"}    Chapter 13. Catalogs and Views    [Next](catalog-bdr-replication-set-config.md "bdr.bdr_replication_set_config"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [13.5. bdr.bdr_conflict_history]{#CATALOG-BDR-CONFLICT-HISTORY} {#bdr.bdr_conflict_history .SECT1}

`bdr.bdr_conflict_history`{.LITERAL} contains an entry for each conflict
generated in the system while [Conflict logging](conflicts-logging.md)
is enabled. Unless conflict logging to table is enabled this table will
always be empty.

This history table is [*not replicated*]{.emphasis} between nodes, so
each node has separate conflict history records. This is a technical
limitation that may be lifted in a future release, but it also saves on
unnecessary replication overhead.

It is safe to `TRUNCATE`{.LITERAL} this table to save disk space.

::: TABLE
[]{#AEN3842}

**Table 13-5. `bdr.bdr_conflict_history`{.STRUCTNAME} Columns**

  Name                                          Type                                   References   Description
  --------------------------------------------- -------------------------------------- ------------ -----------------------------------------------------------------------------------------------------------------------------------------------------
  `local_conflict_lsn`{.STRUCTFIELD}            `pg_lsn`{.TYPE}                                     xlog position at the time the conflict occured on the applying node.
  `local_conflict_time`{.STRUCTFIELD}           `timestamp with timezone`{.TYPE}                    The time the conflict was detected on the applying node. This is not the conflicting transaction\'s commit time; see `local_commit_time`{.LITERAL}.
  `object_schema`{.STRUCTFIELD}                 `text`{.TYPE}                                       Schema of the object involved in the conflict.
  `object_name`{.STRUCTFIELD}                   `text`{.TYPE}                                       Name of the object (table, etc) involved in the conflict.
  `remote_node_sysid`{.STRUCTFIELD}             `text`{.TYPE}                                       sysid of the remote node the conflicting transaction originated from.
  `remote_xid`{.STRUCTFIELD}                    `xid`{.TYPE}                                        xid of the remote transaction involved in the conflict.
  `remote_commit_time`{.STRUCTFIELD}            `timestamp with timezone`{.TYPE}                    The time the remote transaction involved in this conflict committed.
  `remote_commit_lsn`{.STRUCTFIELD}             `pg_lsn`{.TYPE}                                     LSN on remote node at which conflicting transaction committed.
  `conflict_type`{.STRUCTFIELD}                 `bdr.bdr_conflict_type`{.TYPE}                      Nature of the conflict - insert/insert, update/delete, etc.
  `conflict_resolution`{.STRUCTFIELD}           `bdr.bdr_conflict_resolution`{.TYPE}                How the conflict was resolved/handled; see the enum definition.
  `local_tuple`{.STRUCTFIELD}                   `json`{.TYPE}                                       For DML conflicts, the conflicting tuple from the local DB (as json), if logged.
  `remote_tuple`{.STRUCTFIELD}                  `json`{.TYPE}                                       For DML conflicts, the conflicting tuple from the remote DB (as json), if logged
  `local_tuple_xmin`{.STRUCTFIELD}              `xid`{.TYPE}                                        If local_tuple is set, the xmin of the conflicting local tuple.
  `local_tuple_origin_sysid`{.STRUCTFIELD}      `text`{.TYPE}                                       The node id for the true origin of the local tuple. Differs from local_node_sysid if the tuple was originally replicated from another node.
  `error_message`{.STRUCTFIELD}                 `text`{.TYPE}                                       On apply error, the error message from ereport/elog. Other error fields match.
  `error_sqlstate`{.STRUCTFIELD}                `text`{.TYPE}                                        
  `error_querystring`{.STRUCTFIELD}             `text`{.TYPE}                                        
  `error_cursorpos`{.STRUCTFIELD}               `integer`{.TYPE}                                     
  `error_detail`{.STRUCTFIELD}                  `text`{.TYPE}                                        
  `error_hint`{.STRUCTFIELD}                    `text`{.TYPE}                                        
  `error_context`{.STRUCTFIELD}                 `text`{.TYPE}                                        
  `error_columnname`{.STRUCTFIELD}              `text`{.TYPE}                                        
  `error_typename`{.STRUCTFIELD}                `text`{.TYPE}                                        
  `error_constraintname`{.STRUCTFIELD}          `text`{.TYPE}                                        
  `error_filename`{.STRUCTFIELD}                `text`{.TYPE}                                        
  `error_lineno`{.STRUCTFIELD}                  `integer`{.TYPE}                                     
  `error_funcname`{.STRUCTFIELD}                `text`{.TYPE}                                        
  `remote_node_timeline`{.STRUCTFIELD}          `oid`{.TYPE}                                         
  `remote_node_dboid`{.STRUCTFIELD}             `oid`{.TYPE}                                         
  `local_tuple_origin_timeline`{.STRUCTFIELD}   `oid`{.TYPE}                                         
  `local_tuple_origin_dboid`{.STRUCTFIELD}      `oid`{.TYPE}                                         
  `local_commit_time`{.STRUCTFIELD}             `timestamp with timezone`{.TYPE}                    The time the local transaction involved in this conflict committed.
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------------- ------------------------------------------ ----------------------------------------------------------------
  [Prev](catalog-pg-stat-bdr.md){accesskey="P"}       [Home](index.md){accesskey="H"}        [Next](catalog-bdr-replication-set-config.md){accesskey="N"}
  bdr.pg_stat_bdr                                    [Up](catalogs-views.md){accesskey="U"}                                    bdr.bdr_replication_set_config
  ------------------------------------------------- ------------------------------------------ ----------------------------------------------------------------
:::
