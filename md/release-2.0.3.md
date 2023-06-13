::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-2.0.4.md "Release 2.0.4"){accesskey="P"}   [Up](releasenotes.md){accesskey="U"}    Appendix A. Release notes    [Next](release-2.0.2.md "Release 2.0.2"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [A.3. Release 2.0.3]{#RELEASE-2.0.3} {#a.3.-release-2.0.3 .SECT1}

[*Mon Jul 31, 2017*]{.emphasis}

BDR 2.0.3 is a maintenance release for the 2.0 series

Fixes and improvements:

-   Permit `CREATE INDEX CONCURRENTLY`{.LITERAL} and
    `DROP INDEX CONCURRENTLY`{.LITERAL} as raw DDL when
    `bdr.skip_ddl_replication = on`{.VARNAME} is set. It is still not
    accepted via `bdr.bdr_replicate_ddl_command`{.FUNCTION}.

-   Fix an infinite loop that could occur during cancellation of
    write-locks (looping in `BackendIdGetProc`{.FUNCTION})

-   Fix shmem detach when workers are paused

-   Support and document [bdr_init_copy]{.APPLICATION} on a base backup
    pre-copied by rsync or other tools

-   Ensure that [bdr_init_copy]{.APPLICATION} properly respects a
    pre-existing `recovery.conf`{.FILENAME}

-   Expand [bdr_init_copy]{.APPLICATION} regression tests

-   Improve error message for \"tuple natts mismatch\" to identify
    affected table, ec

-

-

-

-
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-2.0.4.md){accesskey="P"}      [Home](index.md){accesskey="H"}       [Next](release-2.0.2.md){accesskey="N"}
  Release 2.0.4                                [Up](releasenotes.md){accesskey="U"}                                Release 2.0.2
  ------------------------------------------- ---------------------------------------- -------------------------------------------
:::
