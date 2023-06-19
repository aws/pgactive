  [BDR 2.0.7 Documentation](README.md)                                                                                            
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-2.0.1.md "Release 2.0.1")   [Up](releasenotes.md)    Appendix A. Release notes    [Next](release-1.0.2.md "Release 1.0.2")  


# [A.6. Release 2.0.0]

[*Fri Jun 16, 2017*]

BDR 2.0 is a major update that brings compatibility with unmodified
community PostgreSQL 9.6, a new DDL replication model, a new
implementation of global sequences, and more.

It it [*crucial*] that BDR 1.0 users read the [documentation
on upgrading from BDR 1.0.](upgrade.md).

Notable release highlights are:

-   Compatibility with community PostgreSQL 9.6

-   Still compatible with Postgres-BDR 9.4 for existing users

-   Runs as an extension on PostgreSQL 9.6, no patched PostgreSQL
    required

-   New global sequences implementation that doesn\'t experience
    exhaustion under load or network partitions

-   New DDL replication implementation for PostgreSQL 9.6 compatibility.
    This brings a different set of limitations and benefits.

-   Retains compatibility with BDR-Postgres 9.4

-   Support for joining new nodes concurrently

There are some important compatibility changes between BDR 1.0 and 2.0
when run on PostgreSQL 9.6, due to functionality present in Postgres-BDR
9.4 that is not available in community PostgreSQL 9.6. Specifically:

-   Transparent DDL replication is not (yet) supported by BDR 2.0 on
    PostgreSQL 9.6. All DDL must be wrapped in
    [`bdr.bdr_replicate_ddl_command(...)`](functions-node-mgmt.md#FUNCTION-BDR-REPLICATE-DDL-COMMAND)
    calls.

-   BDR 1.0-style global sequences created with `USING bdr` or
    `default_sequenceam = 'bdr'` are [*not*]
    supported by BDR 2.0 on PostgreSQL 9.6. An [alternate global
    sequences implementation](global-sequences.md) is provided for BDR
    2.0. Users on Postgres-BDR 9.4 are also encouraged to switch to the
    new global sequences model to ease future upgrades and because the
    new model is more resilient against network partitions.

-   Nodes are now read-only while joining, and only become read/write
    once fully joined.

There is some more information about compatibility in [Upgrading
BDR](upgrade.md).



  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-2.0.1.md)      [Home](README.md)       [Next](release-1.0.2.md)  
  Release 2.0.1                                [Up](releasenotes.md)                                Release 1.0.2
  ------------------------------------------- ---------------------------------------- -------------------------------------------
