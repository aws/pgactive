  [BDR 2.0.7 Documentation](README.md)                                                                                                        
  [Prev](monitoring-peers.md "Monitoring replication peers")   [Up](monitoring.md)    Chapter 7. Monitoring    [Next](monitoring-conflict-stats.md "Monitoring conflicts")  


# [7.4. Monitoring global DDL locks]

The global DDL lock, used in [DDL replication](ddl-replication.md),
can cancel and/or block concurrent writes and other DDL. So it can be
important to determine what is taking the lock, whether it holds it or
is still trying to acquire it, and how long it\'s been trying or has
held the lock.

DDL locking activity can be traced using the
[bdr.trace_ddl_locks_level](bdr-configuration-variables.md#GUC-BDR-TRACE-DDL-LOCKS-LEVEL)
setting using the PostgreSQL log files, which provides the most complete
and useful way to see DDL locking activity. It is strongly recommended
that you enable DDL lock tracing.

The [bdr.bdr_locks](catalog-bdr-locks.md) view provides visibility
into the current DDL locking state of a node.

See [DDL Locking](ddl-replication-advice.md#DDL-REPLICATION-LOCKING)
for more detail on how the global DDL lock works.



  ---------------------------------------------- -------------------------------------- -------------------------------------------------------
  [Prev](monitoring-peers.md)     [Home](README.md)      [Next](monitoring-conflict-stats.md)  
  Monitoring replication peers                    [Up](monitoring.md)                                     Monitoring conflicts
  ---------------------------------------------- -------------------------------------- -------------------------------------------------------
