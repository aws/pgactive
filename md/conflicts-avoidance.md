  [BDR 2.1.0 Documentation](README.md)                                                                                                        
  [Prev](conflicts-types.md "Types of conflict")   [Up](conflicts.md)    Chapter 9. Active-Active conflicts    [Next](conflicts-user-defined-handlers.md "User defined conflict handlers")  


# 9.3. Avoiding or tolerating conflicts

In most cases appropriate application design can be used to avoid
conflicts and/or the application can be made tolerant of conflicts.

Conflicts can only happen if there are things happening at the same time
on multiple nodes, so the simplest way to avoid conflicts is to only
ever write to one node, or to only ever write to independent subsets of
the database on each node. For example, each node might have a separate
schema, and while they all exchange data with each other, writes are
only ever performed on the node that \"owns\" a given schema.

For `INSERT` vs `INSERT` conflicts, use of [Global
sequences](global-sequences.md) can completely prevent conflicts.

BDR users may sometimes find it useful to perform distributed locking at
the application level in cases where conflicts are not acceptable.

The best course of action is frequently to allow conflicts to occur and
design the application to work with [BDR]\'s conflict
resolution mechansisms to cope with the conflict. See [Types of
conflict](conflicts-types.md).



  --------------------------------------------- ------------------------------------- -------------------------------------------------------------
  [Prev](conflicts-types.md)     [Home](README.md)     [Next](conflicts-user-defined-handlers.md)  
  Types of conflict                              [Up](conflicts.md)                                 User defined conflict handlers
  --------------------------------------------- ------------------------------------- -------------------------------------------------------------
