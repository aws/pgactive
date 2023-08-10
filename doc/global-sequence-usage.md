  [BDR 2.1.0 Documentation](README.md)                                                                                                                          
  [Prev](global-sequences-when.md "When to use global sequences")   [Up](global-sequences.md)    Chapter 10. Global Sequences    [Next](global-sequence-limitations.md "Global sequence limitations")  


# 10.3. Using global sequences

To use a global sequence, create a local sequence with
`CREATE SEQUENCE ...` like normal. Then instead of using
`nextval(seqname)` to get values from it, use
`bdr.bdr_snowflake_id_nextval(seqname)`. The destination column
must be `BIGINT` as the result is 64 bits wide.

``` PROGRAMLISTING
    CREATE TABLE gstest (
      id bigint primary key,
      parrot text
    );

    CREATE SEQUENCE gstest_id_seq OWNED BY gstest.id;

    ALTER TABLE gstest ALTER COLUMN id SET DEFAULT bdr.bdr_snowflake_id_nextval('gstest_id_seq');
```

For instance, utility functions like the following can help convert all local
sequences to global sequences and vice versa. It is recommended to change these
functions to taste - like converting only a few specified sequences, skip some
tables or schemas and so on.

``` PROGRAMLISTING
  CREATE FUNCTION convert_local_seqs_to_snowflake_id_seqs()
  RETURNS VOID
  LANGUAGE plpgsql
  AS $$
  DECLARE
    schema_name text;
    table_name text;
    column_name text;
    sequence_name text;
    default_expr text;
    query text;
  BEGIN
    FOR schema_name, table_name, column_name, sequence_name, default_expr IN
        EXECUTE 'SELECT pg_namespace.nspname AS schema_name,
          pg_class.relname AS table_name,
          attname AS column_name,
          pg_get_serial_sequence(attrelid::regclass::text, attname) AS sequence_name,
          pg_get_expr(adbin, adrelid) AS default_expr
          FROM pg_attribute
          JOIN pg_attrdef ON adrelid = attrelid AND adnum = attnum
          JOIN pg_class ON attrelid = pg_class.oid
          JOIN pg_namespace ON pg_class.relnamespace = pg_namespace.oid
          WHERE attnum > 0
          AND NOT attisdropped
          AND pg_get_serial_sequence(attrelid::regclass::text, attname) IS NOT NULL
          AND pg_namespace.nspname <> ''bdr'';'
    LOOP
      IF default_expr NOT LIKE 'bdr.bdr_snowflake_id_nextval%' THEN
        query := format('ALTER TABLE %s.%s ALTER COLUMN %s SET DEFAULT bdr.bdr_snowflake_id_nextval(''%s''::regclass)',
                        schema_name, table_name, column_name, sequence_name);
        EXECUTE query;
        RAISE NOTICE 'globalized sequence: % for column: % table: % schema: %',
                     sequence_name, column_name, table_name, schema_name;
      END IF;
    END LOOP;
  END;
  $$;

  SELECT convert_local_seqs_to_snowflake_id_seqs();

  CREATE FUNCTION convert_snowflake_id_seqs_to_local_seqs()
  RETURNS VOID
  LANGUAGE plpgsql
  AS $$
  DECLARE
    schema_name text;
    table_name text;
    column_name text;
    sequence_name text;
    default_expr text;
    query text;
  BEGIN
    FOR schema_name, table_name, column_name, sequence_name, default_expr IN
      EXECUTE 'SELECT pg_namespace.nspname AS schema_name,
        pg_class.relname AS table_name,
        attname AS column_name,
        pg_get_serial_sequence(attrelid::regclass::text, attname) AS sequence_name,
        pg_get_expr(adbin, adrelid) AS default_expr
        FROM pg_attribute
        JOIN pg_attrdef ON adrelid = attrelid AND adnum = attnum
        JOIN pg_class ON attrelid = pg_class.oid
        JOIN pg_namespace ON pg_class.relnamespace = pg_namespace.oid
        WHERE attnum > 0
        AND NOT attisdropped
        AND pg_get_serial_sequence(attrelid::regclass::text, attname) IS NOT NULL
        AND pg_get_expr(adbin, adrelid) LIKE ''bdr.bdr_snowflake_id_nextval%''
        AND pg_namespace.nspname <> ''bdr'';'
    LOOP
      query := format('ALTER TABLE %s.%s ALTER COLUMN %s SET DEFAULT nextval(''%s''::regclass)',
                      schema_name, table_name, column_name, sequence_name);
      EXECUTE query;
      RAISE NOTICE 'localized sequence: % for column: % table: % schema: %',
                   sequence_name, column_name, table_name, schema_name;
    END LOOP;
  END;
  $$;

  SELECT convert_snowflake_id_seqs_to_local_seqs();
```

If you normally create the sequence as a `BIGSERIAL` column you
may continue to do so. To enable global sequence use on the column you
must `ALTER` the `DEFAULT` expression after table
creation. There is currently no facility to do this automatically and
transparently so you need to do it in a follow up command like:

``` PROGRAMLISTING
    ALTER TABLE my_table ALTER COLUMN my_bigserial SET DEFAULT bdr.bdr_snowflake_id_nextval('my_table_my_bigserial_seq');
   
```

`SERIAL` must be converted to `BIGSERIAL` since
32-bit wide global sequence values are not supported.

  **Warning**
  Do not add a `BIGSERIAL` column to an existing non-empty table, either directly or using `bdr.replicate_ddl_command`. Instead follow the advice for adding columns in the [DDL replication](ddl-replication.md) chapter. If default values are assigned during `ALTER TABLE ... ADD COLUMN ...` rather than by a follow-up `UPDATE` the order of value assignment may differ from node to node, leading to inconsistencies. Additionally, the sequence values will be assigned before you can switch to using `bdr.bdr_snowflake_id_nextval`.

Global sequences are handled normally by [pg_dump].
Because the `DEFAULT` is
`bdr.bdr_snowflake_id_nextval(...)`, the `bdr` schema must
exist on the node targeted for restoration.

Global sequences work on one or more nodes and do not require any
inter-node communication after the node join process completes. So they
may continue to be used even if there\'s the risk of extended network
partitions and are not affected by replication lag or inter-node
latency.

It\'s preferable to avoid calling `nextval` on a sequence
that\'s used with `bdr.bdr_snowflake_id_nextval`. Doing so won\'t
cause any harm so long as the application doesn\'t try to mix the
results of the two functions in the same column and expect them to be
unique.



  --------------------------------------------------- -------------------------------------------- ---------------------------------------------------------
  [Prev](global-sequences-when.md)        [Home](README.md)         [Next](global-sequence-limitations.md)  
  When to use global sequences                         [Up](global-sequences.md)                                Global sequence limitations
  --------------------------------------------------- -------------------------------------------- ---------------------------------------------------------
