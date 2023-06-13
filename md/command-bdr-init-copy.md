::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  --------------------------------------------------------------- ------------------------------------ ----------------------------------- -------------------------------------------------------------------------
  [Prev](commands.md "Command-line Utilities"){accesskey="P"}   [Up](commands.md){accesskey="U"}    Chapter 6. Command-line Utilities    [Next](command-bdr-initial-load.md "bdr_initial_load"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [6.1. bdr_init_copy]{#COMMAND-BDR-INIT-COPY} {#bdr_init_copy .SECT1}

The [bdr_init_copy]{.APPLICATION} command is used to initialize a new
[BDR]{.PRODUCTNAME} node by making physical copy of an existing node and
establishing a connection to the node that the clone was made from.

Using [bdr_init_copy]{.APPLICATION} will clone all databases from the
origin server. All BDR-enabled databases on the cloned peer will be
bought up for BDR on the downstream.

See [Node management](node-management.md) for more information about
node creation.

By default [bdr_init_copy]{.APPLICATION} clones the source BDR node
using [pg_basebackup]{.APPLICATION}. However, if the data directory
already contains a physical backup (clone) of the source database it
will be converted into a BDR node instead. The backup must be taken by
[pg_basebackup]{.APPLICATION} or some other safe cloning method like
`pg_start_backup()`{.LITERAL} + [rsync]{.APPLICATION} +
`pg_stop_backup()`{.LITERAL} + WAL archive replay or streaming. It
backup must have a `recovery.conf`{.FILENAME} that causes it to stream
changes from the upstream server\'s WAL archive via
`restore_command`{.LITERAL} or `primary_conninfo`{.LITERAL} so it can
continue to replay from the source node as a streaming replica when it
is started up. The backup must not already be running.
[bdr_init_copy]{.APPLICATION} will override any existing recovery target
set in `recovery.conf`{.FILENAME}. See [the PostgreSQL
manual](https://www.postgresql.org/docs/current/static/continuous-archiving.html#BACKUP-LOWLEVEL-BASE-BACKUP)
for information on creating base backups.

`bdr_init_copy`{.COMMAND} \[*`option`{.REPLACEABLE}*\...\]

::: SECT2
## [6.1.1. Options]{#COMMANDS-BDR-INIT-COPY-OPTIONS} {#options .SECT2}

The following command-line options control the behaviour of
[bdr_init_copy]{.APPLICATION}.

::: VARIABLELIST

`-D `{.OPTION}*`directory`{.REPLACEABLE}*\
`--pgdata=`{.OPTION}*`directory`{.REPLACEABLE}*

:   Data directory of the new node.

    This can be either a postgres data directory backed up from the
    source node (as discussed above) or an empty directory. In case of
    empty directory, the full backup of the source node will be made
    using [pg_basebackup]{.APPLICATION}.

`-n `{.OPTION}*`nodename`{.REPLACEABLE}*\
`--node-name=`{.OPTION}*`nodename`{.REPLACEABLE}*

:   Name of the new node.

`--replication-sets=`{.OPTION}*`sets`{.REPLACEABLE}*

:   Comma separated list of replication set names to use.

`-s`{.OPTION}

:   Stop the node after creation. The default behavior is to start the
    new node once it\'s setup.

`--postgresql-conf=`{.OPTION}*`postgresql.conf`{.REPLACEABLE}*

:   Path to postgresql.conf file which will be used by the new node. If
    not specified, the postgresql.conf will be copied from the source
    node.

`--hba-conf=`{.OPTION}*`hba.conf`{.REPLACEABLE}*

:   Path to hba.conf file which will be used by the new node. If not
    specified, the hba.conf will be copied from the source node.

`--recovery-conf=`{.OPTION}*`recovery.conf`{.REPLACEABLE}*

:   Path to recovery.conf template file which will be used during the
    physical initialization of the node.

    This parameter is useful if you can\'t use streaming replication for
    initial synchronization and you want to use
    `archive_command`{.VARNAME} instead.

`--log-file=`{.OPTION}*`bdr_init_copy_postgres.log`{.REPLACEABLE}*

:   Path to the log file that bdr_init_copy will write
    `postgres`{.COMMAND} output to when it starts temporary postgres
    instances during setup. Defaults to
    `bdr_init_copy_postgres.log`{.LITERAL} in the current directory.

`--apply-delay=`{.OPTION}*`0`{.REPLACEABLE}*

:   This option is the same as the *`apply_delay`{.REPLACEABLE}* option
    to
    [`bdr_group_join`{.FUNCTION}](functions-node-mgmt.md#FUNCTION-BDR-GROUP-JOIN).
    It is mainly useful for testing - including crude latency
    simulation - and debugging.
:::

The following command-line options specify the source node to connect
to.

::: VARIABLELIST

`-d `{.OPTION}*`dbname`{.REPLACEABLE}*\
`--remote-dbname=`{.OPTION}*`dbname`{.REPLACEABLE}*

:   Specifies the name of the database to connect to. This is equivalent
    to specifying *`dbname`{.REPLACEABLE}* as the first non-option
    argument on the command line.

    If this parameter contains an `=`{.SYMBOL} sign or starts with a
    valid URI prefix (`postgresql://`{.LITERAL} or
    `postgres://`{.LITERAL}), it is treated as a `conninfo`{.PARAMETER}
    string.

`-h `{.OPTION}*`host`{.REPLACEABLE}*\
`--remote-host=`{.OPTION}*`host`{.REPLACEABLE}*

:   Specifies the host name of the machine on which the server is
    running. If the value begins with a slash, it is used as the
    directory for the Unix domain socket. The default is taken from the
    `PGHOST`{.ENVAR} environment variable, if set, else a Unix domain
    socket connection is attempted.

`-p `{.OPTION}*`port`{.REPLACEABLE}*\
`--remote-port=`{.OPTION}*`port`{.REPLACEABLE}*

:   Specifies the TCP port or local Unix domain socket file extension on
    which the server is listening for connections. Defaults to the
    `PGPORT`{.ENVAR} environment variable, if set, or a compiled-in
    default.

`-U `{.OPTION}*`username`{.REPLACEABLE}*\
`--remote-user=`{.OPTION}*`username`{.REPLACEABLE}*

:   User name to connect as.
:::

The following command-line options specify local connection to the newly
created node.

::: VARIABLELIST

`--local-dbname=`{.OPTION}*`dbname`{.REPLACEABLE}*

:   Specifies the name of the database to connect to. This is equivalent
    to specifying *`dbname`{.REPLACEABLE}* as the first non-option
    argument on the command line.

    If this parameter contains an `=`{.SYMBOL} sign or starts with a
    valid URI prefix (`postgresql://`{.LITERAL} or
    `postgres://`{.LITERAL}), it is treated as a `conninfo`{.PARAMETER}
    string.

`--local-host=`{.OPTION}*`host`{.REPLACEABLE}*

:   Specifies the host name of the machine on which the server is
    running. If the value begins with a slash, it is used as the
    directory for the Unix domain socket. The default is taken from the
    `PGHOST`{.ENVAR} environment variable, if set, else a Unix domain
    socket connection is attempted.

`--local-port=`{.OPTION}*`port`{.REPLACEABLE}*

:   Specifies the TCP port or local Unix domain socket file extension on
    which the server is listening for connections. Defaults to the
    `PGPORT`{.ENVAR} environment variable, if set, or a compiled-in
    default.

    [bdr_init_copy]{.APPLICATION} does [*not*]{.emphasis} modify
    `postgresql.conf`{.FILENAME} on the new node to use this port. If it
    is different to the remote port (e.g. when local and remote nodes
    are on the same machine and would otherwise have conflicting ports),
    supply a modified configuration with `--postgresql-conf`{.LITERAL}.

`--local-user=`{.OPTION}*`username`{.REPLACEABLE}*

:   User name to connect as.
:::
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  -------------------------------------- ------------------------------------ ------------------------------------------------------
  [Prev](commands.md){accesskey="P"}    [Home](index.md){accesskey="H"}     [Next](command-bdr-initial-load.md){accesskey="N"}
  Command-line Utilities                  [Up](commands.md){accesskey="U"}                                        bdr_initial_load
  -------------------------------------- ------------------------------------ ------------------------------------------------------
:::
