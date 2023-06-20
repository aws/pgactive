  [BDR 2.0.7 Documentation](README.md)                                                                                       
  [Prev](monitoring-postgres-stats.md "PostgreSQL statistics views")   [Up](manual.md)        [Next](ddl-replication-advice.md "Executing DDL on BDR systems")  


# Chapter 8. DDL Replication

**Table of Contents**

8.1. [Executing DDL on BDR systems](ddl-replication-advice.md)

8.1.1. [The DDL
lock](ddl-replication-advice.md#DDL-REPLICATION-LOCKING)

8.1.2. [Minimising the impact of
DDL](ddl-replication-advice.md#DDL-REPLICATION-SAFETY)

8.2. [Statement specific DDL replication
concerns](ddl-replication-statements.md)

8.2.1. [Statements with weaker DDL
locking](ddl-replication-statements.md#AEN1489)

8.2.2. [Not replicated DDL
statements](ddl-replication-statements.md#AEN1519)

8.2.3. [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS)

8.2.4. [DDL statements with
restrictions](ddl-replication-statements.md#DDL-REPLICATION-RESTRICTED-COMMANDS)

8.2.5. [How to work around restricted
DDL](ddl-replication-statements.md#DDL-REPLICATION-HOW)

[BDR] supports replicating changes to a database\'s
schemas to other connected nodes. That makes it easier to make certain
DDL changes without worrying about having to manually distribute the DDL
change to all nodes and ensure they\'re consistent.

There is not currently an option to turn off DDL replication and apply
DDL manually instead.

Before doing DDL on [BDR], read [Section
8.1](ddl-replication-advice.md) and [Statement specific DDL
replication concerns](ddl-replication-statements.md).

[BDR] is significantly different to standalone PostgreSQL
when it comes to DDL and schema changes, and treating it as the same is
a fast path to replication problems.

> **Important:** While DDL is in progress on any node in the system,
> statements that perform writes (`INSERT`, `UPDATE`
> `DELETE`, any DDL, etc) on that node or any other node will
> `ERROR` even if the writes have nothing to do with the
> objects currently being modified by the DDL in progress.



  ------------------------------------------------------- ----------------------------------- ----------------------------------------------------
  [Prev](monitoring-postgres-stats.md)    [Home](README.md)    [Next](ddl-replication-advice.md)  
  PostgreSQL statistics views                              [Up](manual.md)                           Executing DDL on BDR systems
  ------------------------------------------------------- ----------------------------------- ----------------------------------------------------
