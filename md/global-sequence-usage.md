::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ---------------------------------------------------------------------------------- -------------------------------------------- ------------------------------ ---------------------------------------------------------------------------------------
  [Prev](global-sequences-when.md "When to use global sequences"){accesskey="P"}   [Up](global-sequences.md){accesskey="U"}    Chapter 10. Global Sequences    [Next](global-sequence-limitations.md "Global sequence limitations"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [10.3. Using global sequences]{#GLOBAL-SEQUENCE-USAGE} {#using-global-sequences .SECT1}

To use a global sequence, create a local sequence with
`CREATE SEQUENCE ...`{.LITERAL} like normal. Then instead of using
`nextval(seqname)`{.FUNCTION} to get values from it, use
`bdr.global_seq_nextval(seqname)`{.FUNCTION}. The destination column
must be `BIGINT`{.TYPE} as the result is 64 bits wide.

``` PROGRAMLISTING
   BEGIN;

    CREATE TABLE gstest (
      id bigint primary key,
      parrot text
    );

    CREATE SEQUENCE gstest_id_seq OWNED BY gstest.id;

    ALTER TABLE gstest ALTER COLUMN id SET DEFAULT bdr.global_seq_nextval('gstest_id_seq');


```

If you normally create the sequence as a `BIGSERIAL`{.TYPE} column you
may continue to do so. To enable global sequence use on the column you
must `ALTER`{.LITERAL} the `DEFAULT`{.LITERAL} expression after table
creation. There is currently no facility to do this automatically and
transparently so you need to do it in a follow up command like:

``` PROGRAMLISTING
    ALTER TABLE my_table ALTER COLUMN my_bigserial SET DEFAULT bdr.global_seq_nextval('my_table_my_bigserial_seq');

```

`SERIAL`{.LITERAL} must be converted to `BIGSERIAL`{.LITERAL} since
32-bit wide global sequence values are not supported.

::: WARNING
  ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  **Warning**
  Do not add a `BIGSERIAL`{.TYPE} column to an existing non-empty table, either directly or using `bdr.replicate_ddl_command`{.LITERAL}. Instead follow the advice for adding columns in the [DDL replication](ddl-replication.md) chapter. If default values are assigned during `ALTER TABLE ... ADD COLUMN ...`{.LITERAL} rather than by a follow-up `UPDATE`{.LITERAL} the order of value assignment may differ from node to node, leading to inconsistencies. Additionally, the sequence values will be assigned before you can switch to using `bdr.global_seq_nextval`{.LITERAL}.
  ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
:::

Global sequences are handled normally by [pg_dump]{.APPLICATION}.
Because the `DEFAULT`{.LITERAL} is
`bdr.global_seq_nextval(...)`{.LITERAL}, the `bdr`{.LITERAL} schema must
exist on the node targeted for restoration.

Global sequences work on one or more nodes and do not require any
inter-node communication after the node join process completes. So they
may continue to be used even if there\'s the risk of extended network
partitions and are not affected by replication lag or inter-node
latency.

It\'s preferable to avoid calling `nextval`{.LITERAL} on a sequence
that\'s used with `bdr.global_seq_nextval`{.FUNCTION}. Doing so won\'t
cause any harm so long as the application doesn\'t try to mix the
results of the two functions in the same column and expect them to be
unique.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  --------------------------------------------------- -------------------------------------------- ---------------------------------------------------------
  [Prev](global-sequences-when.md){accesskey="P"}        [Home](index.md){accesskey="H"}         [Next](global-sequence-limitations.md){accesskey="N"}
  When to use global sequences                         [Up](global-sequences.md){accesskey="U"}                                Global sequence limitations
  --------------------------------------------------- -------------------------------------------- ---------------------------------------------------------
:::
