  [BDR 2.0.7 Documentation](README.md)                                                                                                    
  [Prev](settings.md "Configuration Settings")   [Up](settings.md)    Chapter 4. Configuration Settings    [Next](bdr-configuration-variables.md "BDR specific configuration variables")  


# 4.1. Prerequisite [PostgreSQL] parameters

BDR require certain [PostgreSQL] settings to be set to
appropriate values.


`max_worker_processes` (`integer`)

    For BDR this has to be set to a big enough value to have one worker
    per configured database, and one worker per connection.

    For more detailed information about this parameter consult the
    [PostgreSQL]
    [documentation](http://www.postgresql.org/docs/current/interactive/runtime-config-resource.html#GUC-MAX-WORKER-PROCESSES).

`max_replication_slots` (`integer`)

    For BDR this needs to be set big enough so that every connection to
    this node has a free replication slot.

    For more detailed information about this parameter consult the
    [PostgreSQL]
    [documentation](http://www.postgresql.org/docs/current/interactive/runtime-config-replication.html#GUC-MAX-REPLICATION-SLOTS).

`max_wal_senders` (`integer`)

    For BDR this needs to be set big enough so that every connection to
    this node has a free wal sender process.

    If a node also does streaming base backups and/or base backups using
    [pg_basebackup], the value needs to be big enough to
    accomodate both that and BDR.

    For more detailed information about this parameter consult the
    [PostgreSQL]
    [documentation](http://www.postgresql.org/docs/current/interactive/runtime-config-replication.html#GUC-MAX-WAL-SENDERS).

`shared_preload_libraries` (`string`)

    For BDR this parameter has to include `bdr` as one of the
    comma separated values. The parameter can only be changed at server
    start.

    For more detailed information about this parameter consult the
    [PostgreSQL]
    [documentation](http://www.postgresql.org/docs/current/interactive/runtime-config-client.html#GUC-SHARED-PRELOAD-LIBRARIES).

`track_commit_timestamp` (`bool`)

    To use BDR this parameter has to be set to `true`.

`wal_level` (`enum`)

    For BDR this parameter has to be set to `logical`.

    For more detailed information about this parameter consult the
    [PostgreSQL]
    [documentation](http://www.postgresql.org/docs/current/interactive/runtime-config-wal.html#GUC-WAL-LEVEL).

`default_sequenceam` (`string`)

    This option is deprecated. On PostgreSQL 9.6 it is not supported and
    may not be set. On BDR-Postgres 9.4 it can be used to activate the
    old global sequences implementation; see [Global
    sequences](global-sequences.md) and the BDR 1.0 documentation.



  -------------------------------------- ------------------------------------ ---------------------------------------------------------
  [Prev](settings.md)    [Home](README.md)     [Next](bdr-configuration-variables.md)  
  Configuration Settings                  [Up](settings.md)                       BDR specific configuration variables
  -------------------------------------- ------------------------------------ ---------------------------------------------------------
