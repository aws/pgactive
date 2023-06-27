  [BDR 2.0.7 Documentation](README.md)                                                                                            
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-2.0.4.md "Release 2.0.4")   [Up](releasenotes.md)    Appendix A. Release notes    [Next](release-2.0.2.md "Release 2.0.2")  


# A.3. Release 2.0.3

[*Mon Jul 31, 2017*]

BDR 2.0.3 is a maintenance release for the 2.0 series

Fixes and improvements:

-   Permit `CREATE INDEX CONCURRENTLY` and
    `DROP INDEX CONCURRENTLY` as raw DDL when
    `bdr.skip_ddl_replication = on` is set. It is still not
    accepted via `bdr.bdr_replicate_ddl_command`.

-   Fix an infinite loop that could occur during cancellation of
    write-locks (looping in `BackendIdGetProc`)

-   Fix shmem detach when workers are paused

-   Support and document [bdr_init_copy] on a base backup
    pre-copied by rsync or other tools

-   Ensure that [bdr_init_copy] properly respects a
    pre-existing `recovery.conf`

-   Expand [bdr_init_copy] regression tests

-   Improve error message for \"tuple natts mismatch\" to identify
    affected table, ec

-   

-   

-   

-   



  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-2.0.4.md)      [Home](README.md)       [Next](release-2.0.2.md)  
  Release 2.0.4                                [Up](releasenotes.md)                                Release 2.0.2
  ------------------------------------------- ---------------------------------------- -------------------------------------------
