  [BDR 2.0.7 Documentation](README.md)                                                                                           
  ---------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](releasenotes.md "Release notes")   [Up](releasenotes.md)    Appendix A. Release notes    [Next](release-2.0.4.md "Release 2.0.4")  


# [A.1. Release 2.0.5]

[*Thu Mar 8, 2018*]

Fixes and improvements:

-   Work around a core postgres reorderbuffer corruption bug with a
    change in the BDR output plugin. See [the mailing list
    discussion](https://www.postgresql.org/message-id/CAMsr+YHdX=XECbZshDZ2CZNWGTyw-taYBnzqVfx4JzM4ExP5xg@mail.gmail.com)
    of the issue.

-   Ignore column-count mismatches if the mismatched columns are all
    nullable or dropped. This makes recovery from some kinds of operator
    errors simpler, and with sufficient care can be used to bypass DDL
    locking for adding new nullable columns to tables.

-   Fix possible deadlock in the apply worker in the
    `find_pkey_tuple()` function.

-   Be stricter about memory context handling and leak prevention during
    apply. In particular be careful not to fall back to
    `TopMemoryContext`. May help some possible memory leak
    issues.

-   Reset the apply worker memory context on every commit, not just when
    all messages have been consumed. May help with some possible memory
    leak issues.



  ------------------------------------------ ---------------------------------------- -------------------------------------------
  [Prev](releasenotes.md)      [Home](README.md)       [Next](release-2.0.4.md)  
  Release notes                               [Up](releasenotes.md)                                Release 2.0.4
  ------------------------------------------ ---------------------------------------- -------------------------------------------
