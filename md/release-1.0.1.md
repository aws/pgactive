::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                            
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-1.0.2.md "Release 1.0.2"){accesskey="P"}   [Up](releasenotes.md){accesskey="U"}    Appendix A. Release notes    [Next](release-1.0.0.md "Release 1.0.0"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [A.8. Release 1.0.1]{#RELEASE-1.0.1} {#a.8.-release-1.0.1 .SECT1}

The 1.0.1 maintenance release fixes a significant bug in
`bdr_group_join`{.LITERAL} caused by `bdr_dump`{.LITERAL} running parts
of the binary upgrade code that it shouldn\'t have. This could lead to
incorrect frozenxids among other issues.

A variety of minor documentation and message fixes are also
incorporated; see the git log.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-1.0.2.md){accesskey="P"}      [Home](index.md){accesskey="H"}       [Next](release-1.0.0.md){accesskey="N"}
  Release 1.0.2                                [Up](releasenotes.md){accesskey="U"}                                Release 1.0.0
  ------------------------------------------- ---------------------------------------- -------------------------------------------
:::
