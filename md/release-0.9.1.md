  [BDR 2.0.7 Documentation](README.md)                                                                                            
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-0.9.2.md "Release 0.9.2")   [Up](releasenotes.md)    Appendix A. Release notes    [Next](release-0.9.0.md "Release 0.9.0")  


# [A.12. Release 0.9.1]

Version 0.9.1 is a maintenance release focused on stability and
performance.

> **Important:** There is a minor incompatible bug fix in this release.
> The direction of replication sets is reversed between nodes whose set
> memberships differ. Previously, if node A was a member of set x, and
> node B was a member of set y, then a table that is part of set x would
> be replicated from A=\>B but not B=\>A. That is reversed as of BDR
> 0.9.1 so that nodes receive data on tables that are part of their
> replication sets.

Significant improvements to [BDR] in this release include:

-   Fix direction of replication sets (see above)

-   Fix PK detection on inherited tables (BDR)

-   Fix bdr.bdr_replication_identifier table definition (UDR)

-   Don\'t acquire table locks in command filter (BDR/UDR)

-   Rename \'bdr\' reserved db to \'bdr_supervisordb\', prevent users
    from connecting, and protect against drop (BDR/UDR, #60)

-   Bug fixes in [bdr_init_copy] handling of connection
    strings (BDR/UDR, #61)

-   Fixes for UNLOGGED tables in the command filter (BDR/UDR, #44)

-   Permit fast shutdown when replication is paused (BDR/UDR, #58)

-   Introduce
    [bdr.permit_ddl_locking](bdr-configuration-variables.md#GUC-BDR-PERMIT-DDL-LOCKING)
    to help prevent unintended global DDL locks

-   Remove slots when parting a node (BDR, #73)

-   `bdr.table_set_replication_sets` no longer requires
    `permit_unsafe_ddl_commands` (UDR, #67)

-   Improve sequencer locking and pgstat reporting (BDR/UDR)

-   Add
    [bdr.bdr_apply_is_paused()](functions-node-mgmt.md#FUNCTION-BDR-APPLY-IS-PAUSED)
    information function

Where available a github bug number follows the release entry.
Additional details are available from the changelog in git.

Two changes add minor new functionality:

As of 0.9.1 it is now possible for sessions to
`SET bdr.permit_ddl_locking = false` to cause commands that
would require the global DDL lock to be acquired to instead fail with an
ERROR. Administrators may choose to set this in
`postgresql.conf` then have sessions that intend to do DDL
override it. See
[bdr.permit_ddl_locking](bdr-configuration-variables.md#GUC-BDR-PERMIT-DDL-LOCKING)
and [DDL replication](ddl-replication.md) for more information.

The new information function
[bdr.bdr_apply_is_paused()](functions-node-mgmt.md#FUNCTION-BDR-APPLY-IS-PAUSED)
can be called on a node to determine whether replay from peer nodes is
paused on that node.



  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-0.9.2.md)      [Home](README.md)       [Next](release-0.9.0.md)  
  Release 0.9.2                                [Up](releasenotes.md)                                Release 0.9.0
  ------------------------------------------- ---------------------------------------- -------------------------------------------
