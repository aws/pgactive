  [BDR 2.0.7 Documentation](README.md)                                                                                                                     
  [Prev](catalog-bdr-queued-drops.md "bdr.bdr_queued_drops")   [Up](catalogs-views.md)    Chapter 13. Catalogs and Views    [Next](catalog-bdr-sequence-elections.md "bdr.bdr_sequence_elections")  


# [13.12. bdr.bdr_sequence_values]

`bdr.bdr_sequence_values` is a BDR internal implementation
table used for [Global sequences](global-sequences.md). Do not modify
this table directly.

This table keeps track of which global sequence chunks (value ranges)
have been allocated to which nodes. It does [*not*] show
which sequence chunks have been used up. (That knowledge is only kept
track of by the local sequence on a node its self in the opaque binary
`amdata` field of the sequence, so it\'s not really
accessible).

You should never need to access this table.



  ------------------------------------------------------ ------------------------------------------ ------------------------------------------------------------
  [Prev](catalog-bdr-queued-drops.md)       [Home](README.md)        [Next](catalog-bdr-sequence-elections.md)  
  bdr.bdr_queued_drops                                    [Up](catalogs-views.md)                                    bdr.bdr_sequence_elections
  ------------------------------------------------------ ------------------------------------------ ------------------------------------------------------------
