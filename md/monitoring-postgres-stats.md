  [BDR 2.0.7 Documentation](README.md)                                                                                                         
  [Prev](monitoring-conflict-stats.md "Monitoring conflicts")   [Up](monitoring.md)    Chapter 7. Monitoring    [Next](ddl-replication.md "DDL Replication")  


# 7.6. PostgreSQL statistics views

Statistics on table and index usage are updated normally by the
downstream. This is essential for correct function of
[autovacuum](http://www.postgresql.org/docs/current/static/routine-vacuuming.html).
If there are no local writes on the downstream and stats have not been
reset these two views should show matching results between upstream and
downstream:

-   `pg_stat_user_tables`

-   `pg_statio_user_tables`

Since indexes are used to apply changes, the identifying indexes on
downstream side may appear more heavily used with workloads that perform
`UPDATE`s and `DELETE`s than non-identifying indexes
are.

The built-in index monitoring views are:

-   `pg_stat_user_indexes`

-   `pg_statio_user_indexes`

All these views are discussed in the [PostgreSQL documentation on the
statistics
views](http://www.postgresql.org/docs/current/static/monitoring-stats.html#MONITORING-STATS-VIEWS-TABLE).



  ------------------------------------------------------- -------------------------------------- ---------------------------------------------
  [Prev](monitoring-conflict-stats.md)     [Home](README.md)      [Next](ddl-replication.md)  
  Monitoring conflicts                                     [Up](monitoring.md)                                DDL Replication
  ------------------------------------------------------- -------------------------------------- ---------------------------------------------
