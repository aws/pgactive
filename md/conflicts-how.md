  [BDR 2.0.7 Documentation](README.md)                                                                                                        
  ----------------------------------------------------------------- ------------------------------------- ------------------------------------ -----------------------------------------------------------------
  [Prev](conflicts.md "Active-Active conflicts")   [Up](conflicts.md)    Chapter 9. Active-Active conflicts    [Next](conflicts-types.md "Types of conflict")  


# 9.1. How conflicts happen

Inter-node conflicts arise as a result of sequences of events that could
not happen if all the involved transactions happened concurrently on the
same node. Because the nodes only exchange changes after transactions
commit, each transaction is individually valid on the node it committed
on but would not be valid if run on another node that has done other
work in the mean time. Since [BDR] apply essentially
replays the transaction on the other nodes, the replay operation can
fail if there is a conflict between a transaction being applied and a
transaction that was committed on the receiving node.

The reason most conflicts can\'t happen when all transactions run on a
single node is that PostgreSQL has inter-transaction communication
mechanisms to prevent it - `UNIQUE` indexes,
`SEQUENCE`s, row and relation locking,
`SERIALIZABLE` dependency tracking, etc. All of these
mechanisms are ways to communicate between transactions to prevent
undesirable concurrency issues.

[BDR] does not have a distributed transaction manager or
lock manager. That\'s part of why it performs well with latency and
network partitions. As a result, so [*transactions on different nodes
execute entirely in isolation from each other*]. Despite the
usual perception that \"more isolation is good\" you actually need to
reduce isolation to prevent conflicts.



  --------------------------------------- ------------------------------------- ---------------------------------------------
  [Prev](conflicts.md)     [Home](README.md)     [Next](conflicts-types.md)  
  Active-Active conflicts                  [Up](conflicts.md)                              Types of conflict
  --------------------------------------- ------------------------------------- ---------------------------------------------
