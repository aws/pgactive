  [BDR 2.1.0 Documentation](README.md)                                                                                                                   
  [Prev](monitoring-node-join-remove.md "Monitoring node join/removal")   [Up](monitoring.md)    Chapter 7. Monitoring    [Next](monitoring-ddl-lock.md "Monitoring global DDL locks")  


# 7.3. Monitoring replication peers

As outlined in [Why monitoring matters](monitoring-why.md) it is
important to monitor the state of peer nodes in a BDR group. There are
two main views used for this: `pg_stat_replication` to monitor
for actively replicating nodes, and `pg_replication_slots` to
monitor for replication slot progress.

## 7.3.1. Monitoring connected peers using pg_stat_replication

Administrators may query
[pg_catalog.pg_stat_replication](http://www.postgresql.org/docs/current/static/monitoring-stats.html#PG-STAT-REPLICATION-VIEW)
to monitor actively replicating connections. It shows the pid of the
local side of the connection (wal sender process), the application name
sent by the peer (for BDR, this is
`bdr (sysid,timeline,dboid,)`), and other status information:

``` PROGRAMLISTING
    SELECT * FROM pg_stat_replication;
      pid  | usesysid | usename |              application_name              | client_addr | client_hostname | client_port |         backend_start         | backend_xmin |   state   | sent_location | write_location | flush_location | replay_location | sync_priority | sync_state
    -------+----------+---------+--------------------------------------------+-------------+-----------------+-------------+-------------------------------+--------------+-----------+---------------+----------------+----------------+-----------------+---------------+------------
     29045 |    16385 | myadmin   | bdr (6127682459268878512,1,16386,):receive |             |                 |          -1 | 2015-03-18 21:03:28.717175+00 |              | streaming | 0/189D3B8     | 0/189D3B8      | 0/189D3B8      | 0/189D3B8       |             0 | async
     29082 |    16385 | myadmin   | bdr (6127682494973391064,1,16386,):receive |             |                 |          -1 | 2015-03-18 21:03:44.665272+00 |              | streaming | 0/189D3B8     | 0/189D3B8      | 0/189D3B8      | 0/189D3B8       |             0 | async
    
```

This view shows all active replication connections, not just those used
by BDR. You will see connections from physical streaming replicas, other
logical decoding solutions, etc here as well.

To tell how far behind a given active connection is, compare its
`flush_location` (the replay position up to which it has
committed its work) with the sending server\'s
`pg_current_xlog_insert_location()` using
`pg_xlog_location_diff`, e.g:

``` PROGRAMLISTING
     SELECT
       pg_xlog_location_diff(pg_current_xlog_insert_location(), flush_location) AS lag_bytes,
       pid, application_name
     FROM pg_stat_replication;
    
```

This query will show how much lag downstream servers have from the
upstream server you run the query on. You can\'t use this to see, from
the downstream server, how far it is behind an upstream it\'s receiving
from. Also, the query will show lag for all replication consumers,
including non-BDR ones. To show only BDR peers, append
`WHERE application_name LIKE 'bdr%'`.

  **Warning**
  `pg_stat_replication` does [*not*] show peers that have a slot but are not currently connected, even though such peers are still making the server retain WAL. It is important to monitor `pg_replication_slots` too.

There is not currently any facility to report how far behind a given
node is in elapsed seconds of wall-clock time. So you can\'t easily tell
that node *`X`* currently has data that is
*`n`* seconds older than the original data on node
*`Y`*. If this is an application requirement the
application should write periodic timestamp tick records to a table and
check how old the newest tick for a given node is on other nodes.

## 7.3.2. Monitoring replication slots

Information about replication slots (both logical and physical) is
available in the `pg_catalog.pg_replication_slots` view. This
view shows all slots, whether or not there is an active replication
connection using them. It looks like:

``` PROGRAMLISTING
    SELECT * FROM pg_replication_slots;
                    slot_name                | plugin | slot_type | datoid | database | active | active_pid | xmin | catalog_xmin | restart_lsn | confirmed_flush_lsn 
    -----------------------------------------+--------+-----------+--------+----------+--------+------------+------+--------------+-------------+---------------------
     bdr_16386_6127682459268878512_1_16386__ | bdr    | logical   |  16386 | bdrdemo  | t      |       4121 |      |          749 | 0/191B130   | 0/201E120
     bdr_16386_6127682494973391064_1_16386__ | bdr    | logical   |  16386 | bdrdemo  | t      |       4317 |      |          749 | 0/191B130   | 0/201E120
    (2 rows)
    
```

If a slot has `active = t` then there will be a corresponding
`pg_stat_replication` entry for the walsender process
connected to the slot.

This view shows only replication peers that use a slot. Physical
streaming replication connections that don\'t use slots will not show up
here, only in `pg_stat_replication`. BDR always uses slots so
all BDR peers will appear here.

If you want to see a combined view, you can query a join of the two:

``` PROGRAMLISTING
   SELECT *
    FROM pg_catalog.pg_stat_replication r
    FULL OUTER JOIN pg_catalog.pg_replication_slots s
                 ON r.pid = s.active_pid
    WHERE r.application_name IS NULL
       OR r.application_name LIKE 'bdr%';
    
```

This has the handy advantage of showing the replication slot name along
with details of the walsender backend using the slot.

To see how much extra WAL BDR slot is asking the server to keep, in
bytes, use a query like:

``` PROGRAMLISTING
    SELECT
      slot_name, database, active, active_pid
      pg_xlog_location_diff(pg_current_xlog_insert_location(), restart_lsn) AS retained_bytes
    FROM pg_catalog.pg_replication_slots
    WHERE plugin = 'bdr';
    
```

Retained WAL isn\'t additive; if you have three peers, who of which
require 500KB of WAL to be retained and one that requires 8MB, only 8MB
is retained. It\'s like a dynamic version of the
`wal_keep_segments` setting (or, in 9.5,
`min_wal_size`). So you need to monitor to make sure that the
[*largest*] amount of retained WAL doens\'t exhaust the free
space in `pg_xlog` on each node.

It is normal for `pg_replication_slots.restart_lsn` not to
advance as soon as `pg_stat_replication.flush_location`
advances on an active connection. The slot restat position does
[*not*] indicate how old the data you will see on a peer node
is.

`pg_replication_slots.confirmed_flush_lsn` is a better measure
of replication progress, since it shows the position of the last commit
the replica has written safely to disk. However, it will only advance
when a transaction [*completes*] replay and commits. If you
monitor only `confirmed_flush_lsn`, replication will seem to
stop making progress during transfer and apply of big transactions,
wheras `pg_stat_replication.write_location`\'s will continue
to advance.

## 7.3.3. Montitoring BDR workers

All BDR workers (except the supervisor) show up in the system view
[`pg_stat_activity`](https://www.postgresql.org/docs/current/static/monitoring-stats.html#PG-STAT-ACTIVITY-VIEW)
so this view offers some insight into the state of a BDR system. There
is always one *`nodename`*`:perdb` worker per
BDR node. Each connection to another node adds one
*`othernodename`*`:apply` entry for the local
apply worker receiving and applying changes from that remote node, and
one *`othernodename`*`:send` worker for the
local walsender the remote apply worker is connected to.



  --------------------------------------------------------- -------------------------------------- -------------------------------------------------
  [Prev](monitoring-node-join-remove.md)     [Home](README.md)      [Next](monitoring-ddl-lock.md)  
  Monitoring node join/removal                               [Up](monitoring.md)                        Monitoring global DDL locks
  --------------------------------------------------------- -------------------------------------- -------------------------------------------------
