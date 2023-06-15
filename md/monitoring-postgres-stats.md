::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ------------------------------------------------------------------------------ -------------------------------------- ----------------------- ---------------------------------------------------------------
  [Prev](monitoring-conflict-stats.md "Monitoring conflicts"){accesskey="P"}   [Up](monitoring.md){accesskey="U"}    Chapter 7. Monitoring    [Next](ddl-replication.md "DDL Replication"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [7.6. PostgreSQL statistics views]{#MONITORING-POSTGRES-STATS} {#postgresql-statistics-views .SECT1}

Statistics on table and index usage are updated normally by the
downstream master. This is essential for correct function of
[autovacuum](http://www.postgresql.org/docs/current/static/routine-vacuuming.html){target="_top"}.
If there are no local writes on the downstream master and stats have not
been reset these two views should show matching results between upstream
and downstream:

-   `pg_stat_user_tables`{.LITERAL}

-   `pg_statio_user_tables`{.LITERAL}

Since indexes are used to apply changes, the identifying indexes on
downstream side may appear more heavily used with workloads that perform
`UPDATE`{.LITERAL}s and `DELETE`{.LITERAL}s than non-identifying indexes
are.

The built-in index monitoring views are:

-   `pg_stat_user_indexes`{.LITERAL}

-   `pg_statio_user_indexes`{.LITERAL}

All these views are discussed in the [PostgreSQL documentation on the
statistics
views](http://www.postgresql.org/docs/current/static/monitoring-stats.html#MONITORING-STATS-VIEWS-TABLE){target="_top"}.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------------------- -------------------------------------- ---------------------------------------------
  [Prev](monitoring-conflict-stats.md){accesskey="P"}     [Home](index.md){accesskey="H"}      [Next](ddl-replication.md){accesskey="N"}
  Monitoring conflicts                                     [Up](monitoring.md){accesskey="U"}                                DDL Replication
  ------------------------------------------------------- -------------------------------------- ---------------------------------------------
:::
