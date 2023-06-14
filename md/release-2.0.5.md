::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ---------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](releasenotes.md "Release notes"){accesskey="P"}   [Up](releasenotes.md){accesskey="U"}    Appendix A. Release notes    [Next](release-2.0.4.md "Release 2.0.4"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [A.1. Release 2.0.5]{#RELEASE-2.0.5} {#a.1.-release-2.0.5 .SECT1}

[*Thu Mar 8, 2018*]{.emphasis}

Fixes and improvements:

-   Work around a core postgres reorderbuffer corruption bug with a
    change in the BDR output plugin. See [the mailing list
    discussion](https://www.postgresql.org/message-id/CAMsr+YHdX=XECbZshDZ2CZNWGTyw-taYBnzqVfx4JzM4ExP5xg@mail.gmail.com){target="_top"}
    of the issue.

-   Ignore column-count mismatches if the mismatched columns are all
    nullable or dropped. This makes recovery from some kinds of operator
    errors simpler, and with sufficient care can be used to bypass DDL
    locking for adding new nullable columns to tables.

-   Fix possible deadlock in the apply worker in the
    `find_pkey_tuple()`{.LITERAL} function.

-   Be stricter about memory context handling and leak prevention during
    apply. In particular be careful not to fall back to
    `TopMemoryContext`{.LITERAL}. May help some possible memory leak
    issues.

-   Reset the apply worker memory context on every commit, not just when
    all messages have been consumed. May help with some possible memory
    leak issues.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------ ---------------------------------------- -------------------------------------------
  [Prev](releasenotes.md){accesskey="P"}      [Home](index.md){accesskey="H"}       [Next](release-2.0.4.md){accesskey="N"}
  Release notes                               [Up](releasenotes.md){accesskey="U"}                                Release 2.0.4
  ------------------------------------------ ---------------------------------------- -------------------------------------------
:::
