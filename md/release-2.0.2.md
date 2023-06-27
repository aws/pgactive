  [BDR 2.0.7 Documentation](README.md)                                                                                            
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-2.0.3.md "Release 2.0.3")   [Up](releasenotes.md)    Appendix A. Release notes    [Next](release-2.0.1.md "Release 2.0.1")  


# A.4. Release 2.0.2

[*Thu Jun 29, 2017*]

BDR 2.0.2 is a maintenance release for the 2.0 series.

Fixes and improvements:

-   Fix issue where COPY wasn\'t filtered by read-only mode

-   Allow `bdr.permit_unsafe_ddl_commands` to override
    read-only mode on a node

-   Fix join when postgis extension (or any other extension containing
    `INSERT`s in the extension script) is in use



  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-2.0.3.md)      [Home](README.md)       [Next](release-2.0.1.md)  
  Release 2.0.3                                [Up](releasenotes.md)                                Release 2.0.1
  ------------------------------------------- ---------------------------------------- -------------------------------------------
