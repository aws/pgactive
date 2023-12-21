  [BDR 2.1.0 Documentation](README.md)                                                                                                           
  [Prev](catalog-bdr-stats.md "bdr.bdr_stats")   [Up](catalogs-views.md)    Chapter 13. Catalogs and Views    [Next](catalog-bdr-replication-set-config.md "bdr.bdr_replication_set_config")


# 13.5. bdr.bdr_conflict_history

`bdr.bdr_conflict_history` contains an entry for each conflict
generated in the system while [Conflict logging](conflicts-logging.md)
is enabled. Unless conflict logging to table is enabled this table will
always be empty.

This history table is [*not replicated*] between nodes, so
each node has separate conflict history records. This is a technical
limitation that may be lifted in a future release, but it also saves on
unnecessary replication overhead.

It is safe to `TRUNCATE` this table to save disk space.


**Table 13-5. `bdr.bdr_conflict_history` Columns**

  Name                                          Type                                   References   Description
  `local_conflict_lsn`            `pg_lsn`                                     xlog position at the time the conflict occured on the applying node.
  `local_conflict_time`           `timestamp with timezone`                    The time the conflict was detected on the applying node. This is not the conflicting transaction\'s commit time; see `local_commit_time`.
  `object_schema`                 `text`                                       Schema of the object involved in the conflict.
  `object_name`                   `text`                                       Name of the object (table, etc) involved in the conflict.
  `remote_node_sysid`             `text`                                       sysid of the remote node the conflicting transaction originated from.
  `remote_xid`                    `xid`                                        xid of the remote transaction involved in the conflict.
  `remote_commit_time`            `timestamp with timezone`                    The time the remote transaction involved in this conflict committed.
  `remote_commit_lsn`             `pg_lsn`                                     LSN on remote node at which conflicting transaction committed.
  `conflict_type`                 `bdr.bdr_conflict_type`                      Nature of the conflict - insert/insert, update/delete, etc.
  `conflict_resolution`           `bdr.bdr_conflict_resolution`                How the conflict was resolved/handled; see the enum definition.
  `local_tuple`                   `json`                                       For DML conflicts, the conflicting tuple from the local DB (as json), if logged.
  `remote_tuple`                  `json`                                       For DML conflicts, the conflicting tuple from the remote DB (as json), if logged
  `local_tuple_xmin`              `xid`                                        If local_tuple is set, the xmin of the conflicting local tuple.
  `local_tuple_origin_sysid`      `text`                                       The node id for the true origin of the local tuple. Differs from local_node_sysid if the tuple was originally replicated from another node.
  `error_message`                 `text`                                       On apply error, the error message from ereport/elog. Other error fields match.
  `error_sqlstate`                `text`                                        
  `error_querystring`             `text`                                        
  `error_cursorpos`               `integer`                                     
  `error_detail`                  `text`                                        
  `error_hint`                    `text`                                        
  `error_context`                 `text`                                        
  `error_columnname`              `text`                                        
  `error_typename`                `text`                                        
  `error_constraintname`          `text`                                        
  `error_filename`                `text`                                        
  `error_lineno`                  `integer`                                     
  `error_funcname`                `text`                                        
  `remote_node_timeline`          `oid`                                         
  `remote_node_dboid`             `oid`                                         
  `local_tuple_origin_timeline`   `oid`                                         
  `local_tuple_origin_dboid`      `oid`                                         
  `local_commit_time`             `timestamp with timezone`                    The time the local transaction involved in this conflict committed.

We recommend that you schedule a job that purges this table. For example, keeping a week's worth of entries might be sufficient for troubleshooting purposes.

The following example uses the pg_cron extension and the cron.schedule function to schedule a job that runs every day at midnight to purge the `bdr.bdr_conflict_history` table. The job keeps only the last seven days.

`
SELECT cron.schedule('0 0 * * *', $$DELETE
    FROM bdr.bdr_conflict_history
    WHERE local_conflict_time < now() - interval '7 days'$$);
`

  ------------------------------------------------- ------------------------------------------ ----------------------------------------------------------------
  [Prev](catalog-bdr-stats.md)       [Home](README.md)        [Next](catalog-bdr-replication-set-config.md)
  bdr.bdr_stats                                    [Up](catalogs-views.md)                                    bdr.bdr_replication_set_config
  ------------------------------------------------- ------------------------------------------ ----------------------------------------------------------------
