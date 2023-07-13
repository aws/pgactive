  [BDR 2.0.7 Documentation](README.md)                                                                                            
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-1.0.2.md "Release 1.0.2")   [Up](releasenotes.md)    Appendix A. Release notes    [Next](release-1.0.0.md "Release 1.0.0")  


# A.8. Release 1.0.1

The 1.0.1 maintenance release fixes a significant bug in
`bdr_join_group` caused by `bdr_dump` running parts
of the binary upgrade code that it shouldn\'t have. This could lead to
incorrect frozenxids among other issues.

A variety of minor documentation and message fixes are also
incorporated; see the git log.



  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-1.0.2.md)      [Home](README.md)       [Next](release-1.0.0.md)  
  Release 1.0.2                                [Up](releasenotes.md)                                Release 1.0.0
  ------------------------------------------- ---------------------------------------- -------------------------------------------
