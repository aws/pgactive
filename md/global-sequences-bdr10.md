  [BDR 2.0.7 Documentation](README.md)                                                                                                                                                            
  [Prev](global-sequences-alternatives.md "Traditional approaches to sequences in distributed DBs")   [Up](global-sequences.md)    Chapter 10. Global Sequences    [Next](replication-sets.md "Replication Sets")  


# [10.8. BDR 1.0 global sequences]

BDR 1.0 provided a different implementatoin of global sequences. They
intercepted `nextval(...)` function calls and negotiated
between nodes to ensure that values were unique across the whole BDR
group. They were created with the syntax
`CREATE SEQUENCE ... USING bdr` and used with
`nextval(...)` like any other sequence.

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

to list sequences. You can self-join on `pg_class.relowner` to
find the associated table for each sequence.

For upgrade and conversion advice see [Converting BDR 1.0 global
sequences](x4379.md#UPGRADE-20-CONVERT-10-GLOBAL-SEQUENCES).



  ----------------------------------------------------------- -------------------------------------------- ----------------------------------------------
  [Prev](global-sequences-alternatives.md)        [Home](README.md)         [Next](replication-sets.md)  
  Traditional approaches to sequences in distributed DBs       [Up](global-sequences.md)                                Replication Sets
  ----------------------------------------------------------- -------------------------------------------- ----------------------------------------------
