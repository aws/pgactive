::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                            
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-2.0.3.md "Release 2.0.3"){accesskey="P"}   [Up](releasenotes.md){accesskey="U"}    Appendix A. Release notes    [Next](release-2.0.1.md "Release 2.0.1"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [A.4. Release 2.0.2]{#RELEASE-2.0.2} {#a.4.-release-2.0.2 .SECT1}

[*Thu Jun 29, 2017*]{.emphasis}

BDR 2.0.2 is a maintenance release for the 2.0 series.

Fixes and improvements:

-   Fix issue where COPY wasn\'t filtered by read-only mode

-   Allow `bdr.permit_unsafe_ddl_commands`{.LITERAL} to override
    read-only mode on a node

-   Fix join when postgis extension (or any other extension containing
    `INSERT`{.LITERAL}s in the extension script) is in use
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-2.0.3.md){accesskey="P"}      [Home](index.md){accesskey="H"}       [Next](release-2.0.1.md){accesskey="N"}
  Release 2.0.3                                [Up](releasenotes.md){accesskey="U"}                                Release 2.0.1
  ------------------------------------------- ---------------------------------------- -------------------------------------------
:::
