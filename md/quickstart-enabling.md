  [BDR 2.0.7 Documentation](README.md)                                                                                                                 
  [Prev](quickstart-creating.md "Creating the demo databases")   [Up](quickstart.md)    Chapter 3. Quick-start guide    [Next](quickstart-testing.md "Testing your BDR-enabled system")  


# 3.5. Enabling BDR in SQL sessions for both of your nodes/instances

On the first node/instance in database [\"bdrdemo\"] as
postgreSQL superuser, create the extensions necessary for
[BDR]:

``` PROGRAMLISTING
    psql -p 5598 -U postgres bdrdemo

       CREATE EXTENSION btree_gist;
       CREATE EXTENSION bdr;
    
```

Then you run a function that identifies a [BDR] group that
delineates a connection string for other nodes to communicate with (for
the first node, we will use port 5598) from the same SQL session as
above on port 5598:

``` PROGRAMLISTING
    SELECT bdr.bdr_group_create(
      local_node_name := 'node1',
      node_external_dsn := 'port=5598 dbname=bdrdemo host=localhost'
);
    
```

To ensure that the node is ready to replicate, run this function from
the same SQL session as above on port 5598:

``` PROGRAMLISTING
    SELECT bdr.bdr_node_join_wait_for_ready();
    
```

On the second node/instance on port 5599 in database
[\"bdrdemo\"] as postgreSQL superuser, create the extensions
necessary for [BDR]:

``` PROGRAMLISTING
    psql -p 5599 -U postgres bdrdemo

       CREATE EXTENSION btree_gist;
       CREATE EXTENSION bdr;
    
```

Then run a function that joins this node/instance to your
[BDR] group you created above (for the second node, we
will use port 5599) from the same SQL session as above on port 5599:

``` PROGRAMLISTING
    SELECT bdr.bdr_group_join(
      local_node_name := 'node2',
      node_external_dsn := 'port=5599 dbname=bdrdemo host=localhost',
      join_using_dsn := 'port=5598 dbname=bdrdemo host=localhost'
);
    
```

To ensure that the node/instance is ready to replicate, run this
function from the same SQL session as above on port 5599:

``` PROGRAMLISTING
    SELECT bdr.bdr_node_join_wait_for_ready();
    
```



  ------------------------------------------------- -------------------------------------- ------------------------------------------------
  [Prev](quickstart-creating.md)     [Home](README.md)      [Next](quickstart-testing.md)  
  Creating the demo databases                        [Up](quickstart.md)                   Testing your BDR-enabled system
  ------------------------------------------------- -------------------------------------- ------------------------------------------------
