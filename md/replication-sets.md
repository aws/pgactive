  [BDR 2.0.7 Documentation](README.md)                                                                                 
  [Prev](global-sequences-bdr10.md "BDR 1.0 global sequences")   [Up](manual.md)        [Next](replication-sets-concepts.md "Replication Set Concepts")  


# Chapter 11. Replication Sets

**Table of Contents**

11.1. [Replication Set Concepts](replication-sets-concepts.md)

11.2. [Creating replication sets](replication-sets-creation.md)

11.3. [Node Replication Control](replication-sets-nodes.md)

11.4. [Table Replication Control](replication-sets-tables.md)

11.5. [Change-type replication sets](replication-sets-changetype.md)

Sometimes it is not desirable to replicate all changes that happen in a
database to all other nodes in a BDR group. E.g. it might not be
convenient and efficient to replicate a table containing session data.

In simple cases, application developers may simply use
`UNLOGGED` tables (a stock PostgreSQL feature). Since such
tables don\'t generate write-ahead-log (WAL) activity, they are
completely invisible to logical decoding and thus to BDR. No changes to
unlogged tables are replicated.

Where more complex arrangements are needed, replication sets are
available.



  ---------------------------------------------------- ----------------------------------- -------------------------------------------------------
  [Prev](global-sequences-bdr10.md)    [Home](README.md)    [Next](replication-sets-concepts.md)  
  BDR 1.0 global sequences                              [Up](manual.md)                                  Replication Set Concepts
  ---------------------------------------------------- ----------------------------------- -------------------------------------------------------
