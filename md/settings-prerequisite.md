::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  --------------------------------------------------------------- ------------------------------------ ----------------------------------- ------------------------------------------------------------------------------------------------
  [Prev](settings.md "Configuration Settings"){accesskey="P"}   [Up](settings.md){accesskey="U"}    Chapter 4. Configuration Settings    [Next](bdr-configuration-variables.md "BDR specific configuration variables"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [4.1. Prerequisite [PostgreSQL]{.PRODUCTNAME} parameters]{#SETTINGS-PREREQUISITE} {#prerequisite-postgresql-parameters .SECT1}

BDR require certain [PostgreSQL]{.PRODUCTNAME} settings to be set to
appropriate values.

::: VARIABLELIST

[]{#GUC-MAX-WORKER-PROCESSES}`max_worker_processes`{.VARNAME} (`integer`{.TYPE})

:   For BDR this has to be set to a big enough value to have one worker
    per configured database, and one worker per connection.

    For more detailed information about this parameter consult the
    [PostgreSQL]{.PRODUCTNAME}
    [documentation](http://www.postgresql.org/docs/current/interactive/runtime-config-resource.html#GUC-MAX-WORKER-PROCESSES){target="_top"}.

[]{#GUC-MAX-REPLICATION-SLOTS}`max_replication_slots`{.VARNAME} (`integer`{.TYPE})

:   For BDR this needs to be set big enough so that every connection to
    this node has a free replication slot.

    For more detailed information about this parameter consult the
    [PostgreSQL]{.PRODUCTNAME}
    [documentation](http://www.postgresql.org/docs/current/interactive/runtime-config-replication.html#GUC-MAX-REPLICATION-SLOTS){target="_top"}.

[]{#GUC-MAX-WAL-SENDERS}`max_wal_senders`{.VARNAME} (`integer`{.TYPE})

:   For BDR this needs to be set big enough so that every connection to
    this node has a free wal sender process.

    If a node also does streaming base backups and/or base backups using
    [pg_basebackup]{.APPLICATION}, the value needs to be big enough to
    accomodate both that and BDR.

    For more detailed information about this parameter consult the
    [PostgreSQL]{.PRODUCTNAME}
    [documentation](http://www.postgresql.org/docs/current/interactive/runtime-config-replication.html#GUC-MAX-WAL-SENDERS){target="_top"}.

[]{#GUC-SHARED-PRELOAD-LIBRARIES}`shared_preload_libraries`{.VARNAME} (`string`{.TYPE})

:   For BDR this parameter has to include `bdr`{.LITERAL} as one of the
    comma separated values. The parameter can only be changed at server
    start.

    For more detailed information about this parameter consult the
    [PostgreSQL]{.PRODUCTNAME}
    [documentation](http://www.postgresql.org/docs/current/interactive/runtime-config-client.html#GUC-SHARED-PRELOAD-LIBRARIES){target="_top"}.

[]{#GUC-TRACK-COMMIT-TIMESTAMP}`track_commit_timestamp`{.VARNAME} (`bool`{.TYPE})

:   To use BDR this parameter has to be set to `true`{.LITERAL}.

[]{#GUC-WAL-LEVEL}`wal_level`{.VARNAME} (`enum`{.TYPE})

:   For BDR this parameter has to be set to `logical`{.LITERAL}.

    For more detailed information about this parameter consult the
    [PostgreSQL]{.PRODUCTNAME}
    [documentation](http://www.postgresql.org/docs/current/interactive/runtime-config-wal.html#GUC-WAL-LEVEL){target="_top"}.

[]{#GUC-DEFAULT-SEQUENCEAM}`default_sequenceam`{.VARNAME} (`string`{.TYPE})

:   This option is deprecated. On PostgreSQL 9.6 it is not supported and
    may not be set. On BDR-Postgres 9.4 it can be used to activate the
    old global sequences implementation; see [Global
    sequences](global-sequences.md) and the BDR 1.0 documentation.
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  -------------------------------------- ------------------------------------ ---------------------------------------------------------
  [Prev](settings.md){accesskey="P"}    [Home](index.md){accesskey="H"}     [Next](bdr-configuration-variables.md){accesskey="N"}
  Configuration Settings                  [Up](settings.md){accesskey="U"}                       BDR specific configuration variables
  -------------------------------------- ------------------------------------ ---------------------------------------------------------
:::
