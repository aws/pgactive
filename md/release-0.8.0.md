  [BDR 2.0.7 Documentation](README.md)                                                                                            
  ----------------------------------------------------------- ---------------------------------------- --------------------------- ---------------------------------------------------------
  [Prev](release-0.9.0.md "Release 0.9.0")   [Up](releasenotes.md)    Appendix A. Release notes    [Next](release-0.7.md "Release 0.7.0")  


# A.14. Release 0.8.0

The 0.8.0 release was tagged on Fri Feb 6 2015 as git tag
`bdr-plugin/0.8.0`. This release requires bdr-Pg
`bdr-pg/REL9_4_1-1` to support full BDR functionality.

Upgrading to 0.8.0 from 0.7.x requires a [pg_dump] and
[pg_restore] or [pg_upgrade] because the
on-disk format of the PostgreSQL database changed between 9.4beta2 and
9.4.0 final.

Significant features and improvements to [BDR] in this
release include:

-   UDR (Uni-Directional Replication)

-   Replication Sets

-   Global sequence performance improvements

-   Improvements to conflict handling

-   Many robustness and testing improvements

## A.14.1. Replication sets

Replication sets have been introduced. This new feature allows admins to
specify sets of tables that each node should receive changes on. It is
now possible to exclude tables that are not of interest to a particular
node, or to include only a subset of tables for replication to a node.
Replication sets can be used for data distribution, for data integration
and for limited sharding.

## A.14.2. Global sequence performance improvements

The performance of global sequence voting has been greatly improved,
especially at higher node counts. It is now less likely for transactions
to fail because of global sequence exhaustion when the BDR group is
under significant write load.

## A.14.3. DDL Replication improvements

Many more DDL commands can be replicated. Fixes have been applied for
issues with replicating a few of the previously supported commands.

DDL commands that will cause full table rewrites are detected and
prohibited early.

Commands that are disallowed on normal tables are now permitted on temp
tables, including `CREATE TABLE ... AS SELECT ...`.

## A.14.4. Conflict handling enhancements

User defined conflict handlers can now return replacement tuples for
`UPDATE`/`DELETE` conflicts.

User defined conflict handlers are invoked for
`DELETE`/`DELETE` conflicts.

Spurious conflicts are no longer logged after node initialisation.

## A.14.5. Extension source code separation

The BDR source code has been split into two parts: a set of patches to
PostgreSQL 9.4 and a separate PostgreSQL extension. This helps
streamline work on integrating the features BDR requires into core
PostgreSQL for releases 9.5 and onward.

The patched PostgreSQL is now tracked in git branches prefixed with
`bdr-pg/`, mainly `bdr-pg/REL9_4_STABLE`. The
extension is now tracked in git branches prefixed with
`bdr-plugin/`, mainly `bdr-plugin/RELX_Y_STABLE`
(stable releases) and `bdr-plugin/next` (current development
tree). All branches share the same working repository.

Prior releases of the BDR plugin were in the `contrib/bdr`
subdirectory of the patched PostgreSQL source tree instead.

## A.14.6. Other changes

-   Permit the \'bdr\' output plugin to be used from the SQL interface
    for logical replication. This is primarily useful for debugging.

-   Less memory is required to replay and apply large transactions.

-   The bdr_get_local_nodeid() function is available to return the local
    node\'s (sysid,timeline,dboid) tuple.

-   The `bdr_version_num()` and
    `bdr_min_remote_version_num()` functions were added. See
    [SQL functions](functions.md).



  ------------------------------------------- ---------------------------------------- -----------------------------------------
  [Prev](release-0.9.0.md)      [Home](README.md)       [Next](release-0.7.md)  
  Release 0.9.0                                [Up](releasenotes.md)                              Release 0.7.0
  ------------------------------------------- ---------------------------------------- -----------------------------------------
