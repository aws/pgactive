  [BDR 2.0.7 Documentation](README.md)                                                                                                        
  [Prev](ddl-replication-statements.md "Statement specific DDL replication concerns")   [Up](manual.md)        [Next](conflicts-how.md "How conflicts happen")  


# []{#CONFLICTS}Chapter 9. Active-Active conflicts

**Table of Contents**

9.1. [How conflicts happen](conflicts-how.md)

9.2. [Types of conflict](conflicts-types.md)

9.2.1. [`PRIMARY KEY` or `UNIQUE`
conflicts](conflicts-types.md#CONFLICTS-KEY)

9.2.2. [Foreign Key Constraint
conflicts](conflicts-types.md#CONFLICTS-FOREIGN-KEY)

9.2.3. [Exclusion constraint
conflicts](conflicts-types.md#CONFLICTS-EXCLUSION)

9.2.4. [Global data conflicts](conflicts-types.md#AEN2387)

9.2.5. [Lock conflicts and deadlock
aborts](conflicts-types.md#AEN2408)

9.2.6. [Divergent conflicts](conflicts-types.md#CONFLICTS-DIVERGENT)

9.3. [Avoiding or tolerating conflicts](conflicts-avoidance.md)

9.4. [User defined conflict
handlers](conflicts-user-defined-handlers.md)

9.5. [Conflict logging](conflicts-logging.md)

In Active-Active use of [BDR] writes to the same or
related table(s) from multiple different nodes can result in data
conflicts.

Some clustering systems use distributed lock mechanisms to prevent
concurrent access to data. These can perform reasonably when servers are
very close but cannot support geographically distributed applications as
very low latency is critical for acceptable performance.

Distributed locking is essentially a pessimistic approach, whereas BDR
advocates an optimistic approach: avoid conflicts where possible but
allow some types of conflict to occur and and resolve them when they
arise.



  -------------------------------------------------------- ----------------------------------- -------------------------------------------
  [Prev](ddl-replication-statements.md)    [Home](README.md)    [Next](conflicts-how.md)  
  Statement specific DDL replication concerns               [Up](manual.md)                          How conflicts happen
  -------------------------------------------------------- ----------------------------------- -------------------------------------------
