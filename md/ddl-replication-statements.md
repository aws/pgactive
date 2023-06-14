::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ----------------------------------------------------------------------------------- ------------------------------------------- ---------------------------- ----------------------------------------------------------------
  [Prev](ddl-replication-advice.md "Executing DDL on BDR systems"){accesskey="P"}   [Up](ddl-replication.md){accesskey="U"}    Chapter 8. DDL Replication    [Next](conflicts.md "Multi-master conflicts"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [8.2. Statement specific DDL replication concerns]{#DDL-REPLICATION-STATEMENTS} {#statement-specific-ddl-replication-concerns .SECT1}

Not all commands can be replicated automatically. Some are allowed
regardless - generally ones that have affect on more than one database.
Others are disallowed, and some have limitations and restrictions on
what can be used as compared to PostgreSQL without BDR.

::: IMPORTANT
> **Important:** Global DDL, like `CREATE ROLE`{.LITERAL},
> `CREATE USER`{.LITERAL} etc is [*not replicated*]{.emphasis} and
> should be applied on each node if the created objects will be
> referenced by a BDR-enabled database.
:::

::: SECT2
## [8.2.1. Statements with weaker DDL locking]{#AEN1489} {#statements-with-weaker-ddl-locking .SECT2}

Some statements don\'t require the full [DDL
lock](ddl-replication-advice.md#DDL-REPLICATION-LOCKING) and can
proceed with a weaker lock that prohibits other nodes from doing DDL at
the same time, but does not restrict concurrent DML (insert, update or
delete) operations.

-   `CREATE SCHEMA ...`{.LITERAL}

-   `CREATE [TABLE|VIEW] ...`{.LITERAL}

-   `CREATE FOREIGN TABLE ...`{.LITERAL}

-   `CREATE [AGGREGATE|OPERATOR|TYPE] ...`{.LITERAL}

-   `CREATE [OR REPLACE] FUNCTION ...`{.LITERAL}

-   `CREATE DOMAIN ...`{.LITERAL}

-   `ALTER DEFAULT PRIVILEGES ...`{.LITERAL}

-   `ALTER ... OWNER TO ...`{.LITERAL}
:::

::: SECT2
## [8.2.2. Not replicated DDL statements]{#AEN1519} {#not-replicated-ddl-statements .SECT2}

Some DDL statements, mainly those that affect objects that are
PostgreSQL-instance-wide rather than database-sepecific, are not
replicated. They are applied on the node that executes them without
taking the global DDL lock and are not sent to other nodes.

If you create non-replicated objects that are to be referenced by
replicated objects (e.g. creating a role, not replicated, then creating
a table, replicated, that\'s owned by that role) you must ensure that
the non-replicated object is created on all [BDR]{.PRODUCTNAME} nodes.
You can do this manually, by running the statement on each node. Or you
can use
[bdr.bdr_replicate_ddl_command](functions-node-mgmt.md#FUNCTION-BDR-REPLICATE-DDL-COMMAND)
to apply the statement on the local node and manually enqueue it for
replication on all nodes.

Using `bdr.bdr_replicate_ddl_command`{.FUNCTION} is the recommended
approach, e.g.:

``` PROGRAMLISTING
     SELECT bdr.bdr_replicate_ddl_command('CREATE USER myuser;');

```

::: NOTE
> **Note:** It is not necessary that the definition of objects like
> roles be the same on all nodes, only that they exist. You can for
> example `CREATE ROLE somerole WITH NOLOGIN`{.LITERAL} on most nodes,
> but on one node you can create them `WITH LOGIN`{.LITERAL}.
:::

The statements that are applied locally but not replicated are:

::: VARIABLELIST

[]{#DDL-CREATE-INDEX-CONCURRENTLY}`CREATE INDEX CONCURRENTLY`{.VARNAME}

:   `CREATE INDEX CONCURRENTLY`{.LITERAL} is not replicated. It requires
    a top-level transaction so it cannot be run via
    `bdr.bdr_replicate_ddl_command`{.LITERAL}, and is not currently
    supported for direct DDL capture.

    It\'s best to specify an index name and use the same name on all
    nodes. Otherwise a later `DROP INDEX`{.LITERAL} on one node may fail
    to replicate to the other nodes and cause replication to stall.

    Because this command is not replicated, but affects database objects
    that are, it requires that users explicitly set
    `bdr.skip_ddl_replication`{.LITERAL}, making it clear that the
    user/app knows it will not be replicated. This will prevent
    compatility breaks if in future BDR adds support for replicating the
    command. Use:

    ``` PROGRAMLISTING
           SET bdr.skip_ddl_replication = on;
            CREATE INDEX CONCURRENTLY idxname ON tblname(cols);
            RESET bdr.skip_ddl_replication;

    ```

[]{#DDL-DROP-INDEX-CONCURRENTLY}`DROP INDEX CONCURRENTLY`{.VARNAME}

:   `DROP INDEX CONCURRENTLY`{.LITERAL} is not replicated for the same
    reasons as `CREATE INDEX CONCURRENTLY`{.LITERAL}. The same
    requirements and workarounds apply.

[]{#DDL-CREATE-DATABASE}`CREATE DATABASE`{.VARNAME}

:   `CREATE DATABASE`{.LITERAL} cannot be replicated because
    [BDR]{.PRODUCTNAME} works on a per database level.

[]{#DDL-CREATE-ROLE}`CREATE ROLE/USER/GROUP`{.VARNAME}

:   `CREATE ROLE`{.LITERAL} cannot be replicated because
    [BDR]{.PRODUCTNAME} works on a per database level. It is possible
    that a workaround for this will be added.

    ::: WARNING
      --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
      **Warning**
      Not creating roles of the same name (not necessarily with the same access details otherwise though) on all systems can break replication when statements like `ALTER TABLE ... OWNER TO`{.LITERAL} are replicated.
      --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    :::

[]{#DDL-CREATE-TABLESPACE}`CREATE TABLESPACE`{.VARNAME}

:   `CREATE TABLESPACE`{.LITERAL} cannot be replicated because
    [BDR]{.PRODUCTNAME} works on a per database level.

    ::: WARNING
      ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
      **Warning**
      Not creating tablespaces of the same name (not necessarily with the same location though) on all systems can break replication when statements like `ALTER TABLE ... SET TABLESPACE`{.LITERAL} are replicated.
      ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    :::

[]{#DDL-DROP-DATABASE}`DROP DATABASE`{.VARNAME}

:   `DROP DATABASE`{.LITERAL} cannot be replicated because
    [BDR]{.PRODUCTNAME} works on a per database level.

    Note that a database that is configured for [BDR]{.PRODUCTNAME}
    cannot be dropped while that is the case.

[]{#DDL-DROP-TABLESPACE}`DROP TABLESPACE`{.VARNAME}

:   `DROP TABLESPACE`{.LITERAL} cannot be replicated because
    [BDR]{.PRODUCTNAME} works on a per database level.

    ::: WARNING
      -------------------------------------------------------------------------------------------------------------------------------------------------------------
      **Warning**
      Dropping tablespaces only on some nodes can cause problems when relations on other nodes are moved into the tablespace that does not exist locally anymore.
      -------------------------------------------------------------------------------------------------------------------------------------------------------------
    :::

[]{#DDL-DROP-ROLE}`DROP ROLE/USER/GROUP`{.VARNAME}

:   `DROP ROLE`{.LITERAL} cannot be replicated because
    [BDR]{.PRODUCTNAME} works on a per database level. It is possible
    that a workaround for this will be added.

    ::: WARNING
      ------------------------------------------------------------------------------------------------------------------------------------------
      **Warning**
      Dropping role only on some nodes can cause problems when objects on other nodes are assigned to roles that do not exist locally anymore.
      ------------------------------------------------------------------------------------------------------------------------------------------
    :::

[]{#DDL-ALTER-ROLE}`ALTER ROLE/USER/GROUP`{.VARNAME}

:   `ALTER ROLE`{.LITERAL} cannot be replicated because
    [BDR]{.PRODUCTNAME} works on a per database level. It is possible
    that a workaround for this will be added.

    Normally all commands but `ALTER ROLE ... RENAME TO ...`{.LITERAL}
    should be safe to execute in the sense that doing so won\'t cause
    replication to break.

    ::: WARNING
      -----------------------------------------------------------------------------------------------------------------------------
      **Warning**
      Renaming a role only on some nodes can lead to problems due to replicated DDL statements not being able to execute anymore.
      -----------------------------------------------------------------------------------------------------------------------------
    :::

[]{#DDL-ALTER-DATABASE}`ALTER DATABASE`{.VARNAME}

:   `ALTER DATABASE`{.LITERAL} cannot be replicated because
    [BDR]{.PRODUCTNAME} works on a per database level.

    In practice the primary problematic case is when trying to change
    settings on a per database basis using
    `ALTER DATABASE ... SET ...`{.LITERAL}, these have to be executed on
    every database for now.

    ::: WARNING
      -----------------------------------------------------------------------------------------------------------------
      **Warning**
      Renaming a database can lead to the connection information stored on some of the nodes not being valid anymore.
      -----------------------------------------------------------------------------------------------------------------
    :::

[]{#DDL-ALTER-TABLESPACE}`ALTER TABLESPACE`{.VARNAME}

:   `ALTER TABLSPACE`{.LITERAL} cannot be replicated because
    [BDR]{.PRODUCTNAME} works on a per database level. It is safe to
    execute on the individual nodes though.
:::
:::

::: SECT2
## [8.2.3. Prohibited DDL statements]{#DDL-REPLICATION-PROHIBITED-COMMANDS} {#prohibited-ddl-statements .SECT2}

BDR prevents some DDL statements from running when it is active on a
database. This protects the consistency of the system by disallowing
statements that cannot be replicated correctly, or for which replication
is not yet supported. Statements that are supported with some
restrictions are covered in [DDL statements with
restrictions](ddl-replication-statements.md#DDL-REPLICATION-RESTRICTED-COMMANDS);
commands that are entirely disallowed in [BDR]{.PRODUCTNAME} are covered
below.

Generally unsupported statements are prevented from being executed,
raising a `feature_not_supported`{.LITERAL} (SQLSTATE `0A000`{.LITERAL})
error.

The following DDL commands are rejected by [BDR]{.PRODUCTNAME} when
[BDR]{.PRODUCTNAME} is active on a database, and will fail with an
`ERROR`{.LITERAL}:

::: VARIABLELIST

[]{#DDL-CREATE-TABLE-AS}`CREATE TABLE AS/SELECT INTO`{.VARNAME}

:   `CREATE TABLE AS/SELECT INTO`{.LITERAL} are prohibited unless
    `UNLOGGED`{.LITERAL} or `UNLOGGED`{.LITERAL} temporary is specified.

[]{#DDL-CREATE-TABLE-OF-TYPE}`CREATE TABLE ... OF TYPE`{.VARNAME}

:   `CREATE TABLE ... OF TYPE`{.LITERAL} is prohibited unless
    `UNLOGGED`{.LITERAL} or `UNLOGGED`{.LITERAL} temporary is specified.

[]{#DDL-CREATE-TEXT-SEARCH-PARSER}`CREATE TEXT SEARCH PARSER`{.VARNAME}

:   `CREATE TEXT SEARCH PARSER`{.LITERAL} is prohibited.

[]{#DDL-CREATE-TEXT-SEARCH-DICTIONARY}`CREATE TEXT SEARCH DICTIONARY`{.VARNAME}

:   `CREATE TEXT SEARCH DICTIONARY`{.LITERAL} is prohibited.

[]{#DDL-ALTER-TEXT-SEARCH-DICTIONARY}`ALTER TEXT SEARCH DICTIONARY`{.VARNAME}

:   `ALTER TEXT SEARCH DICTIONARY`{.LITERAL} is prohibited.

[]{#DDL-CREATE-TEXT-SEARCH-TEMPLATE}`CREATE TEXT SEARCH TEMPLATE`{.VARNAME}

:   `CREATE TEXT SEARCH TEMPLATE`{.LITERAL} is prohibited.

[]{#DDL-CREATE-TEXT-SEARCH-CONFIGURATION}`CREATE TEXT SEARCH CONFIGURATION`{.VARNAME}

:   `CREATE TEXT SEARCH template`{.LITERAL} is prohibited.

[]{#DDL-ALTER-TEXT-SEARCH-CONFIGURATION}`ALTER TEXT SEARCH CONFIGURATION`{.VARNAME}

:   `ALTER TEXT SEARCH template`{.LITERAL} is prohibited.

[]{#DDL-CREATE-COLLATION}`CREATE COLLATION`{.VARNAME}

:   `CREATE CREATE COLLATION`{.LITERAL} is prohibited.

[]{#DDL-ALTER-EXTENSION}`ALTER EXTENSION`{.VARNAME}

:   `ALTER EXTENSION`{.LITERAL} currently is prohibited.

[]{#DDL-CREATE-FOREIGN-DATA-WRAPPER}`CREATE FOREIGN DATA WRAPPER`{.VARNAME}

:   `CREATE FOREIGN DATA WRAPPER`{.LITERAL} currently is prohibited.

[]{#DDL-ALTER-FOREIGN-DATA-WRAPPER}`ALTER FOREIGN DATA WRAPPER`{.VARNAME}

:   `ALTER FOREIGN DATA WRAPPER`{.LITERAL} currently is prohibited.

[]{#DDL-CREATE-SERVER}`CREATE SERVER`{.VARNAME}

:   `CREATE SERVER`{.LITERAL} currently is prohibited.

[]{#DDL-ALTER-SERVER}`ALTER SERVER`{.VARNAME}

:   `ALTER SERVER`{.LITERAL} currently is prohibited.

[]{#DDL-CREATE-USER-MAPPING}`CREATE USER MAPPING`{.VARNAME}

:   `CREATE USER MAPPING`{.LITERAL} currently is prohibited.

[]{#DDL-ALTER-USER-MAPPING}`ALTER USER MAPPING`{.VARNAME}

:   `ALTER USER MAPPING`{.LITERAL} currently is prohibited.

[]{#DDL-DROP-USER-MAPPING}`DROP USER MAPPING`{.VARNAME}

:   `DROP USER MAPPING`{.LITERAL} currently is prohibited.

[]{#DDL-CREATEH-MATERIALIZED-VIEW}`CREATE MATERIALIZED VIEW`{.VARNAME}

:   `CREATE MATERIALIZED VIEW`{.LITERAL} currently is prohibited.

[]{#DDL-REFRESH-MATERIALIZED-VIEW}`REFRESH MATERIALIZED VIEW`{.VARNAME}

:   `REFRESH MATERIALIZED VIEW`{.LITERAL} currently is prohibited.

[]{#DDL-CREATE-LANGUAGE}`CREATE LANGUAGE`{.VARNAME}

:   `CREATE LANGUAGE`{.LITERAL} currently is prohibited. Note that
    nearly all procedual languages are available as an extension and
    `CREATE EXTENSION`{.LITERAL} is supported.

[]{#DDL-CREATE-CONVERSION}`CREATE CONVERSION`{.VARNAME}

:   `CREATE CONVERSION`{.LITERAL} currently is prohibited.

[]{#DDL-CREATE-CAST}`CREATE CAST`{.VARNAME}

:   ::: NOTE
    > **Note:** `CREATE CAST`{.LITERAL} currently is prohibited. Note
    > that `CREATE CAST`{.LITERAL} inside an extension is supported.
    :::

[]{#DDL-CREATE-OPERATOR-FAMILY}`CREATE OPERATOR FAMILY`{.VARNAME}

:   ::: NOTE
    > **Note:** `CREATE OPERATOR FAMILY`{.LITERAL} currently is
    > prohibited. Note that `CREATE OPERATOR FAMILY`{.LITERAL} inside an
    > extension is supported.
    :::

[]{#DDL-ALTER-OPERATOR-FAMILY}`ALTER OPERATOR FAMILY`{.VARNAME}

:   `ALTER OPERATOR FAMILY`{.LITERAL} currently is prohibited.

    ::: NOTE
    > **Note:** Note that `ALTER OPERATOR FAMILY`{.LITERAL} inside an
    > extension is supported.
    :::

[]{#DDL-CREATE-OPERATOR-CLASS}`CREATE OPERATOR CLASS`{.VARNAME}

:   `CREATE OPERATOR CLASS`{.LITERAL} currently is prohibited.

    ::: NOTE
    > **Note:** Note that `CREATE OPERATOR CLASS`{.LITERAL} inside an
    > extension is supported.
    :::

[]{#DDL-DROP-OWNED}`DROP OWNED`{.VARNAME}

:   `DROP OWNED`{.LITERAL} is prohibited.

[]{#DDL-SECURITY-LABEL}`SECURITY LABEL`{.VARNAME}

:   Except for some [BDR]{.PRODUCTNAME} internal use
    `SECURITY LABEL`{.LITERAL} is prohibited.
:::
:::

::: SECT2
## [8.2.4. DDL statements with restrictions]{#DDL-REPLICATION-RESTRICTED-COMMANDS} {#ddl-statements-with-restrictions .SECT2}

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
`ALTER TABLE`{.LITERAL} that\'ll cause a full table rewrite, but it\'s
generally possible to rephrase that as a series of independent
`ALTER TABLE`{.LITERAL} and `UPDATE`{.LITERAL} statements that don\'t do
the full table rewrite. See
[*`ALTER TABLE`{.VARNAME}*](ddl-replication-statements.md#DDL-ALTER-TABLE)
below for details on that example.

::: NOTE
> **Note:** See [DDL locking details](technotes-ddl-locking.md) for
> additional details on why DDL locking is required and how it\'s done.
:::

Generally unsupported statements are prevented from being executed,
raising a `feature_not_supported`{.LITERAL} (SQLSTATE `0A000`{.LITERAL})
error.

The following statements or statement options are not currently
permitted when BDR is active on a database:

::: VARIABLELIST

[]{#DDL-CREATE-TABLE}`CREATE TABLE`{.VARNAME}

:   Generally `CREATE TABLE`{.LITERAL} is allowed. There are a few
    options/subcommands that are not supported.

    Not supported commands are:

    -   `WITH OIDS`{.LITERAL} - outdated option, not deemed worth to add
        support for

    -   `OF TYPE`{.LITERAL} - not supported yet

    -   `CONSTRAINT ... EXCLUDE`{.LITERAL} - not supported yet

[]{#DDL-ALTER-TABLE}`ALTER TABLE`{.VARNAME}

:   Generally `ALTER TABLE`{.LITERAL} commands are allowed. There are a
    however several sub-commands that are not supported, mainly those
    that perform a full-table re-write.

    Not supported commands are:

    -   `ADD COLUMN ... DEFAULT`{.LITERAL} - this option can
        unfortunately not be supported. It is however often possible to
        rewrite this into a series of supported commands; see [How to
        work around restricted
        DDL](ddl-replication-statements.md#DDL-REPLICATION-HOW).

    -   `ADD CONSTRAINT ... EXCLUDE`{.LITERAL} - exclusion are not
        supported for now. Exclusion constraints don\'t make much sense
        in an asynchronous system and lead to changes that cannot be
        replayed.

    -   `ALTER CONSTRAINT`{.LITERAL} - changing constraint settings is
        not supported for now.

    -   `ALTER COLUMN ... TYPE`{.LITERAL} - changing a column\'s type is
        not supported. Changing a column in a way that doesn\'t require
        table rewrites may be suppported at some point. See [How to work
        around restricted
        DDL](ddl-replication-statements.md#DDL-REPLICATION-HOW) for
        how to work around this limitation.

    -   `ENABLE .. RULE`{.LITERAL} - is not supported.

    -   `DISABLE .. RULE`{.LITERAL} - is not supported.

    -   `[NO] INHERIT`{.LITERAL} - is not supported.

    -   `[NOT] OF TYPE`{.LITERAL} - is not supported.

    -   `ALTER COLUMN ... SET (..)`{.LITERAL} - is not supported at the
        moment. Note however that ALTER COLUMN \... SET \[NOT\] NULL and
        ALTER COLUMN \... SET DEFAULT are supported.

    -   `SET (..)`{.LITERAL} - is not supported at the moment.

[]{#DDL-CREATE-INDEX}`CREATE INDEX`{.VARNAME}

:   Generally `CREATE INDEX`{.LITERAL} is supported, but
    `CREATE UNIQUE INDEX ... WHERE`{.LITERAL}, i.e. partial unique
    indexes are not allowed.

[]{#DDL-CREATE-SEQUENCE}`CREATE SEQUENCE`{.VARNAME}

:   Generally `CREATE SEQUENCE`{.LITERAL} is supported, but when using
    [BDR]{.PRODUCTNAME}\'s distributed sequences, some options are
    prohibited.

[]{#DDL-ALTER-SEQUENCE}`ALTER SEQUENCE`{.VARNAME}

:   Generally `ALTER SEQUENCE`{.LITERAL} is supported, but when using
    [BDR]{.PRODUCTNAME}\'s distributed sequences, some options like
    `START`{.LITERAL} are prohibited. Several of them, like the
    aforementioned `START`{.LITERAL} can be specified during
    `CREATE SEQUENCE`{.LITERAL}.
:::
:::

::: SECT2
## [8.2.5. How to work around restricted DDL]{#DDL-REPLICATION-HOW} {#how-to-work-around-restricted-ddl .SECT2}

As noted in [Prohibited DDL
statements](ddl-replication-statements.md#DDL-REPLICATION-PROHIBITED-COMMANDS),
BDR limits some kinds of DDL operations. In particular, an
`ALTER TABLE`{.LITERAL} that causes a full table rewrite is prohibited.

It\'s possible to split almost all such operations up into smaller
changes, but not always simple. The same decomposition into smaller
operations that\'s done for BDR is what\'s typically needed for
low-lock, low-downtime schema changes on high load systems, though.

::: SECT3
### [8.2.5.1. Adding a column]{#DDL-REPLICATION-ADDCOLUMN} {#adding-a-column .SECT3}

Usually you can just `ALTER TABLE ... ADD COLUMN ...`{.LITERAL}. But you
cannot add columns with a `DEFAULT`{.LITERAL}, and therefore cannot add
`NOT NULL`{.LITERAL} columns to non-empty tables.

To add a column with a default:

-   `ALTER TABLE`{.LITERAL} *`thetable`{.REPLACEABLE}*
    `ADD COLUMN`{.LITERAL}
    *`newcolumn coltype`{.REPLACEABLE}*`;`{.LITERAL}. Note the lack of a
    `DEFAULT`{.LITERAL} or `NOT NULL`{.LITERAL}.

-   `ALTER TABLE`{.LITERAL} *`thetable`{.REPLACEABLE}*
    `ALTER COLUMN`{.LITERAL} *`newcolumn`{.REPLACEABLE}*
    `DEFAULT`{.LITERAL}
    *`default-expression`{.REPLACEABLE}*`;`{.LITERAL}

-   `UPDATE`{.LITERAL} *`thetable`{.REPLACEABLE}* `SET`{.LITERAL}
    *`newcolumn`{.REPLACEABLE}* `=`{.LITERAL}
    *`default-expression`{.REPLACEABLE}*`;`{.LITERAL} . For best results
    batch the update into chunks so you don\'t update more than a few
    tens or hundreds of thousands of rows at once.

-   If required, `ALTER TABLE`{.LITERAL} *`thetable`{.REPLACEABLE}*
    `ALTER COLUMN`{.LITERAL} *`newcolumn`{.REPLACEABLE}*
    `NOT NULL;`{.LITERAL}

This splits schema changes and row changes into separate transactions,
which performs [*much*]{.emphasis} better with BDR and avoids the full
table rewrite limitations. It\'s fine to create the column and alter it
to add a default in a single transaction, but the update and final alter
should be separate transactions.
:::

::: SECT3
### [8.2.5.2. Changing a column\'s type]{#DDL-REPLICATION-ALTERTYPE} {#changing-a-columns-type .SECT3}

Similarly, to change a column\'s type:

-   `ALTER TABLE`{.LITERAL} *`thetable`{.REPLACEABLE}*
    `ADD COLUMN`{.LITERAL} *`newcolumn`{.REPLACEABLE}*
    *`newtype`{.REPLACEABLE}* a column of the desired type

-   Create a `BEFORE INSERT OR UPDATE ON`{.LITERAL}
    *`thetable`{.REPLACEABLE}* `FOR EACH ROW ..`{.LITERAL} trigger that
    assigns `NEW.`{.LITERAL}*`newcolumn`{.REPLACEABLE}*
    `:= NEW.`{.LITERAL}*`oldcolumn`{.REPLACEABLE}* so that new writes to
    the table update the new column too

-   `UPDATE`{.LITERAL} *`thetable`{.REPLACEABLE}* the table in batches
    to copy the value of *`oldcolumn`{.REPLACEABLE}* to
    *`newcolumn`{.REPLACEABLE}*. Batching the work will help reduce
    replication lag if it\'s a big table, since BDR replicates strictly
    in commit order. Updating by range of IDs or whatever method you
    prefer is fine, or the whole table in one go for smaller tables.

-   `CREATE INDEX ...`{.LITERAL} any required indexes on the new column.
    It\'s safe to use `CREATE INDEX ... CONCURRENTLY`{.LITERAL} run
    individually without DDL replication on each node to reduce lock
    durations.

-   `ALTER`{.LITERAL} the column to add a `NOT NULL`{.LITERAL} and
    `CHECK`{.LITERAL} constraints, if required

-   If the column is `UNIQUE`{.LITERAL} or the `PRIMARY KEY`{.LITERAL}
    and is the target of one or more `FOREIGN KEY`{.LITERAL}
    constraints, repeat this process on each referencing table,
    recursively, such that all related tables have the new column fully
    populated before you:

-   `ALTER`{.LITERAL} the table to add any `FOREIGN KEY`{.LITERAL}
    constraints that were present on *`oldcolumn`{.REPLACEABLE}*.

-   `BEGIN`{.LITERAL} a transaction, `DROP`{.LITERAL} the trigger you
    added, `ALTER TABLE`{.LITERAL} to add any `DEFAULT`{.LITERAL}
    required on the column, `DROP`{.LITERAL} the old column, and
    `ALTER TABLE`{.LITERAL} *`thetable`{.REPLACEABLE}*
    `RENAME COLUMN`{.LITERAL} *`newcolumn`{.REPLACEABLE}* `TO`{.LITERAL}
    *`oldcolumn`{.REPLACEABLE}*, then `COMMIT`{.LITERAL}. Because
    you\'re dropping a column you may have to re-create views,
    procedures, etc that depend on the table. Be careful if you
    `CASCADE`{.LITERAL} drop the column, as you\'ll need to ensure you
    re-create everything that referred to it.
:::
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ---------------------------------------------------- ------------------------------------------- ---------------------------------------
  [Prev](ddl-replication-advice.md){accesskey="P"}        [Home](index.md){accesskey="H"}        [Next](conflicts.md){accesskey="N"}
  Executing DDL on BDR systems                          [Up](ddl-replication.md){accesskey="U"}                   Multi-master conflicts
  ---------------------------------------------------- ------------------------------------------- ---------------------------------------
:::
