::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ---------------------------------------------------------------------------------------------------------------------------------- ------------------------------------ ------------------------- -----------------------------------------------------------------------------------------------------
  [Prev](logical-vs-physical.md "Differences between logical (row level) and physical (block level) replication"){accesskey="P"}   [Up](overview.md){accesskey="U"}    Chapter 1. BDR overview    [Next](weak-coupled-multimaster.md "BDR: Weakly coupled multi-master replication"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [1.3. Differences between BDR and trigger-based replication]{#BDR-VS-TRIGGER-BASED} {#differences-between-bdr-and-trigger-based-replication .SECT1}

There are a number of trigger-based logical replication solutions for
PostgreSQL, including
[Londiste](https://wiki.postgresql.org/wiki/SkyTools){target="_top"},
[Slony-I](http://slony.info/){target="_top"} and
[Bucardo](https://bucardo.org/){target="_top"}. They\'re mature, fairly
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
supports multi-master on unmodified PostgreSQL 9.4. So while BDR offers
some significant benefits it won\'t suit every need and every workload.
Evaluate your needs carefully before choosing a technology.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  -------------------------------------------------------------------------------- ------------------------------------ ------------------------------------------------------
  [Prev](logical-vs-physical.md){accesskey="P"}                                   [Home](index.md){accesskey="H"}     [Next](weak-coupled-multimaster.md){accesskey="N"}
  Differences between logical (row level) and physical (block level) replication    [Up](overview.md){accesskey="U"}            BDR: Weakly coupled multi-master replication
  -------------------------------------------------------------------------------- ------------------------------------ ------------------------------------------------------
:::
