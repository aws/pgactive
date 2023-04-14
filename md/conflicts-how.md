::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                                      
  ---------------------------------------------------------------- ------------------------------------- ----------------------------------- -----------------------------------------------------------------
  [Prev](conflicts.md "Multi-master conflicts"){accesskey="P"}   [Up](conflicts.md){accesskey="U"}    Chapter 9. Multi-master conflicts    [Next](conflicts-types.md "Types of conflict"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [9.1. How conflicts happen]{#CONFLICTS-HOW} {#how-conflicts-happen .SECT1}

Inter-node conflicts arise as a result of sequences of events that could
not happen if all the involved transactions happened concurrently on the
same node. Because the nodes only exchange changes after transactions
commit, each transaction is individually valid on the node it committed
on but would not be valid if run on another node that has done other
work in the mean time. Since [BDR]{.PRODUCTNAME} apply essentially
replays the transaction on the other nodes, the replay operation can
fail if there is a conflict between a transaction being applied and a
transaction that was committed on the receiving node.

The reason most conflicts can\'t happen when all transactions run on a
single node is that PostgreSQL has inter-transaction communication
mechanisms to prevent it - `UNIQUE`{.LITERAL} indexes,
`SEQUENCE`{.LITERAL}s, row and relation locking,
`SERIALIZABLE`{.LITERAL} dependency tracking, etc. All of these
mechanisms are ways to communicate between transactions to prevent
undesirable concurrency issues.

[BDR]{.PRODUCTNAME} does not have a distributed transaction manager or
lock manager. That\'s part of why it performs well with latency and
network partitions. As a result, so [*transactions on different nodes
execute entirely in isolation from each other*]{.emphasis}. Despite the
usual perception that \"more isolation is good\" you actually need to
reduce isolation to prevent conflicts.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  --------------------------------------- ------------------------------------- ---------------------------------------------
  [Prev](conflicts.md){accesskey="P"}     [Home](index.md){accesskey="H"}     [Next](conflicts-types.md){accesskey="N"}
  Multi-master conflicts                   [Up](conflicts.md){accesskey="U"}                              Types of conflict
  --------------------------------------- ------------------------------------- ---------------------------------------------
:::
