::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-2.0.5.md "Release 2.0.5"){accesskey="P"}   [Up](releasenotes.md){accesskey="U"}    Appendix A. Release notes    [Next](release-2.0.3.md "Release 2.0.3"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [A.2. Release 2.0.4]{#RELEASE-2.0.4} {#a.2.-release-2.0.4 .SECT1}

[*Tue Oct 24, 2017*]{.emphasis}

Fixes and improvements:

-   Block use of DML in `bdr.bdr_replicate_ddl_command`{.LITERAL} and
    [update
    docs](functions-node-mgmt.md#FUNCTION-BDR-REPLICATE-DDL-COMMAND)

-   Fix memory leak when `bdr.trace_replay = on`{.LITERAL}

-   Fix crash on startup when restarted during DDL lock catchup state

-   Add new
    [`bdr.wait_slot_confirm_lsn`{.LITERAL}](functions-node-mgmt.md#FUNCTION-BDR-WAIT-SLOT-CONFIRM-LSN)
    function for node synchronisation

-   [Fix possible hang in apply
    worker](release-2.0.4.md#RELEASE-2.0.4-APPLYWORKER)

-   Track `local_commit_time`{.STRUCTFIELD} in
    [`bdr.bdr_conflict_history`{.LITERAL}](catalog-bdr-conflict-history.md)

-   Fix `apply_delay`{.LITERAL} for remote commit timestamps that are in
    the local node\'s future due to clock skew

-   Support compilation against PostgreSQL 10\'s `libpq`{.LITERAL}, for
    Debian derivative packaging

-   Show database name and application_name in logs for BDR workers

::: SECT2
## [A.2.1. Block use of DML in `bdr.bdr_replicate_ddl_command`{.LITERAL}]{#RELEASE-2.0.4-DML} {#a.2.1.-block-use-of-dml-in-bdr.bdr_replicate_ddl_command .SECT2}

BDR 2.0.4 now blocks use of DML in
`bdr.bdr_replicate_ddl_command`{.FUNCTION} to guard against operations
that could cause inconsistent data between nodes or cause replication to
stop.

It\'s almost never correct to put DML in
`bdr.replicate_ddl_command`{.FUNCTION}. At best you\'ll get conflicts,
if your DML `UPDATE`{.LITERAL}s or `DELETE`{.LITERAL}s data in a table
using only immutable expressions.

More likely you\'ll get duplicate data if you do `INSERT`{.LITERAL}s
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
`bdr.bdr_replicate_ddl_command`{.FUNCTION}. Then it decodes the
`UPDATE`{.LITERAL}d rows, and tries to send them to the downstream. But
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
of rows generated during a `bdr.bdr_replicate_ddl_command`{.FUNCTION},
we\'d introduce the issues seen in statement-based replication with
volatile functions etc instead. As if we were permitting full table
rewrites.

So 2.0.4 simply disallows DML in
`bdr.bdr_replicate_ddl_command`{.FUNCTION}. It will now fail with a
message like:

``` PROGRAMLISTING
    ERROR:  row-data-modifying statements INSERT, UPDATE and DELETE are not permitted inside bdr.replicate_ddl_command
    HINT:  Split up scripts, putting DDL in bdr.replicate_ddl_command and DML as normal statements

```
:::

::: SECT2
## [A.2.2. Fix possible hang in apply worker]{#RELEASE-2.0.4-APPLYWORKER} {#a.2.2.-fix-possible-hang-in-apply-worker .SECT2}

An apply worker could get stuck while releasing a \"write\" mode global
DDL lock, causing replication to stop and the slot on the other end to
become inactive. The stuck worker would not respond to
`pg_terminate_backend`{.LITERAL} or `kill -TERM`{.LITERAL}. The other
end would report a network error or walsender timeout.

Recovery required a PostgreSQL shutdown with
`pg_ctl -m immediate stop`{.LITERAL}, killing of the stuck worker with
`SIGQUIT`{.LITERAL} or `SIGKILL`{.LITERAL} then starting PostgreSQL back
up.

If the underlying conditions that caused this issue are triggered, BDR
will now emit a log message like

``` PROGRAMLISTING
      WARNING: DDL LOCK TRACE: backend 1234 already registered as waiter for DDL lock release

```

and prevent the problem.
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-2.0.5.md){accesskey="P"}      [Home](index.md){accesskey="H"}       [Next](release-2.0.3.md){accesskey="N"}
  Release 2.0.5                                [Up](releasenotes.md){accesskey="U"}                                Release 2.0.3
  ------------------------------------------- ---------------------------------------- -------------------------------------------
:::
