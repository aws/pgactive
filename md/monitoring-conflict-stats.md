::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ------------------------------------------------------------------------------- -------------------------------------- ----------------------- -------------------------------------------------------------------------------------
  [Prev](monitoring-ddl-lock.md "Monitoring global DDL locks"){accesskey="P"}   [Up](monitoring.md){accesskey="U"}    Chapter 7. Monitoring    [Next](monitoring-postgres-stats.md "PostgreSQL statistics views"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [7.5. Monitoring conflicts]{#MONITORING-CONFLICT-STATS} {#monitoring-conflicts .SECT1}

[Multi-master conflicts](conflicts.md) can arise when multiple nodes
make changes that affect the same tables in ways that can interact with
each other. The BDR system should be monitored to ensure that conflicts
are identified and, where possible, applicaiton changes are made to
eliminate them or make them less frequent.

Not all conflicts are logged to
[bdr.bdr_conflict_history](catalog-bdr-conflict-history.md) even when
[bdr.log_conflicts_to_table](bdr-configuration-variables.md#GUC-BDR-LOG-CONFLICTS-TO-TABLE)
is on. Conflicts where BDR cannot proactively detect and handle the
conflict (like 3-way foreign key conflicts) will result in an
`ERROR`{.LITERAL} message in the PostgreSQL logs and an increment of
[bdr.pg_stat_bdr](catalog-pg-stat-bdr.md)`.nr_rollbacks`{.LITERAL} on
that node for the connection the conflicting transaction originated
from.

If `pg_stat_bdr.nr_rollbacks`{.LITERAL} keeps increasing and a node
isn\'t making forward progress, then it\'s likely there\'s a divergent
conflict or other issue that may need administrator action. Check the
log files for that node for details.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------------- -------------------------------------- -------------------------------------------------------
  [Prev](monitoring-ddl-lock.md){accesskey="P"}     [Home](index.md){accesskey="H"}      [Next](monitoring-postgres-stats.md){accesskey="N"}
  Monitoring global DDL locks                        [Up](monitoring.md){accesskey="U"}                              PostgreSQL statistics views
  ------------------------------------------------- -------------------------------------- -------------------------------------------------------
:::
