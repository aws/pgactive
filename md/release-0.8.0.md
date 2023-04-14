::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                            
  ----------------------------------------------------------- ---------------------------------------- --------------------------- ---------------------------------------------------------
  [Prev](release-0.9.0.md "Release 0.9.0"){accesskey="P"}   [Up](releasenotes.md){accesskey="U"}    Appendix A. Release notes    [Next](release-0.7.md "Release 0.7.0"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [A.14. Release 0.8.0]{#RELEASE-0.8.0} {#a.14.-release-0.8.0 .SECT1}

The 0.8.0 release was tagged on Fri Feb 6 2015 as git tag
`bdr-plugin/0.8.0`{.LITERAL}. This release requires bdr-Pg
`bdr-pg/REL9_4_1-1`{.LITERAL} to support full BDR functionality.

Upgrading to 0.8.0 from 0.7.x requires a [pg_dump]{.APPLICATION} and
[pg_restore]{.APPLICATION} or [pg_upgrade]{.APPLICATION} because the
on-disk format of the PostgreSQL database changed between 9.4beta2 and
9.4.0 final.

Significant features and improvements to [BDR]{.PRODUCTNAME} in this
release include:

-   UDR (Uni-Directional Replication)

-   Replication Sets

-   Global sequence performance improvements

-   Improvements to conflict handling

-   Many robustness and testing improvements

::: SECT2
## [A.14.1. Replication sets]{#RELEASE-0.8.0-REPLICATION-SETS} {#a.14.1.-replication-sets .SECT2}

Replication sets have been introduced. This new feature allows admins to
specify sets of tables that each node should receive changes on. It is
now possible to exclude tables that are not of interest to a particular
node, or to include only a subset of tables for replication to a node.
Replication sets can be used for data distribution, for data integration
and for limited sharding.
:::

::: SECT2
## [A.14.2. Global sequence performance improvements]{#RELEASE-0.8.0-GLOBAL-SEQUENCE-PERFORMANCE} {#a.14.2.-global-sequence-performance-improvements .SECT2}

The performance of global sequence voting has been greatly improved,
especially at higher node counts. It is now less likely for transactions
to fail because of global sequence exhaustion when the BDR group is
under significant write load.
:::

::: SECT2
## [A.14.3. DDL Replication improvements]{#RELEASE-0.8.0-DDL-REPLICATION} {#a.14.3.-ddl-replication-improvements .SECT2}

Many more DDL commands can be replicated. Fixes have been applied for
issues with replicating a few of the previously supported commands.

DDL commands that will cause full table rewrites are detected and
prohibited early.

Commands that are disallowed on normal tables are now permitted on temp
tables, including `CREATE TABLE ... AS SELECT ...`{.LITERAL}.
:::

::: SECT2
## [A.14.4. Conflict handling enhancements]{#RELEASE-0.8.0-CONFLICT-HANDLING} {#a.14.4.-conflict-handling-enhancements .SECT2}

User defined conflict handlers can now return replacement tuples for
`UPDATE`{.LITERAL}/`DELETE`{.LITERAL} conflicts.

User defined conflict handlers are invoked for
`DELETE`{.LITERAL}/`DELETE`{.LITERAL} conflicts.

Spurious conflicts are no longer logged after node initialisation.
:::

::: SECT2
## [A.14.5. Extension source code separation]{#RELEASE-0.8.0-SOURCE-SPLIT} {#a.14.5.-extension-source-code-separation .SECT2}

The BDR source code has been split into two parts: a set of patches to
PostgreSQL 9.4 and a separate PostgreSQL extension. This helps
streamline work on integrating the features BDR requires into core
PostgreSQL for releases 9.5 and onward.

The patched PostgreSQL is now tracked in git branches prefixed with
`bdr-pg/`{.FILENAME}, mainly `bdr-pg/REL9_4_STABLE`{.FILENAME}. The
extension is now tracked in git branches prefixed with
`bdr-plugin/`{.FILENAME}, mainly `bdr-plugin/RELX_Y_STABLE`{.FILENAME}
(stable releases) and `bdr-plugin/next`{.FILENAME} (current development
tree). All branches share the same working repository.

Prior releases of the BDR plugin were in the `contrib/bdr`{.FILENAME}
subdirectory of the patched PostgreSQL source tree instead.
:::

::: SECT2
## [A.14.6. Other changes]{#RELEASE-0.8.0-OTHER} {#a.14.6.-other-changes .SECT2}

-   Permit the \'bdr\' output plugin to be used from the SQL interface
    for logical replication. This is primarily useful for debugging.

-   Less memory is required to replay and apply large transactions.

-   The bdr_get_local_nodeid() function is available to return the local
    node\'s (sysid,timeline,dboid) tuple.

-   The `bdr_version_num()`{.LITERAL} and
    `bdr_min_remote_version_num()`{.LITERAL} functions were added. See
    [SQL functions](functions.md).
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------- ---------------------------------------- -----------------------------------------
  [Prev](release-0.9.0.md){accesskey="P"}      [Home](index.md){accesskey="H"}       [Next](release-0.7.md){accesskey="N"}
  Release 0.9.0                                [Up](releasenotes.md){accesskey="U"}                              Release 0.7.0
  ------------------------------------------- ---------------------------------------- -----------------------------------------
:::
