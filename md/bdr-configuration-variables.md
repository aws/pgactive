::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                                                             
  ---------------------------------------------------------------------------------------- ------------------------------------ ----------------------------------- ---------------------------------------------------------------
  [Prev](settings-prerequisite.md "Prerequisite PostgreSQL parameters"){accesskey="P"}   [Up](settings.md){accesskey="U"}    Chapter 4. Configuration Settings    [Next](node-management.md "Node Management"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [4.2. BDR specific configuration variables]{#BDR-CONFIGURATION-VARIABLES} {#bdr-specific-configuration-variables .SECT1}

The BDR extension exposes a number of configuration parameters via
PostgreSQL\'s usual configuration mechanism. You can set these in the
same way as any other setting, via `postgresql.conf`{.LITERAL} or using
`ALTER SYSTEM`{.LITERAL}. Some variables can also be set per-user,
per-database or per-session, but most require a server reload or a full
server restart to take effect.

::: VARIABLELIST

[]{#GUC-BDR-CONFLICT-LOGGING-INCLUDE-TUPLES}`bdr.conflict_logging_include_tuples`{.VARNAME} (`boolean`{.TYPE})

:   Log whole tuples when logging BDR tuples. Requires a server reload
    to take effect.

[]{#GUC-BDR-LOG-CONFLICTS-TO-TABLE}`bdr.log_conflicts_to_table`{.VARNAME} (`boolean`{.TYPE})

:   This boolean option controls whether detected BDR conflicts get
    logged to the bdr.bdr_conflict_history table. See Conflict logging
    for details. Requires a server reload to take effect.

[]{#GUC-BDR-SYNCHRONOUS-COMMIT}`bdr.synchronous_commit`{.VARNAME} (`boolean`{.TYPE})

:   This boolean option controls whether the
    `synchronous_commit`{.VARNAME} setting in [BDR]{.PRODUCTNAME} apply
    workers is enabled. It defaults to `off`{.LITERAL}. If set to
    `off`{.LITERAL}, [BDR]{.PRODUCTNAME} apply workers will perform
    asynchronous commits, allowing [PostgreSQL]{.PRODUCTNAME} to
    considerably improve throughput for apply, at the cost of delaying
    sending of replay confirmations to the upstream.

    It it always is safe to have
    `bdr.synchronous_commit = off`{.LITERAL}. It\'ll never cause
    transactions to be lost or skipped. It [*only*]{.emphasis} controls
    how promptly replicated data is flushed to disk on the downstream
    node and confirmations are sent to the upstream node. If it\'s off
    (default), BDR delays sending replay flush confirmations for commits
    to the upstream until the needed commits get flushed to disk by an
    unrelated commit, checkpoint, or other periodic work. This usually
    doesn\'t matter, but if the upstream has this downstream listed in
    `synchronous_standby_names`{.LITERAL}, setting
    `bdr.synchronous_commit = off`{.LITERAL} on the downstream will
    cause synchronous commits on the upstream to take
    [*much*]{.emphasis} longer to report success to the client. So in
    this case you should set it to on.

    ::: NOTE
    > **Note:** Using `bdr.synchronous_commit = on`{.LITERAL} and
    > putting bdr nodes in `synchronous_standby_names`{.LITERAL} will
    > [*not*]{.emphasis} prevent the replication conflicts that arise
    > with multi-master use of BDR. There is still no locking between
    > nodes and no global snapshot management so concurrent transactions
    > on different nodes can still change the same tuple. Transactions
    > still only start to replicate after they commit on the upstream
    > node. Synchronous commit does [*not*]{.emphasis} make BDR an
    > always-consistent system. See the [Overview](overview.md).
    :::

[]{#GUC-TEMP-DUMP-DIRECTORY}`bdr.temp_dump_directory`{.VARNAME} (`string`{.TYPE})

:   Specifies the path to a temporary storage location, writable by the
    postgres user, that needs to have enough storage space to contain a
    complete dump of the a potentially cloned database.

    This setting is only used during initial bringup via logical copy.
    It is not used by [bdr_init_copy]{.APPLICATION}.

[]{#GUC-BDR-MAX-DDL-LOCK-DELAY}`bdr.max_ddl_lock_delay`{.VARNAME} (`milliseconds`{.TYPE})

:   Controls how long a DDL lock attempt can wait for concurrent write
    transactions to commit or roll back before it forcibly aborts them.
    `-1`{.LITERAL} (the default) uses the value of
    `max_standby_streaming_delay`{.LITERAL}. Can be set with time units
    like `'10s'`{.LITERAL}. See [DDL
    Locking](ddl-replication-advice.md#DDL-REPLICATION-LOCKING).

[]{#GUC-BDR-DDL-LOCK-TIMEOUT}`bdr.ddl_lock_timeout`{.VARNAME} (`milliseconds`{.TYPE})

:   Controls how long a DDL lock attempt can wait to acquire the lock.
    The default value `-1`{.LITERAL} (the default) uses the value of
    `lock_timeout`{.LITERAL}. Can be set with time units like
    `'10s'`{.LITERAL}. See [DDL
    Locking](ddl-replication-advice.md#DDL-REPLICATION-LOCKING). Note
    that once the DDL lock is acquired and the DDL operation begins this
    timer stops ticking; it doesn\'t limit the overall duration a DDL
    lock may be held, only how long a transaction can wait for one to be
    acquired. To limit overall duration use a
    `statement_timeout`{.LITERAL}.

[]{#GUC-BDR-PERMIT-DDL-LOCKING}`bdr.permit_ddl_locking`{.VARNAME} (`boolean`{.TYPE})

:   Allow sessions to run DDL commands that acquire the global DDL lock.
    See [DDL replication](ddl-replication.md) for details on the DDL
    lock. Setting this to off by default means that unintended DDL that
    can be disruptive to production is prevented.

[]{#GUC-BDR-TRACE-DDL-LOCKS-LEVEL}`bdr.trace_ddl_locks_level`{.VARNAME} (`boolean`{.TYPE})

:   Override the default debug log level for BDR DDL locking (used in
    [DDL replication](ddl-replication.md)) so that DDL-lock related
    messages are emitted at the LOG debug level instead. This can be
    used to trace DDL locking activity on the system without having to
    configure the extremely verbose DEBUG1 or DEBUG2 log levels for the
    whole server.

    In increasing order of verbosity, settings are `none`{.LITERAL},
    `statement`{.LITERAL}, `acquire_release`{.LITERAL},
    `peers`{.LITERAL} and `debug`{.LITERAL}. At ` none`{.LITERAL} level
    DDL lock messages are only emitted at DEBUG1 and lower server log
    levels. `statement`{.LITERAL} adds `LOG`{.LITERAL} output whenever a
    statement causes an attempt to acquire a DDL lock.
    `acquire_release`{.LITERAL} also records when the lock is actually
    acquired and when it\'s subsequently released, or if it\'s declined,
    and records when peer nodes apply a remote DDL lock.
    `peer`{.LITERAL} adds more detail about the negotiation between peer
    nodes for DDL locks, and `debug`{.LITERAL} forces everything
    DDL-lock-related to be logged at `LOG`{.LITERAL} level.

    Changes take effect on server configuration reload, a restart is not
    required.

    See also [Monitoring global DDL locks](monitoring-ddl-lock.md).
:::

::: SECT2
## [4.2.1. Less common or internal configuration variables]{#AEN783} {#less-common-or-internal-configuration-variables .SECT2}

::: VARIABLELIST

[]{#GUC-BDR-DEFAULT-APPLY-DELAY}`bdr.default_apply_delay`{.VARNAME} (`integer`{.TYPE})

:   Sets a default apply delay (in milliseconds) for all configured
    connections that don\'t have a explicitly configured apply delay in
    their `bdr.bdr_connections`{.LITERAL} entry as set at node create or
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

[]{#GUC-BDR-SKIP-DDL-LOCKING}`bdr.skip_ddl_locking`{.VARNAME} (`boolean`{.TYPE})

:   Only affects BDR. Prevents acquisiton of the the global DDL lock
    when executing DDL statement. This is mainly used internally, but
    can also be useful in other cases. This option can be set at any
    time, but only by superusers.

    ::: WARNING
      -------------------------------------------------------------------------------
      **Warning**
      Inconsiderate usage of this option easily allows to break replication setups.
      -------------------------------------------------------------------------------
    :::

[]{#GUC-BDR-PERMIT-UNSAFE-DDL-COMMANDS}`bdr.permit_unsafe_ddl_commands`{.VARNAME} (`boolean`{.TYPE})

:   Only affects BDR. Permits execution of schema changes that cannot
    safely be replicated and overrides the read-only status of a node.
    This is primarily used internally, but can also be used in other
    cases. This option can be set at any time, but only by superusers.

    ::: WARNING
      -------------------------------------------------------------------------------
      **Warning**
      Inconsiderate usage of this option easily allows to break replication setups.
      -------------------------------------------------------------------------------
    :::

[]{#GUC-BDR-SKIP-DDL-REPLICATION}`bdr.skip_ddl_replication`{.VARNAME} (`boolean`{.TYPE})

:   Only affects BDR. Skips replication of DDL changes made in a session
    where this option is set to other systems. This is primarily useful
    for BDR internal use, but also can be used for some intentional
    schema changes like adding a index only on some nodes. This option
    can be set at any time, but only by superusers.

    ::: WARNING
      -------------------------------------------------------------------------------
      **Warning**
      Inconsiderate usage of this option easily allows to break replication setups.
      -------------------------------------------------------------------------------
    :::

[]{#GUC-BDR-DO-NOT-REPLICATE}`bdr.do_not_replicate`{.VARNAME} (`boolean`{.TYPE})

:   This parameter is intended for internal use only. Changes made in a
    transaction with this parameter set will not be queued for
    replication to other nodes.

    ::: WARNING
      -------------------------------------------------------------------------------
      **Warning**
      Inconsiderate usage of this option easily allows to break replication setups.
      -------------------------------------------------------------------------------
    :::

[]{#GUC-BDR-DISCARD-MISMATCHED-ROW-ATTRIBUTES}`bdr.discard_mismatched_row_attributes`{.VARNAME} (`boolean`{.TYPE})

:   This parameter is intended for specialist use only. It is only
    useful when a problem has arisen where rows on the incoming
    replication stream have more columns than the local table, and the
    remote rows have non-null values in them. This setting overrides the
    error
    `cannot right-pad mismatched attributes; attno %u is missing in local table and remote row has non-null, non-dropped value for this attribute`{.LITERAL}
    and allows data divergence to arise to let replication continue. It
    is better to fix the schema mismatch locally instead with a
    non-replicated schema change, so the remote rows can apply.

    ::: WARNING
      -------------------------------------------------------------------------------
      **Warning**
      Inconsiderate usage of this option easily allows to break replication setups.
      -------------------------------------------------------------------------------
    :::

[]{#GUC-BDR-TRACE-REPLAY}`bdr.trace_replay`{.VARNAME} (`boolean`{.TYPE})

:   When `on`{.LITERAL}, emits a log message for each remote action
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
    > `VERBOSE_INSERT`{.LITERAL}, `VERBOSE_UPDATE`{.LITERAL} and
    > `VERBOSE_DELETE`{.LITERAL} defined if you want row values.
    :::

[]{#GUC-BDR-EXTRA-APPLY-CONNECTION-OPTIONS}`bdr.extra_apply_connection_options`{.VARNAME} (`boolean`{.TYPE})

:   Add connection parameters to all connections made by BDR nodes to
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
:::
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ---------------------------------------------------- ------------------------------------ ---------------------------------------------
  [Prev](settings-prerequisite.md){accesskey="P"}     [Home](index.md){accesskey="H"}     [Next](node-management.md){accesskey="N"}
  Prerequisite [PostgreSQL]{.PRODUCTNAME} parameters    [Up](settings.md){accesskey="U"}                                Node Management
  ---------------------------------------------------- ------------------------------------ ---------------------------------------------
:::
