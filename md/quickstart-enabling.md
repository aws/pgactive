::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ------------------------------------------------------------------------------- -------------------------------------- ------------------------------ ----------------------------------------------------------------------------------
  [Prev](quickstart-creating.md "Creating the demo databases"){accesskey="P"}   [Up](quickstart.md){accesskey="U"}    Chapter 3. Quick-start guide    [Next](quickstart-testing.md "Testing your BDR-enabled system"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [3.5. Enabling BDR in SQL sessions for both of your nodes/instances]{#QUICKSTART-ENABLING} {#enabling-bdr-in-sql-sessions-for-both-of-your-nodesinstances .SECT1}

On the first node/instance in database [\"bdrdemo\"]{.QUOTE} as
postgreSQL superuser, create the extensions necessary for
[BDR]{.PRODUCTNAME}:

``` PROGRAMLISTING
    psql -p 5598 -U postgres bdrdemo

       CREATE EXTENSION btree_gist;
       CREATE EXTENSION bdr;

```

Then you run a function that identifies a [BDR]{.PRODUCTNAME} group that
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
[\"bdrdemo\"]{.QUOTE} as postgreSQL superuser, create the extensions
necessary for [BDR]{.PRODUCTNAME}:

``` PROGRAMLISTING
    psql -p 5599 -U postgres bdrdemo

       CREATE EXTENSION btree_gist;
       CREATE EXTENSION bdr;

```

Then run a function that joins this node/instance to your
[BDR]{.PRODUCTNAME} group you created above (for the second node, we
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
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------------- -------------------------------------- ------------------------------------------------
  [Prev](quickstart-creating.md){accesskey="P"}     [Home](index.md){accesskey="H"}      [Next](quickstart-testing.md){accesskey="N"}
  Creating the demo databases                        [Up](quickstart.md){accesskey="U"}                   Testing your BDR-enabled system
  ------------------------------------------------- -------------------------------------- ------------------------------------------------
:::
