  [BDR 2.0.7 Documentation](README.md)                                                                                                         
  [Prev](technotes-ddl-locking.md "DDL locking details")   [Up](technotes.md)    Appendix C. Technical notes    [Next](bookindex.md "Index")  


# C.3. Full table rewrites

There are a number of reasons why BDR doesn\'t support DDL operations
that perform full table rewrites.

They tend to be very slow operations for which the [global DDL
lock](ddl-replication-advice.md#DDL-REPLICATION-LOCKING) must be held
throughout. That\'s a long time to \"stop the world\". They can be
problematic for apps on standalone PostgreSQL for this reason, but it\'s
worse on BDR due to the global DDL lock.

Table rewrites discard replication origin and commit timestamp
information that we need to ensure that conflict resolution is
consistent across all nodes. There\'s currently no way to remap it.

Finally, we can\'t guarantee that the rewrite will have the same results
across all nodes unless the entire expression it uses is classified as
immutable. This isn\'t currently checked for. Even seemingly safe
defaults like `nextval(...)` aren\'t safe because the order
in which rows are processed by a table rewrite will be different on
different nodes, so different rows will get a given generated value on
each node.

Because of the performance issues we recommend that table-rewriting
operations be split up into multiple smaller operations and aren\'t
prioritizing support for the subset of them that can be made safe; see
[How to work around restricted
DDL](ddl-replication-statements.md#DDL-REPLICATION-HOW).



  --------------------------------------------------- ------------------------------------- ---------------------------------------
  [Prev](technotes-ddl-locking.md)     [Home](README.md)     [Next](bookindex.md)  
  DDL locking details                                  [Up](technotes.md)                                    Index
  --------------------------------------------------- ------------------------------------- ---------------------------------------
