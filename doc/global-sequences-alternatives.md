  [BDR 2.1.0 Documentation](README.md)                                                                                                                     
 [Prev](global-sequences-orms.md "Global sequences and ORMs")   [Up](global-sequences.md)    Chapter 10. Global Sequences    [Next](replication-sets.md "Replication Sets")

# 10.6. Traditional approaches to sequences in distributed DBs

Global sequences provide a mostly-application-transparent alternative to
using offset-step sequences or UUID/GUID keys, but they are not without
downsides.

BDR users may use any other Active-Active-safe sequence/key generation
strategy. It is not necessary to use global sequences. The approaches
described below will be superior for many applications\' needs, and more
sophisticated approaches also exist.

  **Warning**
  Applications can [*not*] safely use counter-table based approaches relying on `SELECT ... FOR UPDATE`, `UPDATE ... RETURNING ...` etc for sequence generation in BDR. Because BDR is asynchronous and doesn\'t take row locks between nodes, the same values will be generated on more than one node. For the same reason the usual strategies for \"gapless\" sequence generation do not work with BDR. In most cases the application should coordinate generation of sequences that must be gapless from some external source using two-phase commit, or it should only generate them on one node in the BDR group.

## 10.6.1. Split-step or partitioned sequences

In split-step or partitioned sequences a normal PostgreSQL sequence is used on
each node. Each sequence increments by the same amount and starts at
differing offsets. For example with step 1000 node1\'s sequence
generates 1001, 2001, 3001, and so on, node 2\'s generates 1002, 2002,
3002, etc. This scheme works well even if the nodes cannot communicate
for extended periods, but requires that the designer specify a maximum
number of nodes when establishing the schema and requires per-node
configuration. Mistakes can easily lead to overlapping sequences.

It is relatively simple to configure this approach with BDR by creating
the desired sequence on one node like

``` PROGRAMLISTING
      CREATE TABLE some_table (
        generated_value bigint primary key
      );

      CREATE SEQUENCE some_seq INCREMENT 1000 OWNED BY some_table.generated_value;

      ALTER TABLE some_table ALTER COLUMN generated_value SET DEFAULT nextval('some_seq');
```

\... then on each node calling `setval` to give each node a
different offset starting value, e.g.

``` PROGRAMLISTING
      -- On node 1
      SELECT setval('some_seq', 1);

      -- On node 2
      SELECT setval('some_seq', 2);

      -- ... etc
```

You should be sure to allow a large enough `INCREMENT` to
leave room for all the nodes you may ever want to add since changing it
in future is difficult and disruptive.

If you use bigint values there is no practial concern about key
exhaustion even if you use offsets of 10000 or more. You\'ll need
hundreds of years with hundreds of machines doing millions of inserts
per second to have any chance of approaching exhaustion.

BDR does not currently offer any automation for configuration of the
per-node offsets on such split-step or partitioned sequences. For instance,
utility functions like the following can help convert all local sequences to
partitioned sequences and vice versa. It is recommended to change these
functions to taste - like converting only a few specified sequences, skip some
tables or schemas and so on.

``` PROGRAMLISTING
  CREATE FUNCTION convert_local_seqs_to_partitioned_seqs (
    IN unique_node_id integer,
    IN increment_by integer)
  RETURNS VOID
  LANGUAGE plpgsql
  AS $$
  DECLARE
    schema_name text;
    table_name text;
    column_name text;
    sequence_name text;
    query text;
    max_seq_value bigint;
  BEGIN
    FOR schema_name, table_name, column_name, sequence_name IN
      EXECUTE 'SELECT pg_namespace.nspname AS schema_name,
        pg_class.relname AS table_name,
        attname AS column_name,
        pg_get_serial_sequence(attrelid::regclass::text, attname) AS sequence_name
        FROM pg_attribute
        JOIN pg_attrdef ON adrelid = attrelid AND adnum = attnum
        JOIN pg_class ON attrelid = pg_class.oid
        JOIN pg_namespace ON pg_class.relnamespace = pg_namespace.oid
        WHERE attnum > 0
        AND NOT attisdropped
        AND pg_get_serial_sequence(attrelid::regclass::text, attname) IS NOT NULL
        AND pg_namespace.nspname <> ''bdr'';'
    LOOP
      query := format('ALTER SEQUENCE %s INCREMENT BY %s;', sequence_name, increment_by);
      EXECUTE query;
      query := format('SELECT max(%s) FROM %s', column_name, table_name);
      EXECUTE query INTO max_seq_value;
      query := format('SELECT setval(''%s'', %s);', sequence_name, max_seq_value);
      EXECUTE query;
      RAISE NOTICE 'globalized sequence: % for column: % table: % schema: %',
                   sequence_name, column_name, table_name, schema_name;
    END LOOP;
  END;
  $$;

  SELECT convert_local_seqs_to_partitioned_seqs();

  CREATE FUNCTION convert_partitioned_seqs_to_local_seqs()
  RETURNS VOID
  LANGUAGE plpgsql
  AS $$
  DECLARE
    schema_name text;
    table_name text;
    column_name text;
    sequence_name text;
    query text;
    max_seq_value bigint;
  BEGIN
    FOR schema_name, table_name, column_name, sequence_name IN
      EXECUTE 'SELECT pg_namespace.nspname AS schema_name,
        pg_class.relname AS table_name,
        attname AS column_name,
        pg_get_serial_sequence(attrelid::regclass::text, attname) AS sequence_name
        FROM pg_attribute
        JOIN pg_attrdef ON adrelid = attrelid AND adnum = attnum
        JOIN pg_class ON attrelid = pg_class.oid
        JOIN pg_namespace ON pg_class.relnamespace = pg_namespace.oid
        WHERE attnum > 0
        AND NOT attisdropped
        AND pg_get_serial_sequence(attrelid::regclass::text, attname) IS NOT NULL
        AND pg_namespace.nspname <> ''bdr'';'
    LOOP
      query := format('ALTER SEQUENCE %s INCREMENT BY 1;', sequence_name);
      EXECUTE query;
      query := format('SELECT max(%s) FROM %s', column_name, table_name);
      EXECUTE query INTO max_seq_value;
      query := format('SELECT setval(''%s'', %s);', sequence_name, max_seq_value);
      EXECUTE query;
      RAISE NOTICE 'localized sequence: % for column: % table: % schema: %',
                   sequence_name, column_name, table_name, schema_name;
    END LOOP;
  END;
  $$;

  SELECT convert_partitioned_seqs_to_local_seqs();
```

## 10.6.2. Composite keys

A variant on split-step or partitioned sequences is to use a composite key
composed of `PRIMARY KEY (node_number, generated_value)` where the node
number is usually obtained from a function that returns a different
number on each node. Such a function may be created by temporarily
disabling DDL replication and creating a constant SQL function, or by
using a one-row table that isn\'t part of a replication set to store a
different value in each node.

## 10.6.3. UUIDs

UUID keys instead eschew sequences entirely and use 128-bit universal
unique identifiers. These are large random or pseudorandom values that
are large enough that it\'s nearly impossible for the same value to be
generated twice. There is no need for nodes to have continuous
communication when using UUID keys.

In the incredibly unlikely event of a collision, conflict detection will
choose the newer of the two inserted records to retain. Conflict
logging, if enabled, will record such an event, but it is
[*exceptionally*] unlikely to ever occur, since collisions
only become practically likely after about 2\^64 keys have been
generated.

The main downside of UUID keys is that they\'re somewhat space- and
network-inefficient, consuming more space not only as a primary key, but
also where referenced in foreign keys and when transmitted on the wire.
Additionally, not all applications cope well with [UUID]
keys.

PostgreSQL has a built-in `uuid` data type and the
`uuid-ossp` extension will generate UUIDs, e.g.

``` PROGRAMLISTING
     CREATE EXTENSION "uuid-ossp";

     SELECT uuid_generate_v4();
    
```



  ---------------------------------------------------- -------------------------------------------- ----------------------------------------------------
  [Prev](global-sequences-orms.md)        [Home](README.md)         [Next](replication-sets.md)  
   Global sequences and ORMs                                [Up](global-sequences.md)   Replication Sets
  ---------------------------------------------------- -------------------------------------------- ----------------------------------------------------
