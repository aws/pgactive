::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ----------------------------------------------------------------------------- -------------------------------------------- ------------------------------ -------------------------------------------------------------------------------
  [Prev](global-sequence-voting.md "Global sequence voting"){accesskey="P"}   [Up](global-sequences.md){accesskey="U"}    Chapter 10. Global Sequences    [Next](global-sequences-bdr10.md "BDR 1.0 global sequences"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [10.7. Traditional approaches to sequences in distributed DBs]{#GLOBAL-SEQUENCES-ALTERNATIVES} {#traditional-approaches-to-sequences-in-distributed-dbs .SECT1}

Global sequences provide a mostly-application-transparent alternative to
using offset-step sequences or UUID/GUID keys, but they are not without
downsides.

BDR users may use any other multimaster-safe sequence/key generation
strategy. It is not necessary to use global sequences. The approaches
described below will be superior for many applications\' needs, and more
sophisticated approaches also exist.

::: WARNING
  ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  **Warning**
  Applications can [*not*]{.emphasis} safely use counter-table based approaches relying on `SELECT ... FOR UPDATE`{.LITERAL}, `UPDATE ... RETURNING ...`{.LITERAL} etc for sequence generation in BDR. Because BDR is asynchronous and doesn\'t take row locks between nodes, the same values will be generated on more than one node. For the same reason the usual strategies for \"gapless\" sequence generation do not work with BDR. In most cases the application should coordinate generation of sequences that must be gapless from some external source using two-phase commit, or it should only generate them on one node in the BDR group.
  ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
:::

::: SECT2
## [10.7.1. Step/offset sequences]{#GLOBAL-SEQUENCES-ALTERNATIVE-STEPOFFSET} {#stepoffset-sequences .SECT2}

In offset-step sequences a normal PostgreSQL sequence is used on each
node. Each sequence increments by the same amount and starts at
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

\... then on each node calling `setval`{.FUNCTION} to give each node a
different offset starting value, e.g.

``` PROGRAMLISTING
      -- On node 1
      SELECT setval('some_seq', 1);

      -- On node 2
      SELECT setval('some_seq', 2);

      -- ... etc

```

You should be sure to allow a large enough `INCREMENT`{.LITERAL} to
leave room for all the nodes you may ever want to add since changing it
in future is difficult and disruptive.

On BDR-Postgres 9.4, create the sequence with `USING local`{.LITERAL} to
make sure there\'s no conflict with any `default_sequenceam`{.LITERAL}
setting.

If you use bigint values there is no practial concern about key
exhaustion even if you use offsets of 10000 or more. You\'ll need
hundreds of years with hundreds of machines doing millions of inserts
per second to have any chance of approaching exhaustion.

BDR does not currently offer any automation for configuration of the
per-node offsets on such step/offset sequences.
:::

::: SECT2
## [10.7.2. Composite keys]{#GLOBAL-SEQUENCES-ALTERNATIVE-COMPOSITE} {#composite-keys .SECT2}

A variant on step/offset sequences is to use a composite key composed of
`PRIMARY KEY (node_number, generated_value)`{.LITERAL} where the node
number is usually obtained from a function that returns a different
number on each node. Such a function may be created by temporarily
disabling DDL replication and creating a constant SQL function, or by
using a one-row table that isn\'t part of a replication set to store a
different value in each node.
:::

::: SECT2
## [10.7.3. UUIDs]{#GLOBAL-SEQUENCES-ALTERNATIVE-UUID} {#uuids .SECT2}

UUID keys instead eschew sequences entirely and use 128-bit universal
unique identifiers. These are large random or pseudorandom values that
are large enough that it\'s nearly impossible for the same value to be
generated twice. There is no need for nodes to have continuous
communication when using UUID keys.

In the incredibly unlikely event of a collision, conflict detection will
choose the newer of the two inserted records to retain. Conflict
logging, if enabled, will record such an event, but it is
[*exceptionally*]{.emphasis} unlikely to ever occur, since collisions
only become practically likely after about 2\^64 keys have been
generated.

The main downside of UUID keys is that they\'re somewhat space- and
network-inefficient, consuming more space not only as a primary key, but
also where referenced in foreign keys and when transmitted on the wire.
Additionally, not all applications cope well with [UUID]{.APPLICATION}
keys.

PostgreSQL has a built-in `uuid`{.LITERAL} data type and the
`uuid-ossp`{.LITERAL} extension will generate UUIDs, e.g.

``` PROGRAMLISTING
     CREATE EXTENSION "uuid-ossp";

     SELECT uuid_generate_v4();

```
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ---------------------------------------------------- -------------------------------------------- ----------------------------------------------------
  [Prev](global-sequence-voting.md){accesskey="P"}        [Home](index.md){accesskey="H"}         [Next](global-sequences-bdr10.md){accesskey="N"}
  Global sequence voting                                [Up](global-sequences.md){accesskey="U"}                              BDR 1.0 global sequences
  ---------------------------------------------------- -------------------------------------------- ----------------------------------------------------
:::
