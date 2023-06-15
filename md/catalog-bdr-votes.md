::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ----------------------------------------------------------------------------------------- ------------------------------------------ -------------------------------- -----------------------------------------------------
  [Prev](catalog-bdr-sequence-elections.md "bdr.bdr_sequence_elections"){accesskey="P"}   [Up](catalogs-views.md){accesskey="U"}    Chapter 13. Catalogs and Views    [Next](upgrade.md "Upgrading BDR"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [13.14. bdr.bdr_votes]{#CATALOG-BDR-VOTES} {#bdr.bdr_votes .SECT1}

`bdr.bdr_votes`{.LITERAL} is a BDR internal implementation table used
for [Global sequences](global-sequences.md). Do not modify this table
directly. It\'s used when making decisions about which new sequence
chunks to hand out to which nodes.

You should never need to access this table.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------------------------ ------------------------------------------ -------------------------------------
  [Prev](catalog-bdr-sequence-elections.md){accesskey="P"}       [Home](index.md){accesskey="H"}        [Next](upgrade.md){accesskey="N"}
  bdr.bdr_sequence_elections                                    [Up](catalogs-views.md){accesskey="U"}          Upgrading [BDR]{.PRODUCTNAME}
  ------------------------------------------------------------ ------------------------------------------ -------------------------------------
:::
