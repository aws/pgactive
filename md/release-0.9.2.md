::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                            
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-0.9.3.md "Release 0.9.3"){accesskey="P"}   [Up](releasenotes.md){accesskey="U"}    Appendix A. Release notes    [Next](release-0.9.1.md "Release 0.9.1"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [A.11. Release 0.9.2]{#RELEASE-0.9.2} {#a.11.-release-0.9.2 .SECT1}

Version 0.9.2 is a maintenance release focused on stability and
usability, specially in [bdr_init_copy]{.APPLICATION}.

Significant improvements to [BDR]{.PRODUCTNAME} in this release include:

-   New nodes created by [bdr_init_copy]{.APPLICATION} could re-use
    global sequence values (#101)

-   Permit DML on `pg_catalog`{.LITERAL} tables (#102)

-   Check exit code of utilities lauched by
    [bdr_init_copy]{.APPLICATION} (#100)

-   Ensure log locations are writeable before
    [bdr_init_copy]{.APPLICATION} (#99)

-   add `--replication_sets`{.LITERAL} option for
    [bdr_init_copy]{.APPLICATION}

-   sanity check existing data directory before doing
    [bdr_init_copy]{.APPLICATION} (#91)

-   handle multiple remote BDR databases correctly with
    [bdr_init_copy]{.APPLICATION} (#88)

-   set `node_local_dsn`{.LITERAL} and `node_name`{.LITERAL} correctly
    in [bdr_init_copy]{.APPLICATION}

-   [bdr_init_copy]{.APPLICATION} fixes for connection string parsing

-   be less strict about version string matching for
    [pg_dump]{.APPLICATION}, etc (#89, #75)

-   improve error message on apply failures caused by multiple unique
    indexes

-   Make sequence pernode cache configurable

There are no compatibility-affecting changes in this release.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-0.9.3.md){accesskey="P"}      [Home](index.md){accesskey="H"}       [Next](release-0.9.1.md){accesskey="N"}
  Release 0.9.3                                [Up](releasenotes.md){accesskey="U"}                                Release 0.9.1
  ------------------------------------------- ---------------------------------------- -------------------------------------------
:::
