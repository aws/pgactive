::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ---------------------------------------------------------------------------------------- -------------------------------------- ----------------------- -------------------------------------------------------------------------------
  [Prev](monitoring-node-join-remove.md "Monitoring node join/removal"){accesskey="P"}   [Up](monitoring.md){accesskey="U"}    Chapter 7. Monitoring    [Next](monitoring-ddl-lock.md "Monitoring global DDL locks"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [7.3. Monitoring replication peers]{#MONITORING-PEERS} {#monitoring-replication-peers .SECT1}

As outlined in [Why monitoring matters](monitoring-why.md) it is
important to monitor the state of peer nodes in a BDR group. There are
two main views used for this: `pg_stat_replication`{.LITERAL} to monitor
for actively replicating nodes, and `pg_replication_slots`{.LITERAL} to
monitor for replication slot progress.

::: SECT2
## [7.3.1. Monitoring connected peers using pg_stat_replication]{#MONITORING-CONNECTIONS} {#monitoring-connected-peers-using-pg_stat_replication .SECT2}

Administrators may query
[pg_catalog.pg_stat_replication](http://www.postgresql.org/docs/current/static/monitoring-stats.html#PG-STAT-REPLICATION-VIEW){target="_top"}
to monitor actively replicating connections. It shows the pid of the
local side of the connection (wal sender process), the application name
sent by the peer (for BDR, this is
`bdr (sysid,timeline,dboid,)`{.LITERAL}), and other status information:

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
`flush_location`{.LITERAL} (the replay position up to which it has
committed its work) with the sending server\'s
`pg_current_xlog_insert_location()`{.LITERAL} using
`pg_xlog_location_diff`{.LITERAL}, e.g:

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
`WHERE application_name LIKE 'bdr%'`{.LITERAL}.

::: WARNING
  ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  **Warning**
  `pg_stat_replication`{.LITERAL} does [*not*]{.emphasis} show peers that have a slot but are not currently connected, even though such peers are still making the server retain WAL. It is important to monitor `pg_replication_slots`{.LITERAL} too.
  ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
:::

There is not currently any facility to report how far behind a given
node is in elapsed seconds of wall-clock time. So you can\'t easily tell
that node *`X`{.REPLACEABLE}* currently has data that is
*`n`{.REPLACEABLE}* seconds older than the original data on node
*`Y`{.REPLACEABLE}*. If this is an application requirement the
application should write periodic timestamp tick records to a table and
check how old the newest tick for a given node is on other nodes.
:::

::: SECT2
## [7.3.2. Monitoring replication slots]{#MONITORING-SLOTS} {#monitoring-replication-slots .SECT2}

Information about replication slots (both logical and physical) is
available in the `pg_catalog.pg_replication_slots`{.LITERAL} view. This
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

If a slot has `active = t`{.LITERAL} then there will be a corresponding
`pg_stat_replication`{.LITERAL} entry for the walsender process
connected to the slot.

This view shows only replication peers that use a slot. Physical
streaming replication connections that don\'t use slots will not show up
here, only in `pg_stat_replication`{.LITERAL}. BDR always uses slots so
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
`wal_keep_segments`{.LITERAL} setting (or, in 9.5,
`min_wal_size`{.LITERAL}). So you need to monitor to make sure that the
[*largest*]{.emphasis} amount of retained WAL doens\'t exhaust the free
space in `pg_xlog`{.FILENAME} on each node.

It is normal for `pg_replication_slots.restart_lsn`{.LITERAL} not to
advance as soon as `pg_stat_replication.flush_location`{.LITERAL}
advances on an active connection. The slot restat position does
[*not*]{.emphasis} indicate how old the data you will see on a peer node
is.

`pg_replication_slots.confirmed_flush_lsn`{.LITERAL} is a better measure
of replication progress, since it shows the position of the last commit
the replica has written safely to disk. However, it will only advance
when a transaction [*completes*]{.emphasis} replay and commits. If you
monitor only `confirmed_flush_lsn`{.LITERAL}, replication will seem to
stop making progress during transfer and apply of big transactions,
wheras `pg_stat_replication.write_location`{.LITERAL}\'s will continue
to advance.
:::

::: SECT2
## [7.3.3. Montitoring BDR workers]{#MONITORING-WORKERS} {#montitoring-bdr-workers .SECT2}

All BDR workers (except the supervisor) show up in the system view
[`pg_stat_activity`{.LITERAL}](https://www.postgresql.org/docs/current/static/monitoring-stats.html#PG-STAT-ACTIVITY-VIEW){target="_top"}
so this view offers some insight into the state of a BDR system. There
is always one *`nodename`{.REPLACEABLE}*`:perdb`{.LITERAL} worker per
BDR node. Each connection to another node adds one
*`othernodename`{.REPLACEABLE}*`:apply`{.LITERAL} entry for the local
apply worker receiving and applying changes from that remote node, and
one *`othernodename`{.REPLACEABLE}*`:send`{.LITERAL} worker for the
local walsender the remote apply worker is connected to.
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  --------------------------------------------------------- -------------------------------------- -------------------------------------------------
  [Prev](monitoring-node-join-remove.md){accesskey="P"}     [Home](index.md){accesskey="H"}      [Next](monitoring-ddl-lock.md){accesskey="N"}
  Monitoring node join/removal                               [Up](monitoring.md){accesskey="U"}                        Monitoring global DDL locks
  --------------------------------------------------------- -------------------------------------- -------------------------------------------------
:::
