  [BDR 2.0.7 Documentation](README.md)                                                                                                    
  --------------------------------------------------------------- ------------------------------------ ----------------------------------- -----------------------------------------------------
  [Prev](commands.md "Command-line Utilities")   [Up](commands.md)    Chapter 6. Command-line Utilities    [Next](monitoring.md "Monitoring")  


# [6.1. bdr_init_copy]

The [bdr_init_copy] command is used to initialize a new
[BDR] node by making physical copy of an existing node and
establishing a connection to the node that the clone was made from.

Using [bdr_init_copy] will clone all databases from the
origin server. All BDR-enabled databases on the cloned peer will be
bought up for BDR on the downstream.

See [Node management](node-management.md) for more information about
node creation.

By default [bdr_init_copy] clones the source BDR node
using [pg_basebackup]. However, if the data directory
already contains a physical backup (clone) of the source database it
will be converted into a BDR node instead. The backup must be taken by
[pg_basebackup] or some other safe cloning method like
`pg_start_backup()` + [rsync] +
`pg_stop_backup()` + WAL archive replay or streaming. It
backup must have a `recovery.conf` that causes it to stream
changes from the upstream server\'s WAL archive via
`restore_command` or `primary_conninfo` so it can
continue to replay from the source node as a streaming replica when it
is started up. The backup must not already be running.
[bdr_init_copy] will override any existing recovery target
set in `recovery.conf`. See [the PostgreSQL
manual](https://www.postgresql.org/docs/current/static/continuous-archiving.html#BACKUP-LOWLEVEL-BASE-BACKUP)
for information on creating base backups.

`bdr_init_copy` \[*`option`*\...\]

## [6.1.1. Options]

The following command-line options control the behaviour of
[bdr_init_copy].


`-D `*`directory`*\
`--pgdata=`*`directory`*

    Data directory of the new node.

    This can be either a postgres data directory backed up from the
    source node (as discussed above) or an empty directory. In case of
    empty directory, the full backup of the source node will be made
    using [pg_basebackup].

`-n `*`nodename`*\
`--node-name=`*`nodename`*

    Name of the new node.

`--replication-sets=`*`sets`*

    Comma separated list of replication set names to use.

`-s`

    Stop the node after creation. The default behavior is to start the
    new node once it\'s setup.

`--postgresql-conf=`*`postgresql.conf`*

    Path to postgresql.conf file which will be used by the new node. If
    not specified, the postgresql.conf will be copied from the source
    node.

`--hba-conf=`*`hba.conf`*

    Path to hba.conf file which will be used by the new node. If not
    specified, the hba.conf will be copied from the source node.

`--recovery-conf=`*`recovery.conf`*

    Path to recovery.conf template file which will be used during the
    physical initialization of the node.

    This parameter is useful if you can\'t use streaming replication for
    initial synchronization and you want to use
    `archive_command` instead.

`--log-file=`*`bdr_init_copy_postgres.log`*

    Path to the log file that bdr_init_copy will write
    `postgres` output to when it starts temporary postgres
    instances during setup. Defaults to
    `bdr_init_copy_postgres.log` in the current directory.

`--apply-delay=`*`0`*

    This option is the same as the *`apply_delay`* option
    to
    [`bdr_group_join`](functions-node-mgmt.md#FUNCTION-BDR-GROUP-JOIN).
    It is mainly useful for testing - including crude latency
    simulation - and debugging.

The following command-line options specify the source node to connect
to.


`-d `*`dbname`*\
`--remote-dbname=`*`dbname`*

    Specifies the name of the database to connect to. This is equivalent
    to specifying *`dbname`* as the first non-option
    argument on the command line.

    If this parameter contains an `=` sign or starts with a
    valid URI prefix (`postgresql://` or
    `postgres://`), it is treated as a `conninfo`
    string.

`-h `*`host`*\
`--remote-host=`*`host`*

    Specifies the host name of the machine on which the server is
    running. If the value begins with a slash, it is used as the
    directory for the Unix domain socket. The default is taken from the
    `PGHOST` environment variable, if set, else a Unix domain
    socket connection is attempted.

`-p `*`port`*\
`--remote-port=`*`port`*

    Specifies the TCP port or local Unix domain socket file extension on
    which the server is listening for connections. Defaults to the
    `PGPORT` environment variable, if set, or a compiled-in
    default.

`-U `*`username`*\
`--remote-user=`*`username`*

    User name to connect as.

The following command-line options specify local connection to the newly
created node.


`--local-dbname=`*`dbname`*

    Specifies the name of the database to connect to. This is equivalent
    to specifying *`dbname`* as the first non-option
    argument on the command line.

    If this parameter contains an `=` sign or starts with a
    valid URI prefix (`postgresql://` or
    `postgres://`), it is treated as a `conninfo`
    string.

`--local-host=`*`host`*

    Specifies the host name of the machine on which the server is
    running. If the value begins with a slash, it is used as the
    directory for the Unix domain socket. The default is taken from the
    `PGHOST` environment variable, if set, else a Unix domain
    socket connection is attempted.

`--local-port=`*`port`*

    Specifies the TCP port or local Unix domain socket file extension on
    which the server is listening for connections. Defaults to the
    `PGPORT` environment variable, if set, or a compiled-in
    default.

    [bdr_init_copy] does [*not*] modify
    `postgresql.conf` on the new node to use this port. If it
    is different to the remote port (e.g. when local and remote nodes
    are on the same machine and would otherwise have conflicting ports),
    supply a modified configuration with `--postgresql-conf`.

`--local-user=`*`username`*

    User name to connect as.



  -------------------------------------- ------------------------------------ ----------------------------------------
  [Prev](commands.md)    [Home](README.md)     [Next](monitoring.md)  
  Command-line Utilities                  [Up](commands.md)                                Monitoring
  -------------------------------------- ------------------------------------ ----------------------------------------
