::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  -------------------------------------------------------------------------------------------------------------------- -------------------------------------------- ------------------------------ -----------------------------------------------------------------
  [Prev](global-sequences-alternatives.md "Traditional approaches to sequences in distributed DBs"){accesskey="P"}   [Up](global-sequences.md){accesskey="U"}    Chapter 10. Global Sequences    [Next](replication-sets.md "Replication Sets"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [10.8. BDR 1.0 global sequences]{#GLOBAL-SEQUENCES-BDR10} {#bdr-1.0-global-sequences .SECT1}

BDR 1.0 provided a different implementatoin of global sequences. They
intercepted `nextval(...)`{.FUNCTION} function calls and negotiated
between nodes to ensure that values were unique across the whole BDR
group. They were created with the syntax
`CREATE SEQUENCE ... USING bdr`{.LITERAL} and used with
`nextval(...)`{.LITERAL} like any other sequence.

This funtionality relied on modifications to Postgres-BDR 9.4 that were
not included in PostgreSQL 9.6. So the feature is not available on 9.6.
It also tolerated extended network partitions poorly. New applications
should avoid using them.

The old implementation is retained in BDR 2.0 for applications migrating
from BDR 1.0 that are still on BDR-Postgres 9.4. For details on the old
global sequences implementation see the BDR 1.0 manual.

To find out whether you are using BDR 1.0 global sequences and if so
where, query:

``` PROGRAMLISTING
    SELECT oid::regclass
    FROM pg_class
    WHERE relkind = 'S' AND
          pg_class.relam = (SELECT oid FROM pg_seqam WHERE seqamname = 'bdr');

```

to list sequences. You can self-join on `pg_class.relowner`{.LITERAL} to
find the associated table for each sequence.

For upgrade and conversion advice see [Converting BDR 1.0 global
sequences](x4379.md#UPGRADE-20-CONVERT-10-GLOBAL-SEQUENCES).
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ----------------------------------------------------------- -------------------------------------------- ----------------------------------------------
  [Prev](global-sequences-alternatives.md){accesskey="P"}        [Home](index.md){accesskey="H"}         [Next](replication-sets.md){accesskey="N"}
  Traditional approaches to sequences in distributed DBs       [Up](global-sequences.md){accesskey="U"}                                Replication Sets
  ----------------------------------------------------------- -------------------------------------------- ----------------------------------------------
:::
