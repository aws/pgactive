::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                                                     
  ----------------------------------------------------------------------------- ------------------------------------------ -------------------------------- -----------------------------------------------------------------------------------------
  [Prev](catalog-bdr-queued-drops.md "bdr.bdr_queued_drops"){accesskey="P"}   [Up](catalogs-views.md){accesskey="U"}    Chapter 13. Catalogs and Views    [Next](catalog-bdr-sequence-elections.md "bdr.bdr_sequence_elections"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [13.12. bdr.bdr_sequence_values]{#CATALOG-BDR-SEQUENCE-VALUES} {#bdr.bdr_sequence_values .SECT1}

`bdr.bdr_sequence_values`{.LITERAL} is a BDR internal implementation
table used for [Global sequences](global-sequences.md). Do not modify
this table directly.

This table keeps track of which global sequence chunks (value ranges)
have been allocated to which nodes. It does [*not*]{.emphasis} show
which sequence chunks have been used up. (That knowledge is only kept
track of by the local sequence on a node its self in the opaque binary
`amdata`{.LITERAL} field of the sequence, so it\'s not really
accessible).

You should never need to access this table.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------------------ ------------------------------------------ ------------------------------------------------------------
  [Prev](catalog-bdr-queued-drops.md){accesskey="P"}       [Home](index.md){accesskey="H"}        [Next](catalog-bdr-sequence-elections.md){accesskey="N"}
  bdr.bdr_queued_drops                                    [Up](catalogs-views.md){accesskey="U"}                                    bdr.bdr_sequence_elections
  ------------------------------------------------------ ------------------------------------------ ------------------------------------------------------------
:::
