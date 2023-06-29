  [BDR 2.0.7 Documentation](README.md)                                                                                                                   
  [Prev](node-management-rejoining.md "Rejoining a BDR node")   [Up](node-management.md)    Chapter 5. Node Management    [Next](commands.md "Command-line Utilities")  


# 5.5. n-safe synchronous replication

BDR can be configured to use PostgreSQL\'s 9.6+\'s underlying n-safe
synchronous replication support. Each node may have a priority-ordered
of other nodes set in
[`synchronous_standby_names`](https://www.postgresql.org/docs/current/static/runtime-config-replication.html#GUC-SYNCHRONOUS-STANDBY-NAMES)
along with the minimum number that must confirm replay before the commit
is accepted on the upstream. PostgreSQL will delay confirmation of
`COMMIT` to the client until the highest-priority
currently-connected node on the list has confirmed that the commit has
been replayed and locally flushed.

When using Postgres-BDR 9.4, only 1-safe synchronous replication is
supported using the simple list-of-standby-names syntax for
`synchronous_standby_names`.

The
[`application_name`](https://www.postgresql.org/docs/current/static/runtime-config-logging.html#GUC-APPLICATION-NAME)
of each BDR apply worker\'s connection to its upstream nodes is
*`nodename`*`:send`. This is what appears in
`pg_stat_activity` for connections from peers and what\'s used
in `synchronous_standby_names`. The node name must be
`"`double quoted`"` for use in
`synchronous_standby_names`.

> **Note:** BDR 1.0 can also support synchronous replication, but the
> node connection strings in `bdr.bdr_connections` must
> manually be amended to add
> `application_name=`*`nodename`*`:send`
> (replacing *`nodename`* with the actual node name).

A typical configuration is 4 nodes arranged in two mutually synchronous
1-safe pairs. If the nodes names are A, B, C and D and we want A to be
synchronous with B and vice versa, and C to be synchronous with D and
vice versa, each node\'s configuration would be:

``` PROGRAMLISTING
   # on node A:
   synchronous_standby_names = '1 ("B:send")'
   bdr.synchronous_commit = on
   # on node B:
   synchronous_standby_names = '1 ("A:send")'
   bdr.synchronous_commit = on
   # on node C:
   synchronous_standby_names = '1 ("D:send")'
   bdr.synchronous_commit = on
   # on node D:
   synchronous_standby_names = '1 ("C:send")'
   bdr.synchronous_commit = on
   
```

With this configuration, commits on A will hang indefinitely if B goes
down or vice versa. If this is not desired, each node can use the other
nodes as secondary synchronous options (possibly with higher latency
over a WAN), e.g.

``` PROGRAMLISTING
   # on node A, prefer sync rep to B, but if B is down allow COMMIT
   # confirmation if either C or D are reachable and caught up:
   synchronous_standby_names = '1 ("B:send","C:send","D:send")'
   
```

If confirmation from all three other nodes is required before local
commit, use 3-safe:

``` PROGRAMLISTING
   # Require that B, C and D all confirm commit replay before local commit
   # on A becomes visible.
   synchronous_standby_names = '3 ("B:send","C:send","D:send")'
   
```

See [the PostgreSQL manual on synchronous
replication](https://www.postgresql.org/docs/current/static/warm-standby.html#SYNCHRONOUS-REPLICATION)
for a discussion of how synchronous replication works in PostgreSQL.
Most of the same principles apply when the other end is a BDR node not a
physical standby.

> **Note:** PostgreSQL\'s synchronous replication commits on the
> upstream before replicating to the downstream(s), it just hides the
> commit from other concurrent transactions until the downstreams
> complete. If the upstream is restarted the hidden commit(s) become
> visible even if the downstreams have not replied yet, so node restarts
> effectively momentarily disable synchronous replication.

It\'s generally a good idea to set
[`bdr.synchronous_commit = on`](bdr-configuration-variables.md#GUC-BDR-SYNCHRONOUS-COMMIT)
on all peers listed in `synchronous_standby_names` if using
synchronous replication, since this speeds up acknowledgement of commits
by peers and thus helps `COMMIT` return with minimal delay.

To reduce the delay in `COMMIT` acknowledgement and increase
throughput, users may wish to run unimportant transactions with

``` PROGRAMLISTING
     SET LOCAL synchronous_commit = off;
   
```

This effectively disables synchronous replication for individual
transactions.

Unlike PostgreSQL\'s physical replication, logical decoding (and
therefore BDR) cannot begin replicating a transaction to peer nodes
until it has committed on the originating node. This means that large
transactions can be subject to long delays on `COMMIT` when
synchronous replication is in use. Even if large transactions are run
with `synchronous_commit = off` they may delay commit
confirmation for small synchronous transactions that commit after the
big transactions because logical decoding processes transactions in
strict commit-order.

Even if synchronous replication is enabled, conflicts are still possible
even in a 2-node mutually synchronous configuration since no inter-node
locking is performed.



  ------------------------------------------------------- ------------------------------------------- --------------------------------------
  [Prev](node-management-rejoining.md)        [Home](README.md)        [Next](commands.md)  
  Rejoining a BDR node                                     [Up](node-management.md)                  Command-line Utilities
  ------------------------------------------------------- ------------------------------------------- --------------------------------------
