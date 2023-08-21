  [BDR 2.1.0 Documentation](README.md)                                                                                                                             
  [Prev](settings-prerequisite.md "Prerequisite PostgreSQL parameters")   [Up](settings.md)    Chapter 4. Configuration Settings    [Next](node-management.md "Node Management")  


# 4.2. BDR specific configuration variables

The BDR extension exposes a number of configuration parameters via
PostgreSQL\'s usual configuration mechanism. You can set these in the
same way as any other setting, via `postgresql.conf` or using
`ALTER SYSTEM`. Some variables can also be set per-user,
per-database or per-session, but most require a server reload or a full
server restart to take effect.


`bdr.conflict_logging_include_tuples` (`boolean`)

    Log whole tuples when logging BDR tuples. Requires a server reload
    to take effect.

`bdr.log_conflicts_to_table` (`boolean`)

    This boolean option controls whether detected BDR conflicts get
    logged to the bdr.bdr_conflict_history table. See Conflict logging
    for details. Requires a server reload to take effect.

`bdr.synchronous_commit` (`boolean`)

    This boolean option controls whether the
    `synchronous_commit` setting in [BDR] apply
    workers is enabled. It defaults to `off`. If set to
    `off`, [BDR] apply workers will perform
    asynchronous commits, allowing [PostgreSQL] to
    considerably improve throughput for apply, at the cost of delaying
    sending of replay confirmations to the upstream.

    It it always is safe to have
    `bdr.synchronous_commit = off`. It\'ll never cause
    transactions to be lost or skipped. It [*only*] controls
    how promptly replicated data is flushed to disk on the downstream
    node and confirmations are sent to the upstream node. If it\'s off
    (default), BDR delays sending replay flush confirmations for commits
    to the upstream until the needed commits get flushed to disk by an
    unrelated commit, checkpoint, or other periodic work. This usually
    doesn\'t matter, but if the upstream has this downstream listed in
    `synchronous_standby_names`, setting
    `bdr.synchronous_commit = off` on the downstream will
    cause synchronous commits on the upstream to take
    [*much*] longer to report success to the client. So in
    this case you should set it to on.

    ::: NOTE
    > **Note:** Using `bdr.synchronous_commit = on` and
    > putting BDR nodes in `synchronous_standby_names` will
    > [*not*] prevent the replication conflicts that arise
    > with Active-Active use of BDR. There is still no locking between
    > nodes and no global snapshot management so concurrent transactions
    > on different nodes can still change the same tuple. Transactions
    > still only start to replicate after they commit on the upstream
    > node. Synchronous commit does [*not*] make BDR an
    > always-consistent system. See the [Overview](overview.md).
    :::

`bdr.temp_dump_directory` (`string`)

    Specifies the path to a temporary storage location, writable by the
    postgres user, that needs to have enough storage space to contain a
    complete dump of the a potentially cloned database.

    This setting is only used during initial bringup via logical copy.
    It is not used by [bdr_init_copy].

`bdr.max_ddl_lock_delay` (`milliseconds`)

    Controls how long a DDL lock attempt can wait for concurrent write
    transactions to commit or roll back before it forcibly aborts them.
    `-1` (the default) uses the value of
    `max_standby_streaming_delay`. Can be set with time units
    like `'10s'`. See [DDL
    Locking](ddl-replication-advice.md#DDL-REPLICATION-LOCKING).

`bdr.ddl_lock_timeout` (`milliseconds`)

    Controls how long a DDL lock attempt can wait to acquire the lock.
    The default value `-1` (the default) uses the value of
    `lock_timeout`. Can be set with time units like
    `'10s'`. See [DDL
    Locking](ddl-replication-advice.md#DDL-REPLICATION-LOCKING). Note
    that once the DDL lock is acquired and the DDL operation begins this
    timer stops ticking; it doesn\'t limit the overall duration a DDL
    lock may be held, only how long a transaction can wait for one to be
    acquired. To limit overall duration use a
    `statement_timeout`.

`bdr.trace_ddl_locks_level` (`boolean`)

    Override the default debug log level for BDR DDL locking (used in
    [DDL replication](ddl-replication.md)) so that DDL-lock related
    messages are emitted at the LOG debug level instead. This can be
    used to trace DDL locking activity on the system without having to
    configure the extremely verbose DEBUG1 or DEBUG2 log levels for the
    whole server.

    In increasing order of verbosity, settings are `none`,
    `statement`, `acquire_release`,
    `peers` and `debug`. At ` none` level
    DDL lock messages are only emitted at DEBUG1 and lower server log
    levels. `statement` adds `LOG` output whenever a
    statement causes an attempt to acquire a DDL lock.
    `acquire_release` also records when the lock is actually
    acquired and when it\'s subsequently released, or if it\'s declined,
    and records when peer nodes apply a remote DDL lock.
    `peer` adds more detail about the negotiation between peer
    nodes for DDL locks, and `debug` forces everything
    DDL-lock-related to be logged at `LOG` level.

    Changes take effect on server configuration reload, a restart is not
    required.

    See also [Monitoring global DDL locks](monitoring-ddl-lock.md).

## 4.2.1. Less common or internal configuration variables


`bdr.default_apply_delay` (`integer`)

    Sets a default apply delay (in milliseconds) for all configured
    connections that don\'t have a explicitly configured apply delay in
    their `bdr.bdr_connections` entry as set at node create or
    join time.

    BDR won\'t replay a transaction on peer nodes until at least the
    specified number of milliseconds have elapsed since it was
    committed.

    This is primarily useful to simulate a high latency network in a low
    latency testing environment, mainly to make it easier to create
    conflicts. For example, if node A and B both have a 500ms
    apply_delay set, then after INSERTing a value into a table on node
    A, you have at least 500ms to perform a conflicting INSERT on B.
    This parameter requires a server reload or restart of the apply
    workers to take effect.

`bdr.skip_ddl_replication` (`boolean`)

    Only affects BDR. Skips replication and apply of DDL changes.
    This is set to on by default so that a BDR node bevahes as a non BDR one by
    default.  This option can be changed globally or enabled locally
    (at the session level) but only by superusers.

    ::: WARNING
      **Warning**
      Inconsiderate usage of this option easily allows to break replication setups.
    :::

`bdr.do_not_replicate` (`boolean`)

    This parameter is intended for internal use only. Changes made in a
    transaction with this parameter set will not be queued for
    replication to other nodes.

    ::: WARNING
      **Warning**
      Inconsiderate usage of this option easily allows to break replication setups.
    :::

`bdr.discard_mismatched_row_attributes` (`boolean`)

    This parameter is intended for specialist use only. It is only
    useful when a problem has arisen where rows on the incoming
    replication stream have more columns than the local table, and the
    remote rows have non-null values in them. This setting overrides the
    error
    `cannot right-pad mismatched attributes; attno %u is missing in local table and remote row has non-null, non-dropped value for this attribute`
    and allows data divergence to arise to let replication continue. It
    is better to fix the schema mismatch locally instead with a
    non-replicated schema change, so the remote rows can apply.

    ::: WARNING
      **Warning**
      Inconsiderate usage of this option easily allows to break replication setups.
    :::

`bdr.trace_replay` (`boolean`)

    When `on`, emits a log message for each remote action
    processed by a BDR downstream apply worker. The message records the
    change type, the table affected, the number of changes since xact
    start, the xact\'s commit lsn, commit time, the upstream node and
    which node it was forwarded from if any. Queued DDL commands and
    table drops are also printed. The additional logging has a
    performance impact and should not be enabled when not required.

    Changes take effect on server configuration reload, a restart is not
    required.

    ::: NOTE
    > **Note:** Row field contents are not shown. Recompile BDR with
    > `VERBOSE_INSERT`, `VERBOSE_UPDATE` and
    > `VERBOSE_DELETE` defined if you want row values.
    :::

`bdr.extra_apply_connection_options` (`boolean`)

    Add connection parameters to all connections made by BDR nodes to
    their peers. This is useful for configuring keepalives, SSL modes,
    etc. Settings given in an individual node\'s configured connection
    string will override these options and BDR\'s built-in connection
    options. See [libpq connection
    strings](https://www.postgresql.org/docs/9.4/static/libpq-connect.html#LIBPQ-CONNSTRING).

    ::: NOTE
    > **Note:** BDR automatically sets a fallback application name and
    > enables more aggressive keepalives:
    >
    > ``` PROGRAMLISTING
    > connect_timeout=30
    > keepalives=1
    > keepalives_idle=20
    > keepalives_interval=20
    > keepalives_count=5
    >          
    > ```
    >
    > You may override these settings with this option, e.g.:
    >
    > ``` PROGRAMLISTING
    > bdr.extra_apply_connection_options = 'keepalives=0'
    > ```
    >
    > It is not recommended to turn keepalives off unless you are having
    > problems with apply of a large, long running transaction running
    > to completion on an erratic network.
    :::

    Changes take effect on server configuration reload, a restart is not
    required.

`bdr.init_node_parallel_jobs` (`int`)

    Sets the number of parallel jobs to be used by pg_dump and
    pg_restore performed while logical join of a node using the
    [bdr.bdr_join_group](functions-node-mgmt.md#FUNCTION-BDR-JOIN-GROUP)
    function.

    Changes take effect on server configuration reload, a restart is not
    required.

`bdr.max_nodes` (`int`)

    Sets maximum allowed nodes in a BDR group. A new node fails to join a BDR
    group if it has a different value for this parameter when compared with its
    upstream node.  An existing node can't start BDR workers if the parameter
    value doesn't match with its upstream node. Hence, users must ensure all
    BDR members have the same value for the parameter at any point of time.
    Default value for this parameter is 4, meaning, there can be maximum of 4
    nodes allowed in the BDR group at any point of time. Note that more members
    in a BDR group require more sophisticated monitoring and maintenance, so
    choose this parameter value wisely.

    Set this parameter either in configuration file or via ALTER SYSTEM SET
    command. Changes take effect on server restart.

`bdr.permit_node_identifier_getter_function_creation` (`boolean`)

    This parameter is intended for internal use only. When set BDR allows
    creation of BDR node identifier getter function.

    ::: WARNING
      **Warning**
      Inconsiderate usage of this option easily allows to break replication setups.
    :::
  ---------------------------------------------------- ------------------------------------ ---------------------------------------------
  [Prev](settings-prerequisite.md)     [Home](README.md)     [Next](node-management.md)  
  Prerequisite [PostgreSQL] parameters    [Up](settings.md)                                Node Management
  ---------------------------------------------------- ------------------------------------ ---------------------------------------------
