  [BDR 2.1.0 Documentation](README.md)                                                                                                                                                   
  [Prev](quickstart-enabling.md "Enabling BDR in SQL sessions for both of your nodes/instances")   [Up](quickstart.md)    Chapter 3. Quick-start guide    [Next](manual.md "BDR administration manual")  


# 3.6. Testing your BDR-enabled system

Create a table and insert rows from your first node/instance:

``` PROGRAMLISTING
    psql -p 5598 -U postgres bdrdemo

      SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.t1bdr (c1 INT, PRIMARY KEY (c1)); $DDL$);
      INSERT INTO t1bdr VALUES (1);
      INSERT INTO t1bdr VALUES (2);
      -- you will see two rows
      SELECT * FROM t1bdr;
    
```

Check that the rows are there on your second node/instance. Now, delete
a row:

``` PROGRAMLISTING
    psql -p 5599 -U postgres bdrdemo

      -- you will see two rows
      SELECT * FROM t1bdr;
      DELETE FROM t1bdr WHERE c1 = 2;
      -- you will see one row
      SELECT * FROM t1bdr;
    
```

Check that one row has been deleted from the first node/instance::

``` PROGRAMLISTING
    psql -p 5598 -U postgres bdrdemo

      -- you will see one row
      SELECT * FROM t1bdr;
    
```

Create and use global sequence::

``` PROGRAMLISTING
    psql -p 5598 -U postgres bdrdemo

      -- Create a normal local sequence
      SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE SEQUENCE public.test_seq; $DDL$);
    
```

Once you\'ve created a global sequence you may use it with
bdr.bdr_snowflake_id_nextval(seqname). Note: The destination column must be
BIGINT. See [Global Sequence
Limitations](global-sequence-limitations.md).

``` PROGRAMLISTING
    psql -p 5598 -U postgres bdrdemo

      -- Use the global sequence
      SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_tbl (id bigint DEFAULT bdr.bdr_snowflake_id_nextval('public.test_seq'),name text); $DDL$);
   
    
```

Insert and check the id.

``` PROGRAMLISTING
    psql -p 5598 -U postgres bdrdemo

      -- Insert into the table
      INSERT INTO test_tbl(name) VALUES ('first-entry');
      -- you will see database-generated ids
      SELECT * from test_tbl;
 
    
```

To enable global sequence use on a BIGSERIAL column see [Using global
sequences](global-sequence-usage.md).



  --------------------------------------------------------------- -------------------------------------- ------------------------------------
  [Prev](quickstart-enabling.md)                   [Home](README.md)      [Next](manual.md)  
  Enabling BDR in SQL sessions for both of your nodes/instances    [Up](quickstart.md)             BDR administration manual
  --------------------------------------------------------------- -------------------------------------- ------------------------------------
