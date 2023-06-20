  [BDR 2.0.7 Documentation](README.md)                                                                                                                        
  [Prev](ddl-replication-advice.md "Executing DDL on BDR systems")   [Up](ddl-replication.md)    Chapter 8. DDL Replication    [Next](conflicts.md "Active-Active conflicts")  


# 8.2. Statement specific DDL replication concerns

Not all commands can be replicated automatically. Some are allowed
regardless - generally ones that have affect on more than one database.
Others are disallowed, and some have limitations and restrictions on
what can be used as compared to PostgreSQL without BDR.

> **Important:** Global DDL, like `CREATE ROLE`,
> `CREATE USER` etc is [*not replicated*] and
> should be applied on each node if the created objects will be
> referenced by a BDR-enabled database.

## 8.2.1. Statements with weaker DDL locking

Some statements don\'t require the full [DDL
lock](ddl-replication-advice.md#DDL-REPLICATION-LOCKING) and can
proceed with a weaker lock that prohibits other nodes from doing DDL at
the same time, but does not restrict concurrent DML (insert, update or
delete) operations.

-   `CREATE SCHEMA ...`

-   `CREATE [TABLE|VIEW] ...`

-   `CREATE FOREIGN TABLE ...`

-   `CREATE [AGGREGATE|OPERATOR|TYPE] ...`

-   `CREATE [OR REPLACE] FUNCTION ...`

-   `CREATE DOMAIN ...`

-   `ALTER DEFAULT PRIVILEGES ...`

-   `ALTER ... OWNER TO ...`

## 8.2.2. Not replicated DDL statements

Some DDL statements, mainly those that affect objects that are
PostgreSQL-instance-wide rather than database-sepecific, are not
replicated. They are applied on the node that executes them without
taking the global DDL lock and are not sent to other nodes.

If you create non-replicated objects that are to be referenced by
replicated objects (e.g. creating a role, not replicated, then creating
a table, replicated, that\'s owned by that role) you must ensure that
the non-replicated object is created on all [BDR] nodes.
You can do this manually, by running the statement on each node. Or you
can use
[bdr.bdr_replicate_ddl_command](functions-node-mgmt.md#FUNCTION-BDR-REPLICATE-DDL-COMMAND)
to apply the statement on the local node and manually enqueue it for
replication on all nodes.

Using `bdr.bdr_replicate_ddl_command` is the recommended
approach, e.g.:

``` PROGRAMLISTING
     SELECT bdr.bdr_replicate_ddl_command('CREATE USER myuser;');
    
```

> **Note:** It is not necessary that the definition of objects like
> roles be the same on all nodes, only that they exist. You can for
> example `CREATE ROLE somerole WITH NOLOGIN` on most nodes,
> but on one node you can create them `WITH LOGIN`.

The statements that are applied locally but not replicated are:


[]{#DDL-CREATE-INDEX-CONCURRENTLY}`CREATE INDEX CONCURRENTLY`

    `CREATE INDEX CONCURRENTLY` is not replicated. It requires
    a top-level transaction so it cannot be run via
    `bdr.bdr_replicate_ddl_command`, and is not currently
    supported for direct DDL capture.

    It\'s best to specify an index name and use the same name on all
    nodes. Otherwise a later `DROP INDEX` on one node may fail
    to replicate to the other nodes and cause replication to stall.

    Because this command is not replicated, but affects database objects
    that are, it requires that users explicitly set
    `bdr.skip_ddl_replication`, making it clear that the
    user/app knows it will not be replicated. This will prevent
    compatility breaks if in future BDR adds support for replicating the
    command. Use:

    ``` PROGRAMLISTING
           SET bdr.skip_ddl_replication = on;
            CREATE INDEX CONCURRENTLY idxname ON tblname(cols);
            RESET bdr.skip_ddl_replication;
            
    ```

[]{#DDL-DROP-INDEX-CONCURRENTLY}`DROP INDEX CONCURRENTLY`

    `DROP INDEX CONCURRENTLY` is not replicated for the same
    reasons as `CREATE INDEX CONCURRENTLY`. The same
    requirements and workarounds apply.

[]{#DDL-CREATE-DATABASE}`CREATE DATABASE`

    `CREATE DATABASE` cannot be replicated because
    [BDR] works on a per database level.

[]{#DDL-CREATE-ROLE}`CREATE ROLE/USER/GROUP`

    `CREATE ROLE` cannot be replicated because
    [BDR] works on a per database level. It is possible
    that a workaround for this will be added.

    ::: WARNING
      **Warning**
      Not creating roles of the same name (not necessarily with the same access details otherwise though) on all systems can break replication when statements like `ALTER TABLE ... OWNER TO` are replicated.
    :::

[]{#DDL-CREATE-TABLESPACE}`CREATE TABLESPACE`

    `CREATE TABLESPACE` cannot be replicated because
    [BDR] works on a per database level.

    ::: WARNING
      **Warning**
      Not creating tablespaces of the same name (not necessarily with the same location though) on all systems can break replication when statements like `ALTER TABLE ... SET TABLESPACE` are replicated.
    :::

[]{#DDL-DROP-DATABASE}`DROP DATABASE`

    `DROP DATABASE` cannot be replicated because
    [BDR] works on a per database level.

    Note that a database that is configured for [BDR]
    cannot be dropped while that is the case.

[]{#DDL-DROP-TABLESPACE}`DROP TABLESPACE`

    `DROP TABLESPACE` cannot be replicated because
    [BDR] works on a per database level.

    ::: WARNING
      **Warning**
      Dropping tablespaces only on some nodes can cause problems when relations on other nodes are moved into the tablespace that does not exist locally anymore.
    :::

[]{#DDL-DROP-ROLE}`DROP ROLE/USER/GROUP`

    `DROP ROLE` cannot be replicated because
    [BDR] works on a per database level. It is possible
    that a workaround for this will be added.

    ::: WARNING
      **Warning**
      Dropping role only on some nodes can cause problems when objects on other nodes are assigned to roles that do not exist locally anymore.
    :::

[]{#DDL-ALTER-ROLE}`ALTER ROLE/USER/GROUP`

    `ALTER ROLE` cannot be replicated because
    [BDR] works on a per database level. It is possible
    that a workaround for this will be added.

    Normally all commands but `ALTER ROLE ... RENAME TO ...`
    should be safe to execute in the sense that doing so won\'t cause
    replication to break.

    ::: WARNING
      **Warning**
      Renaming a role only on some nodes can lead to problems due to replicated DDL statements not being able to execute anymore.
    :::

[]{#DDL-ALTER-DATABASE}`ALTER DATABASE`

    `ALTER DATABASE` cannot be replicated because
    [BDR] works on a per database level.

    In practice the primary problematic case is when trying to change
    settings on a per database basis using
    `ALTER DATABASE ... SET ...`, these have to be executed on
    every database for now.

    ::: WARNING
      **Warning**
      Renaming a database can lead to the connection information stored on some of the nodes not being valid anymore.
    :::

[]{#DDL-ALTER-TABLESPACE}`ALTER TABLESPACE`

    `ALTER TABLSPACE` cannot be replicated because
    [BDR] works on a per database level. It is safe to
    execute on the individual nodes though.

## 8.2.3. Prohibited DDL statements

BDR prevents some DDL statements from running when it is active on a
database. This protects the consistency of the system by disallowing
statements that cannot be replicated correctly, or for which replication
is not yet supported. Statements that are supported with some
restrictions are covered in [DDL statements with
restrictions](ddl-replication-statements.md#DDL-REPLICATION-RESTRICTED-COMMANDS);
commands that are entirely disallowed in [BDR] are covered
below.

Generally unsupported statements are prevented from being executed,
raising a `feature_not_supported` (SQLSTATE `0A000`)
error.

The following DDL commands are rejected by [BDR] when
[BDR] is active on a database, and will fail with an
`ERROR`:


[]{#DDL-CREATE-TABLE-AS}`CREATE TABLE AS/SELECT INTO`

    `CREATE TABLE AS/SELECT INTO` are prohibited unless
    `UNLOGGED` or `UNLOGGED` temporary is specified.

[]{#DDL-CREATE-TABLE-OF-TYPE}`CREATE TABLE ... OF TYPE`

    `CREATE TABLE ... OF TYPE` is prohibited unless
    `UNLOGGED` or `UNLOGGED` temporary is specified.

[]{#DDL-CREATE-TEXT-SEARCH-PARSER}`CREATE TEXT SEARCH PARSER`

    `CREATE TEXT SEARCH PARSER` is prohibited.

[]{#DDL-CREATE-TEXT-SEARCH-DICTIONARY}`CREATE TEXT SEARCH DICTIONARY`

    `CREATE TEXT SEARCH DICTIONARY` is prohibited.

[]{#DDL-ALTER-TEXT-SEARCH-DICTIONARY}`ALTER TEXT SEARCH DICTIONARY`

    `ALTER TEXT SEARCH DICTIONARY` is prohibited.

[]{#DDL-CREATE-TEXT-SEARCH-TEMPLATE}`CREATE TEXT SEARCH TEMPLATE`

    `CREATE TEXT SEARCH TEMPLATE` is prohibited.

[]{#DDL-CREATE-TEXT-SEARCH-CONFIGURATION}`CREATE TEXT SEARCH CONFIGURATION`

    `CREATE TEXT SEARCH template` is prohibited.

[]{#DDL-ALTER-TEXT-SEARCH-CONFIGURATION}`ALTER TEXT SEARCH CONFIGURATION`

    `ALTER TEXT SEARCH template` is prohibited.

[]{#DDL-CREATE-COLLATION}`CREATE COLLATION`

    `CREATE CREATE COLLATION` is prohibited.

[]{#DDL-ALTER-EXTENSION}`ALTER EXTENSION`

    `ALTER EXTENSION` currently is prohibited.

[]{#DDL-CREATE-FOREIGN-DATA-WRAPPER}`CREATE FOREIGN DATA WRAPPER`

    `CREATE FOREIGN DATA WRAPPER` currently is prohibited.

[]{#DDL-ALTER-FOREIGN-DATA-WRAPPER}`ALTER FOREIGN DATA WRAPPER`

    `ALTER FOREIGN DATA WRAPPER` currently is prohibited.

[]{#DDL-CREATE-SERVER}`CREATE SERVER`

    `CREATE SERVER` currently is prohibited.

[]{#DDL-ALTER-SERVER}`ALTER SERVER`

    `ALTER SERVER` currently is prohibited.

[]{#DDL-CREATE-USER-MAPPING}`CREATE USER MAPPING`

    `CREATE USER MAPPING` currently is prohibited.

[]{#DDL-ALTER-USER-MAPPING}`ALTER USER MAPPING`

    `ALTER USER MAPPING` currently is prohibited.

[]{#DDL-DROP-USER-MAPPING}`DROP USER MAPPING`

    `DROP USER MAPPING` currently is prohibited.

[]{#DDL-CREATEH-MATERIALIZED-VIEW}`CREATE MATERIALIZED VIEW`

    `CREATE MATERIALIZED VIEW` currently is prohibited.

[]{#DDL-REFRESH-MATERIALIZED-VIEW}`REFRESH MATERIALIZED VIEW`

    `REFRESH MATERIALIZED VIEW` currently is prohibited.

[]{#DDL-CREATE-LANGUAGE}`CREATE LANGUAGE`

    `CREATE LANGUAGE` currently is prohibited. Note that
    nearly all procedual languages are available as an extension and
    `CREATE EXTENSION` is supported.

[]{#DDL-CREATE-CONVERSION}`CREATE CONVERSION`

    `CREATE CONVERSION` currently is prohibited.

[]{#DDL-CREATE-CAST}`CREATE CAST`

    ::: NOTE
    > **Note:** `CREATE CAST` currently is prohibited. Note
    > that `CREATE CAST` inside an extension is supported.
    :::

[]{#DDL-CREATE-OPERATOR-FAMILY}`CREATE OPERATOR FAMILY`

    ::: NOTE
    > **Note:** `CREATE OPERATOR FAMILY` currently is
    > prohibited. Note that `CREATE OPERATOR FAMILY` inside an
    > extension is supported.
    :::

[]{#DDL-ALTER-OPERATOR-FAMILY}`ALTER OPERATOR FAMILY`

    `ALTER OPERATOR FAMILY` currently is prohibited.

    ::: NOTE
    > **Note:** Note that `ALTER OPERATOR FAMILY` inside an
    > extension is supported.
    :::

[]{#DDL-CREATE-OPERATOR-CLASS}`CREATE OPERATOR CLASS`

    `CREATE OPERATOR CLASS` currently is prohibited.

    ::: NOTE
    > **Note:** Note that `CREATE OPERATOR CLASS` inside an
    > extension is supported.
    :::

[]{#DDL-DROP-OWNED}`DROP OWNED`

    `DROP OWNED` is prohibited.

[]{#DDL-SECURITY-LABEL}`SECURITY LABEL`

    Except for some [BDR] internal use
    `SECURITY LABEL` is prohibited.

## 8.2.4. DDL statements with restrictions

BDR prevents some DDL statements from running when it is active on a
database. This protects the consistency of the system by disallowing
statements that cannot be replicated correctly, or for which replication
is not yet supported. Entirely prohibited statements are covered above
in [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS);
commands where some subcommands or features are limited are covered
below.

If a statement is not permitted under BDR it is often possible to find
another way to do the same thing. For example, you can\'t do a
`ALTER TABLE` that\'ll cause a full table rewrite, but it\'s
generally possible to rephrase that as a series of independent
`ALTER TABLE` and `UPDATE` statements that don\'t do
the full table rewrite. See
[*`ALTER TABLE`*](ddl-replication-statements.md#DDL-ALTER-TABLE)
below for details on that example.

> **Note:** See [DDL locking details](technotes-ddl-locking.md) for
> additional details on why DDL locking is required and how it\'s done.

Generally unsupported statements are prevented from being executed,
raising a `feature_not_supported` (SQLSTATE `0A000`)
error.

The following statements or statement options are not currently
permitted when BDR is active on a database:


[]{#DDL-CREATE-TABLE}`CREATE TABLE`

    Generally `CREATE TABLE` is allowed. There are a few
    options/subcommands that are not supported.

    Not supported commands are:

    -   `WITH OIDS` - outdated option, not deemed worth to add
        support for

    -   `OF TYPE` - not supported yet

    -   `CONSTRAINT ... EXCLUDE` - not supported yet

[]{#DDL-ALTER-TABLE}`ALTER TABLE`

    Generally `ALTER TABLE` commands are allowed. There are a
    however several sub-commands that are not supported, mainly those
    that perform a full-table re-write.

    Not supported commands are:

    -   `ADD COLUMN ... DEFAULT` - this option can
        unfortunately not be supported. It is however often possible to
        rewrite this into a series of supported commands; see [How to
        work around restricted
        DDL](ddl-replication-statements.md#DDL-REPLICATION-HOW).

    -   `ADD CONSTRAINT ... EXCLUDE` - exclusion are not
        supported for now. Exclusion constraints don\'t make much sense
        in an asynchronous system and lead to changes that cannot be
        replayed.

    -   `ALTER CONSTRAINT` - changing constraint settings is
        not supported for now.

    -   `ALTER COLUMN ... TYPE` - changing a column\'s type is
        not supported. Changing a column in a way that doesn\'t require
        table rewrites may be suppported at some point. See [How to work
        around restricted
        DDL](ddl-replication-statements.md#DDL-REPLICATION-HOW) for
        how to work around this limitation.

    -   `ENABLE .. RULE` - is not supported.

    -   `DISABLE .. RULE` - is not supported.

    -   `[NO] INHERIT` - is not supported.

    -   `[NOT] OF TYPE` - is not supported.

    -   `ALTER COLUMN ... SET (..)` - is not supported at the
        moment. Note however that ALTER COLUMN \... SET \[NOT\] NULL and
        ALTER COLUMN \... SET DEFAULT are supported.

    -   `SET (..)` - is not supported at the moment.

[]{#DDL-CREATE-INDEX}`CREATE INDEX`

    Generally `CREATE INDEX` is supported, but
    `CREATE UNIQUE INDEX ... WHERE`, i.e. partial unique
    indexes are not allowed.

[]{#DDL-CREATE-SEQUENCE}`CREATE SEQUENCE`

    Generally `CREATE SEQUENCE` is supported, but when using
    [BDR]\'s distributed sequences, some options are
    prohibited.

[]{#DDL-ALTER-SEQUENCE}`ALTER SEQUENCE`

    Generally `ALTER SEQUENCE` is supported, but when using
    [BDR]\'s distributed sequences, some options like
    `START` are prohibited. Several of them, like the
    aforementioned `START` can be specified during
    `CREATE SEQUENCE`.

## 8.2.5. How to work around restricted DDL

As noted in [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS),
BDR limits some kinds of DDL operations. In particular, an
`ALTER TABLE` that causes a full table rewrite is prohibited.

It\'s possible to split almost all such operations up into smaller
changes, but not always simple. The same decomposition into smaller
operations that\'s done for BDR is what\'s typically needed for
low-lock, low-downtime schema changes on high load systems, though.

### 8.2.5.1. Adding a column

Usually you can just `ALTER TABLE ... ADD COLUMN ...`. But you
cannot add columns with a `DEFAULT`, and therefore cannot add
`NOT NULL` columns to non-empty tables.

To add a column with a default:

-   `ALTER TABLE` *`thetable`*
    `ADD COLUMN`
    *`newcolumn coltype`*`;`. Note the lack of a
    `DEFAULT` or `NOT NULL`.

-   `ALTER TABLE` *`thetable`*
    `ALTER COLUMN` *`newcolumn`*
    `DEFAULT`
    *`default-expression`*`;`

-   `UPDATE` *`thetable`* `SET`
    *`newcolumn`* `=`
    *`default-expression`*`;` . For best results
    batch the update into chunks so you don\'t update more than a few
    tens or hundreds of thousands of rows at once.

-   If required, `ALTER TABLE` *`thetable`*
    `ALTER COLUMN` *`newcolumn`*
    `NOT NULL;`

This splits schema changes and row changes into separate transactions,
which performs [*much*] better with BDR and avoids the full
table rewrite limitations. It\'s fine to create the column and alter it
to add a default in a single transaction, but the update and final alter
should be separate transactions.

### 8.2.5.2. Changing a column\'s type

Similarly, to change a column\'s type:

-   `ALTER TABLE` *`thetable`*
    `ADD COLUMN` *`newcolumn`*
    *`newtype`* a column of the desired type

-   Create a `BEFORE INSERT OR UPDATE ON`
    *`thetable`* `FOR EACH ROW ..` trigger that
    assigns `NEW.`*`newcolumn`*
    `:= NEW.`*`oldcolumn`* so that new writes to
    the table update the new column too

-   `UPDATE` *`thetable`* the table in batches
    to copy the value of *`oldcolumn`* to
    *`newcolumn`*. Batching the work will help reduce
    replication lag if it\'s a big table, since BDR replicates strictly
    in commit order. Updating by range of IDs or whatever method you
    prefer is fine, or the whole table in one go for smaller tables.

-   `CREATE INDEX ...` any required indexes on the new column.
    It\'s safe to use `CREATE INDEX ... CONCURRENTLY` run
    individually without DDL replication on each node to reduce lock
    durations.

-   `ALTER` the column to add a `NOT NULL` and
    `CHECK` constraints, if required

-   If the column is `UNIQUE` or the `PRIMARY KEY`
    and is the target of one or more `FOREIGN KEY`
    constraints, repeat this process on each referencing table,
    recursively, such that all related tables have the new column fully
    populated before you:

-   `ALTER` the table to add any `FOREIGN KEY`
    constraints that were present on *`oldcolumn`*.

-   `BEGIN` a transaction, `DROP` the trigger you
    added, `ALTER TABLE` to add any `DEFAULT`
    required on the column, `DROP` the old column, and
    `ALTER TABLE` *`thetable`*
    `RENAME COLUMN` *`newcolumn`* `TO`
    *`oldcolumn`*, then `COMMIT`. Because
    you\'re dropping a column you may have to re-create views,
    procedures, etc that depend on the table. Be careful if you
    `CASCADE` drop the column, as you\'ll need to ensure you
    re-create everything that referred to it.



  ---------------------------------------------------- ------------------------------------------- ---------------------------------------
  [Prev](ddl-replication-advice.md)        [Home](README.md)        [Next](conflicts.md)  
  Executing DDL on BDR systems                          [Up](ddl-replication.md)                  Active-Active conflicts
  ---------------------------------------------------- ------------------------------------------- ---------------------------------------
