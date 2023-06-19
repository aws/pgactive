  [BDR 2.0.7 Documentation](README.md)                                                                                
  [Prev](monitoring.md "Monitoring")   [Up](monitoring.md)    Chapter 7. Monitoring    [Next](monitoring-node-join-remove.md "Monitoring node join/removal")  


# [7.1. Why monitoring matters]

If one or more nodes are down in a BDR group then [DDL
locking](ddl-replication-advice.md#DDL-REPLICATION-LOCKING) for [DDL
replication](ddl-replication.md) will wait indefinitely or until
cancelled. DDL locking requires [*consensus*] across all
nodes, not just a quorum, so it must be able to reach all nodes. So
it\'s important to monitor for node outages, especially since a stuck
DDL locking attempt will cause all queries to wait until it fails or
completes.

Because DDL locking inserts messages into the replication stream, a node
that is extremely behind on replay will cause similar disruption to one
that is entirely down.

Protracted node outages can also cause disk space exhaustion, resulting
in other nodes rejecting writes or performing emergency shutdowns.
Because every node connects to every other node there is a replication
slot for every downstream peer node on each node. Replication slots
ensure that an upstream (sending) server will retain enough write-ahead
log (WAL) in `pg_xlog` to resume replay from point the
downstream peer (receiver) last replayed on that slot. If a peer stops
consuming data on a slot or falls increasingly behind on replay then the
server that has that slot will accumulate WAL until it runs out of disk
space on `pg_xlog`. This can happen even if the downstream
peer is online and replaying if it isn\'t able to receive and replay
changes as fast as the upstream node generates them. WAL archiving
cannot (yet) be used for logical replication, so the WAL segments must
remain in `pg_xlog` until all nodes are done with them.

A less significant side effect of a lagging peer node is that
`VACUUM` cannot remove old rows (deleted rows, or old versions
of updated rows) in `pg_catalog` tables or user-catalogs until
every replica has replayed up to the transaction that deleted or
replaced that row. This can be an issue in deployments that use lots of
temporary tables, as it can cause significant bloat in
`pg_class` and `pg_attribute`.

It is important to have automated monitoring in place to ensure that if
replication slots start falling badly behind the admin is alerted and
can take proactive action. BDR does not provide monitoring, but can be
integrated into tools like:

-   [collectd](https://collectd.org/)

-   [Munin](http://munin-monitoring.org/) or
    [Cacti](http://www.cacti.net/)

-   [Icinga](https://www.icinga.com/) or
    [Nagios](https://www.nagios.com/)

-   [Zabbix](http://www.zabbix.com/)

-   \... and numerous extensible commercial solutions

If there isn\'t a plugin available for your chosen platform, feel free
to [get in touch with
2ndQuadrant](http://2ndQuadrant.com).



  ---------------------------------------- -------------------------------------- ---------------------------------------------------------
  [Prev](monitoring.md)     [Home](README.md)      [Next](monitoring-node-join-remove.md)  
  Monitoring                                [Up](monitoring.md)                               Monitoring node join/removal
  ---------------------------------------- -------------------------------------- ---------------------------------------------------------
