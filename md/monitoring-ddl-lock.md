::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                                        
  ----------------------------------------------------------------------------- -------------------------------------- ----------------------- ------------------------------------------------------------------------------
  [Prev](monitoring-peers.md "Monitoring replication peers"){accesskey="P"}   [Up](monitoring.md){accesskey="U"}    Chapter 7. Monitoring    [Next](monitoring-conflict-stats.md "Monitoring conflicts"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [7.4. Monitoring global DDL locks]{#MONITORING-DDL-LOCK} {#monitoring-global-ddl-locks .SECT1}

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
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ---------------------------------------------- -------------------------------------- -------------------------------------------------------
  [Prev](monitoring-peers.md){accesskey="P"}     [Home](index.md){accesskey="H"}      [Next](monitoring-conflict-stats.md){accesskey="N"}
  Monitoring replication peers                    [Up](monitoring.md){accesskey="U"}                                     Monitoring conflicts
  ---------------------------------------------- -------------------------------------- -------------------------------------------------------
:::
