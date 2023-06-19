  [BDR 2.0.7 Documentation](README.md)                                                                                                                          
  [Prev](replication-sets-concepts.md "Replication Set Concepts")   [Up](replication-sets.md)    Chapter 11. Replication Sets    [Next](replication-sets-nodes.md "Node Replication Control")  


# [11.2. Creating replication sets]

Replication sets are not created or dropped explicitly. Rather, a
replication set exists if it has one or more tables assigned to it or
one or more connections consuming it. The `default`
replication set always exists, and contains all tables that have not
been explicitly assigned to another replication set. Adding a table to
some non-default replication set [*removes it from the
`default` replication set*] unless you also
explicitly name the `default` replication set in its set
memberships.



  ------------------------------------------------------- -------------------------------------------- ----------------------------------------------------
  [Prev](replication-sets-concepts.md)        [Home](README.md)         [Next](replication-sets-nodes.md)  
  Replication Set Concepts                                 [Up](replication-sets.md)                              Node Replication Control
  ------------------------------------------------------- -------------------------------------------- ----------------------------------------------------
