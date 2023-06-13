::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-2.0.0.md "Release 2.0.0"){accesskey="P"}   [Up](releasenotes.md){accesskey="U"}    Appendix A. Release notes    [Next](release-1.0.1.md "Release 1.0.1"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [A.7. Release 1.0.2]{#RELEASE-1.0.2} {#a.7.-release-1.0.2 .SECT1}

[*Mon Nov 14, 2016*]{.emphasis}

The BDR 1.0.2 maintenance release fixes an intermittent failure in
[bdr_init_copy]{.APPLICATION} caused by failure to wait until promotion
of a copied node has fully completed. (#255).

Also fixes failure to replicate the `ch_timeframe`{.LITERAL} argument to
peers when user defined conflict handlers are created.

Alongside the bdr-plugin 1.0.2 release, BDR-Postgres is updated to
9.4.10, including all fixes and improvements from PostgreSQL 9.4.10. See
[the PostgreSQL 9.4.10 release
notes](https://www.postgresql.org/docs/9.4/static/release-9-4-10.html).
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-2.0.0.md){accesskey="P"}      [Home](index.md){accesskey="H"}       [Next](release-1.0.1.md){accesskey="N"}
  Release 2.0.0                                [Up](releasenotes.md){accesskey="U"}                                Release 1.0.1
  ------------------------------------------- ---------------------------------------- -------------------------------------------
:::
