  [BDR 2.0.7 Documentation](README.md)                                                                                                                                                             
  [Prev](logical-vs-physical.md "Differences between logical (row level) and physical (block level) replication")   [Up](overview.md)    Chapter 1. BDR overview    [Next](weak-coupled-activeactive.md "BDR: Weakly coupled Active-Active replication")  


# [1.3. Differences between BDR and trigger-based replication]

There are a number of trigger-based logical replication solutions for
PostgreSQL, including
[Londiste](https://wiki.postgresql.org/wiki/SkyTools),
[Slony-I](http://slony.info/) and
[Bucardo](https://bucardo.org/). They\'re mature, fairly
widely used and powerful, and like BDR they have the advantages (and
disadvantages) of logical replication.

As with the comparison with physical replication, BDR offers advantages
over trigger-based replication solutions but also has some downsides.

All trigger-based replication solutions suffer from inherent
write-amplification, where each write to the database produces a
corresponding write to a replication log table. Both the original write
and the write to the replication log get recorded in WAL as well as the
heap, so each write actually happens four times. By reading and
processing WAL for data to replicate BDR avoids this write
amplification, so writes to a BDR-replicated database only get written
twice - like any other durable write on PostgreSQL.

Trigger-based replication also requires an external dæmon process on the
sending and/or receiving side. BDR runs its management processes inside
PostgreSQL itself, so there\'s no separate replication process to
manage.

At the time of writing, systems like Londiste have a number of features
for which BDR has no corresponding capability. Londiste can synchronise
and compare tables between the publisher and subscriber. Slony-I
supports events and confirmations. Slony-I provides infrastructure for
handling DDL while also running on unmodified PostgreSQL 9.4. Bucardo
supports Active-Active on unmodified PostgreSQL 9.4. So while BDR offers
some significant benefits it won\'t suit every need and every workload.
Evaluate your needs carefully before choosing a technology.



  [Prev](logical-vs-physical.md)                                   [Home](README.md)     [Next](weak-coupled-activeactive.md)  
  Differences between logical (row level) and physical (block level) replication    [Up](overview.md)            BDR: Weakly coupled Active-Active replication
