  [BDR 2.0.7 Documentation](README.md)                                                                                            
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-2.0.5.md "Release 2.0.5")   [Up](releasenotes.md)    Appendix A. Release notes    [Next](release-2.0.3.md "Release 2.0.3")  


# A.2. Release 2.0.4

[*Tue Oct 24, 2017*]

Fixes and improvements:

-   Block use of DML in `bdr.bdr_replicate_ddl_command` and
    [update
    docs](functions-node-mgmt.md#FUNCTION-BDR-REPLICATE-DDL-COMMAND)

-   Fix memory leak when `bdr.trace_replay = on`

-   Fix crash on startup when restarted during DDL lock catchup state

-   Add new
    [`bdr.bdr_wait_for_slots_confirmed_flush_lsn`](functions-node-mgmt.md#FUNCTION-BDR-WAIT-FOR-SLOTS-CONFIRMED-FLUSH-LSN)
    function for node synchronisation

-   [Fix possible hang in apply
    worker](release-2.0.4.md#RELEASE-2.0.4-APPLYWORKER)

-   Track `local_commit_time` in
    [`bdr.bdr_conflict_history`](catalog-bdr-conflict-history.md)

-   Fix `apply_delay` for remote commit timestamps that are in
    the local node\'s future due to clock skew

-   Support compilation against PostgreSQL 10\'s `libpq`, for
    Debian derivative packaging

-   Show database name and application_name in logs for BDR workers

## A.2.1. Block use of DML in `bdr.bdr_replicate_ddl_command`

BDR 2.0.4 now blocks use of DML in
`bdr.bdr_replicate_ddl_command` to guard against operations
that could cause inconsistent data between nodes or cause replication to
stop.

It\'s almost never correct to put DML in
`bdr.replicate_ddl_command`. At best you\'ll get conflicts,
if your DML `UPDATE`s or `DELETE`s data in a table
using only immutable expressions.

More likely you\'ll get duplicate data if you do `INSERT`s
with synthetic keys, because it gets replicated once as a statement and
again as a decoded row change. The two rows might be different if you
use sequences, volatile functions, etc.

If your DML is mixed with DDL, you can break replication. For example:

``` PROGRAMLISTING
    bdr.bdr_replicate_ddl_command($DDL$
        ALTER TABLE t1 ADD COLUMN foo integer;
        UPDATE t1 SET foo = bar;
        ALTER TABLE t1 DROP COLUMN bar;
    $DDL$);
    
```

will break replication with the error

``` PROGRAMLISTING
    ERROR: data for dropped column
    
```

because BDR applies the whole compound SQL statement string from
`bdr.bdr_replicate_ddl_command`. Then it decodes the
`UPDATE`d rows, and tries to send them to the downstream. But
the downstream already applied the whole SQL statement string\... so the
target column \'bar\' doesn\'t exist anymore.

Replication will then fail with an error like

``` PROGRAMLISTING
    ERROR: 42P10: remote tuple has different column count to local table
    DETAIL: Table "public"."test" has 15 columns on local node (...) vs 16 on remote node (...)
    
```

or on older BDR versions:

``` PROGRAMLISTING
    ERROR: XX000: tuple natts mismatch, 15 vs 16
    
```

There\'s no safe way to handle this. If we suppressed logical decoding
of rows generated during a `bdr.bdr_replicate_ddl_command`,
we\'d introduce the issues seen in statement-based replication with
volatile functions etc instead. As if we were permitting full table
rewrites.

So 2.0.4 simply disallows DML in
`bdr.bdr_replicate_ddl_command`. It will now fail with a
message like:

``` PROGRAMLISTING
    ERROR:  row-data-modifying statements INSERT, UPDATE and DELETE are not permitted inside bdr.replicate_ddl_command
    HINT:  Split up scripts, putting DDL in bdr.replicate_ddl_command and DML as normal statements
    
```

## A.2.2. Fix possible hang in apply worker

An apply worker could get stuck while releasing a \"write\" mode global
DDL lock, causing replication to stop and the slot on the other end to
become inactive. The stuck worker would not respond to
`pg_terminate_backend` or `kill -TERM`. The other
end would report a network error or walsender timeout.

Recovery required a PostgreSQL shutdown with
`pg_ctl -m immediate stop`, killing of the stuck worker with
`SIGQUIT` or `SIGKILL` then starting PostgreSQL back
up.

If the underlying conditions that caused this issue are triggered, BDR
will now emit a log message like

``` PROGRAMLISTING
      WARNING: DDL LOCK TRACE: backend 1234 already registered as waiter for DDL lock release
    
```

and prevent the problem.



  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-2.0.5.md)      [Home](README.md)       [Next](release-2.0.3.md)  
  Release 2.0.5                                [Up](releasenotes.md)                                Release 2.0.3
  ------------------------------------------- ---------------------------------------- -------------------------------------------
