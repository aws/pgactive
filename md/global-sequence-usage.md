  [BDR 2.0.7 Documentation](README.md)                                                                                                                          
  [Prev](global-sequences-when.md "When to use global sequences")   [Up](global-sequences.md)    Chapter 10. Global Sequences    [Next](global-sequence-limitations.md "Global sequence limitations")  


# 10.3. Using global sequences

To use a global sequence, create a local sequence with
`CREATE SEQUENCE ...` like normal. Then instead of using
`nextval(seqname)` to get values from it, use
`bdr.global_seq_nextval(seqname)`. The destination column
must be `BIGINT` as the result is 64 bits wide.

``` PROGRAMLISTING
   BEGIN;

    CREATE TABLE gstest (
      id bigint primary key,
      parrot text
    );

    CREATE SEQUENCE gstest_id_seq OWNED BY gstest.id;

    ALTER TABLE gstest ALTER COLUMN id SET DEFAULT bdr.global_seq_nextval('gstest_id_seq');
   
   
```

If you normally create the sequence as a `BIGSERIAL` column you
may continue to do so. To enable global sequence use on the column you
must `ALTER` the `DEFAULT` expression after table
creation. There is currently no facility to do this automatically and
transparently so you need to do it in a follow up command like:

``` PROGRAMLISTING
    ALTER TABLE my_table ALTER COLUMN my_bigserial SET DEFAULT bdr.global_seq_nextval('my_table_my_bigserial_seq');
   
```

`SERIAL` must be converted to `BIGSERIAL` since
32-bit wide global sequence values are not supported.

  **Warning**
  Do not add a `BIGSERIAL` column to an existing non-empty table, either directly or using `bdr.replicate_ddl_command`. Instead follow the advice for adding columns in the [DDL replication](ddl-replication.md) chapter. If default values are assigned during `ALTER TABLE ... ADD COLUMN ...` rather than by a follow-up `UPDATE` the order of value assignment may differ from node to node, leading to inconsistencies. Additionally, the sequence values will be assigned before you can switch to using `bdr.global_seq_nextval`.

Global sequences are handled normally by [pg_dump].
Because the `DEFAULT` is
`bdr.global_seq_nextval(...)`, the `bdr` schema must
exist on the node targeted for restoration.

Global sequences work on one or more nodes and do not require any
inter-node communication after the node join process completes. So they
may continue to be used even if there\'s the risk of extended network
partitions and are not affected by replication lag or inter-node
latency.

It\'s preferable to avoid calling `nextval` on a sequence
that\'s used with `bdr.global_seq_nextval`. Doing so won\'t
cause any harm so long as the application doesn\'t try to mix the
results of the two functions in the same column and expect them to be
unique.



  --------------------------------------------------- -------------------------------------------- ---------------------------------------------------------
  [Prev](global-sequences-when.md)        [Home](README.md)         [Next](global-sequence-limitations.md)  
  When to use global sequences                         [Up](global-sequences.md)                                Global sequence limitations
  --------------------------------------------------- -------------------------------------------- ---------------------------------------------------------
