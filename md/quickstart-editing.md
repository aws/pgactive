::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ---------------------------------------------------------------------------------------------------- -------------------------------------- ------------------------------ -------------------------------------------------------------------------------------------------------
  [Prev](quickstart-instances.md "Creating BDR-enabled PostgreSQL nodes/instances"){accesskey="P"}   [Up](quickstart.md){accesskey="U"}    Chapter 3. Quick-start guide    [Next](quickstart-starting.md "Starting the BDR-enabled PostgreSQL nodes/instances"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [3.2. Editing the configuration files to enable BDR]{#QUICKSTART-EDITING} {#editing-the-configuration-files-to-enable-bdr .SECT1}

Edit the postgresql.conf file for both nodes/instances:

``` PROGRAMLISTING
    shared_preload_libraries = 'bdr'
    wal_level = 'logical'
    track_commit_timestamp = on
    max_connections = 100
    max_wal_senders = 10
    max_replication_slots = 10
    # Make sure there are enough background worker slots for BDR to run
    max_worker_processes = 10

    # These aren't required, but are useful for diagnosing problems
    #log_error_verbosity = verbose
    #log_min_messages = debug1
    #log_line_prefix = 'd=%d p=%p a=%a%q '

    # Useful options for playing with conflicts
    #bdr.default_apply_delay=2000   # milliseconds
    #bdr.log_conflicts_to_table=on

```

Edit or uncomment authentication parameters to allow replication in the
pg_hba.conf file for both nodes/instances:

``` PROGRAMLISTING
    local   replication   postgres                  trust
    host    replication   postgres     127.0.0.1/32 trust
    host    replication   postgres     ::1/128      trust

```
:::

::: NAVFOOTER

------------------------------------------------------------------------

  -------------------------------------------------- -------------------------------------- -----------------------------------------------------
  [Prev](quickstart-instances.md){accesskey="P"}     [Home](index.md){accesskey="H"}          [Next](quickstart-starting.md){accesskey="N"}
  Creating BDR-enabled PostgreSQL nodes/instances     [Up](quickstart.md){accesskey="U"}    Starting the BDR-enabled PostgreSQL nodes/instances
  -------------------------------------------------- -------------------------------------- -----------------------------------------------------
:::
