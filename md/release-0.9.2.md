  [BDR 2.0.7 Documentation](README.md)                                                                                            
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-0.9.3.md "Release 0.9.3")   [Up](releasenotes.md)    Appendix A. Release notes    [Next](release-0.9.1.md "Release 0.9.1")  


# A.11. Release 0.9.2

Version 0.9.2 is a maintenance release focused on stability and
usability, specially in [bdr_init_copy].

Significant improvements to [BDR] in this release include:

-   New nodes created by [bdr_init_copy] could re-use
    global sequence values (#101)

-   Permit DML on `pg_catalog` tables (#102)

-   Check exit code of utilities lauched by
    [bdr_init_copy] (#100)

-   Ensure log locations are writeable before
    [bdr_init_copy] (#99)

-   add `--replication_sets` option for
    [bdr_init_copy]

-   sanity check existing data directory before doing
    [bdr_init_copy] (#91)

-   handle multiple remote BDR databases correctly with
    [bdr_init_copy] (#88)

-   set `node_local_dsn` and `node_name` correctly
    in [bdr_init_copy]

-   [bdr_init_copy] fixes for connection string parsing

-   be less strict about version string matching for
    [pg_dump], etc (#89, #75)

-   improve error message on apply failures caused by multiple unique
    indexes

-   Make sequence pernode cache configurable

There are no compatibility-affecting changes in this release.



  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-0.9.3.md)      [Home](README.md)       [Next](release-0.9.1.md)  
  Release 0.9.3                                [Up](releasenotes.md)                                Release 0.9.1
  ------------------------------------------- ---------------------------------------- -------------------------------------------
