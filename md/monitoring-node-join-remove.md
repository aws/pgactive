  [BDR 2.0.7 Documentation](README.md)                                                                                                
  [Prev](monitoring-why.md "Why monitoring matters")   [Up](monitoring.md)    Chapter 7. Monitoring    [Next](monitoring-peers.md "Monitoring replication peers")  


# 7.2. Monitoring node join/removal

Node join and removal is asynchronous in BDR. The [Node management
functions](functions-node-mgmt.md) return immediately, without first
ensuring the join or part operation is complete. To see when a join or
part operation finishes it is necessary to check the node state
indirectly via [bdr.bdr_nodes](catalog-bdr-nodes.md) or using helper
functions.

The helper function
[bdr.bdr_node_join_wait_for_ready](functions-node-mgmt.md#FUNCTION-BDR-NODE-JOIN-WAIT-FOR-READY),
when called, will cause a PostgreSQL session to pause until outstanding
node join operations complete. More helpers for node status monitoring
will be added over time.

For other node status monitoring [bdr.bdr_nodes](catalog-bdr-nodes.md)
or must be queried directly.

Here is an example of a `SELECT` from
`bdr.bdr_nodes` that indicates that one node is ready
(`r`), one node has been removed/killed (`k`), and
one node is initializing (`i`):

``` PROGRAMLISTING
    SELECT * FROM bdr.bdr_nodes;
         node_sysid      | node_timeline | node_dboid | node_status | node_name |      node_local_dsn      |    node_init_from_dsn
    ---------------------+---------------+------------+-------------+-----------+--------------------------+--------------------------
     6125823754033780536 |             1 |      16385 | r           | node1     | port=5598 dbname=bdrdemo |
     6125823714403985168 |             1 |      16386 | k           | node2     | port=5599 dbname=bdrdemo | port=5598 dbname=bdrdemo
     6125847382076805699 |             1 |      16386 | i           | node3     | port=6000 dbname=bdrdemo | port=5598 dbname=bdrdemo
    (3 rows)
    
```



  -------------------------------------------- -------------------------------------- ----------------------------------------------
  [Prev](monitoring-why.md)     [Home](README.md)      [Next](monitoring-peers.md)  
  Why monitoring matters                        [Up](monitoring.md)                    Monitoring replication peers
  -------------------------------------------- -------------------------------------- ----------------------------------------------
