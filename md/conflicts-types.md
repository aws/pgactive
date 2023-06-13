::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ------------------------------------------------------------------ ------------------------------------- ----------------------------------- ------------------------------------------------------------------------------------
  [Prev](conflicts-how.md "How conflicts happen"){accesskey="P"}   [Up](conflicts.md){accesskey="U"}    Chapter 9. Multi-master conflicts    [Next](conflicts-avoidance.md "Avoiding or tolerating conflicts"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [9.2. Types of conflict]{#CONFLICTS-TYPES} {#types-of-conflict .SECT1}

::: SECT2
## [9.2.1. `PRIMARY KEY`{.LITERAL} or `UNIQUE`{.LITERAL} conflicts]{#CONFLICTS-KEY} {#primary-key-or-unique-conflicts .SECT2}

The most common conflicts are row conflicts where two operations affect
a row with the same key in ways they could not do on a single node.
[BDR]{.PRODUCTNAME} can detect most of those and apply last-update-wins
conflict handling or invoke a user-defined conflict handler.

Row conflicts include:

-   `INSERT`{.LITERAL} vs `INSERT`{.LITERAL}

-   `INSERT`{.LITERAL} vs `UPDATE`{.LITERAL}

-   `UPDATE`{.LITERAL} vs `DELETE`{.LITERAL}

-   `INSERT`{.LITERAL} vs `DELETE`{.LITERAL}

-   `DELETE`{.LITERAL} vs `DELETE`{.LITERAL}

::: SECT3
### [9.2.1.1. INSERT/INSERT conflicts]{#CONFLICTS-INSERT-INSERT} {#insertinsert-conflicts .SECT3}

The most common conflict, `INSERT`{.LITERAL} vs `INSERT`{.LITERAL},
arises where `INSERT`{.LITERAL}s on two different nodes create a tuple
with the same `PRIMARY KEY`{.LITERAL} values (or the same values for a
single `UNIQUE`{.LITERAL} constraint if no `PRIMARY KEY`{.LITERAL}
exists). [BDR]{.PRODUCTNAME} handles this by retaining the most recently
inserted tuple of the two according to the originating host\'s
timestamps unless a user-defined conflict handler overrides this.

No special administrator action is required to deal with these
conflicts, but the user must understand that [*one of the
`INSERT`{.LITERAL}ed tuples is effectively discarded on all
nodes*]{.emphasis} - there is no data merging done unless a user defined
conflict handler does it.

Conflict handling is only possible when [*only one constraint is
violated by the incoming insert from the remote node*]{.emphasis};
[INSERTs that violate multiple UNIQUE
constraints](conflicts-types.md#CONFLICTS-INSERT-UNIQUE-MULTIPLE-INDEX)
are more problematic.
:::

::: SECT3
### [9.2.1.2. INSERTs that violate multiple UNIQUE constraints]{#CONFLICTS-INSERT-UNIQUE-MULTIPLE-INDEX} {#inserts-that-violate-multiple-unique-constraints .SECT3}

An `INSERT`{.LITERAL}/`INSERT`{.LITERAL} conflict can violate more than
one `UNIQUE`{.LITERAL} constraint (of which one might be the
`PRIMARY KEY`{.LITERAL}).

[BDR]{.PRODUCTNAME} can only handle an
`INSERT`{.LITERAL}/`INSERT`{.LITERAL} conflict on one unique constraint
(including the `PRIMARY KEY`{.LITERAL}). If a new row conflicts with
more than one `UNIQUE`{.LITERAL} constraint then the apply worker
that\'s trying to apply the change will `ERROR`{.LITERAL} out with:

``` PROGRAMLISTING
     ERROR: multiple unique constraints violated by remotely INSERTed tuple

```

(Older versions would report a
`"diverging uniqueness conflict"`{.LITERAL} error instead).

In case of such a conflict, you must manually remove the conflicting
tuple(s) from the local side by `DELETE`{.LITERAL}ing it or by
`UPDATE`{.LITERAL}ing it so that it no longer conflicts with the new
remote tuple. There may be more than one conflicting tuple. There is not
currently any built-in facility to ignore, discard or merge tuples that
violate more than one local unique constraint.

See also: [UPDATEs that violate multiple UNIQUE
constraints](conflicts-types.md#CONFLICTS-UPDATE-UNIQUE-MULTIPLE-INDEX)
:::

::: SECT3
### [9.2.1.3. UPDATE/UPDATE conflicts]{#CONFLICTS-UPDATE-UPDATE} {#updateupdate-conflicts .SECT3}

Where two concurrent `UPDATE`{.LITERAL}s on different nodes change the
same tuple (but not its `PRIMARY KEY`{.LITERAL}), an
`UPDATE`{.LITERAL}/`UPDATE`{.LITERAL} conflict occurs on replay. These
are resolved using last-update-wins handling or, if it exists, a
user-defined conflict handler.

Because a `PRIMARY KEY`{.LITERAL} must exist in order to match tuples
and perform conflict resolution, `UPDATE`{.LITERAL}s are rejected on
tables without a `PRIMARY KEY`{.LITERAL} with:

``` PROGRAMLISTING
      ERROR: Cannot run UPDATE or DELETE on table (tablename) because it does not have a primary key.

```
:::

::: SECT3
### [9.2.1.4. UPDATE conflicts on the PRIMARY KEY]{#CONFLICTS-UPDATE-PK} {#update-conflicts-on-the-primary-key .SECT3}

[BDR]{.PRODUCTNAME} cannot currently perform last-update-wins conflict
resolution where the `PRIMARY KEY`{.LITERAL} is changed by an
`UPDATE`{.LITERAL} operation. It is permissible to update the primary
key, but you must ensure that no conflict with existing values is
possible.

Conflicts on update of the primary key are divergent conflicts that
require manual operator intervention; see [Divergent
conflicts](conflicts-types.md#CONFLICTS-DIVERGENT).
:::

::: SECT3
### [9.2.1.5. UPDATEs that violate multiple UNIQUE constraints]{#CONFLICTS-UPDATE-UNIQUE-MULTIPLE-INDEX} {#updates-that-violate-multiple-unique-constraints .SECT3}

Like [INSERTs that violate multiple UNIQUE
constraints](conflicts-types.md#CONFLICTS-INSERT-UNIQUE-MULTIPLE-INDEX),
where an incoming `UPDATE`{.LITERAL} violates more than one
`UNIQUE`{.LITERAL} index (and/or the `PRIMARY KEY`{.LITERAL}),
[BDR]{.PRODUCTNAME} cannot apply last-update-wins conflict resolution.

This is a divergent conflict that will require operator intervention;
see [Divergent conflicts](conflicts-types.md#CONFLICTS-DIVERGENT).
:::

::: SECT3
### [9.2.1.6. UPDATE/DELETE conflicts]{#CONFLICTS-UPDATE-DELETE} {#updatedelete-conflicts .SECT3}

It is possible for one node to `UPDATE`{.LITERAL} a row that another
node simultaneously `DELETE`{.LITERAL}s. In this case a
`UPDATE`{.LITERAL}/`DELETE`{.LITERAL} conflict occurs on replay. The
resolution of this conflict is to discard any `UPDATE`{.LITERAL} that
arrives after the `DELETE`{.LITERAL} unless a user-defined conflict
handler specifies otherwise.

Because a `PRIMARY KEY`{.LITERAL} must exist in order to match tuples
and perform conflict resolution, `DELETE`{.LITERAL}s are rejected on
tables without a `PRIMARY KEY`{.LITERAL} with:

``` PROGRAMLISTING
      ERROR: Cannot run UPDATE or DELETE on table (tablename) because it does not have a primary key.

```

::: NOTE
> **Note:** [BDR]{.PRODUCTNAME} cannot currently differentiate between
> `UPDATE`{.LITERAL}/`DELETE`{.LITERAL} conflicts and [INSERT/UPDATE
> conflicts](conflicts-types.md#CONFLICTS-INSERT-UPDATE). In both
> cases an `UPDATE`{.LITERAL} arrives that affects a nonexistent row.
> Since [BDR]{.PRODUCTNAME} is asynchronous and there\'s no ordering of
> replay between nodes, it can\'t tell if this is an update to a new row
> we haven\'t yet received the insert for, or a row we\'ve already
> replayed a delete for. In both cases the resolution is the same - the
> update is discarded.
:::
:::

::: SECT3
### [9.2.1.7. INSERT/UPDATE conflicts]{#CONFLICTS-INSERT-UPDATE} {#insertupdate-conflicts .SECT3}

If one node `INSERT`{.LITERAL}s a row which is then replayed to a 2nd
node and `UPDATE`{.LITERAL}d there, a 3rd node may receive the
`UPDATE`{.LITERAL} from the 2nd node before it receives the
`INSERT`{.LITERAL} from the 1st node. This is an
`INSERT`{.LITERAL}/`UPDATE`{.LITERAL} conflict.

Unless a user defined conflict trigger determines otherwise these
conflicts are handled by discarding the `UPDATE`{.LITERAL}. This can
lead to [*different data on different nodes*]{.emphasis}. See
[UPDATE/DELETE conflicts](conflicts-types.md#CONFLICTS-UPDATE-DELETE)
for details.
:::

::: SECT3
### [9.2.1.8. DELETE/DELETE conflicts]{#CONFLICTS-DELETE-DELETE} {#deletedelete-conflicts .SECT3}

A `DELETE`{.LITERAL}/`DELETE`{.LITERAL} conflict arises where two
different nodes concurrently delete the same tuple.

This conflict is harmless since both `DELETE`{.LITERAL}s have the same
effect, so one of them can be safely ignored.
:::
:::

::: SECT2
## [9.2.2. Foreign Key Constraint conflicts]{#CONFLICTS-FOREIGN-KEY} {#foreign-key-constraint-conflicts .SECT2}

Conflicts between a remote transaction being applied and existing local
data can also occur for `FOREIGN KEY`{.LITERAL} constraints. These
conflicts are usually transient issues that arise from transactions
being applied in a different order to the order they appeared to occur
logically on the nodes that originated them.

BDR applies changes with
`session_replication_role = 'replica'`{.LITERAL} so foreign keys are
[*not*]{.emphasis} checked when applying changes. In a multi-master
environment this can result in FK violations. Most are transient and
only exist until replication catches up with changes from another node,
but it\'s also possible to create dangling FKs because there\'s no
inter-node row locking in BDR. This is a necessary consequence of a
partition-tolerant asynchronous multi-master system, since one node can
insert and commit a new child row in some FK relationship at the same
time another node concurrently deletes the parent row. It\'s recommended
that use of FKs be constrained to sets of closely related entities that
are generally modified from only one node, are infrequently modified, or
where the modification\'s concurrency is application-mediated.

It\'s also possible to `bdr.acquire_global_lock('ddl')`{.LITERAL} to
lock out other nodes from making concurrent changes, but this is a
heavyweight option and must be done in [*all*]{.emphasis} transactions
that may modify the related tables. So it\'s best used for rarely
modified data where consistency is crucial.
:::

::: SECT2
## [9.2.3. Exclusion constraint conflicts]{#CONFLICTS-EXCLUSION} {#exclusion-constraint-conflicts .SECT2}

[BDR]{.PRODUCTNAME} doesn\'t support exclusion constraints and restricts
their creation.

::: IMPORTANT
> **Important:** If an existing stand-alone database is converted to a
> [BDR]{.PRODUCTNAME} database then all exclusion constraints should be
> manually dropped.
:::

In a distributed asynchronous system it is not possible to ensure that
no set of rows that violates the constraint exists, because all
transactions on different nodes are fully isolated. Exclusion
constraints would lead to replay deadlocks where replay could not
progress from any node to any other node because of exclusion constraint
violations.

If you force [BDR]{.PRODUCTNAME} to create an exclusion constraint, or
you don\'t drop existing ones when converting a standalone database to
[BDR]{.PRODUCTNAME} you should expect replication to break. You can get
it to progress again by removing or altering the local tuple(s) that an
incoming remote tuple conflicts with so that the remote transaction can
be applied.
:::

::: SECT2
## [9.2.4. Global data conflicts]{#AEN2392} {#global-data-conflicts .SECT2}

Conflicts can also arise where nodes have global
(PostgreSQL-system-wide) data, like roles, that differs. This can result
in operations - mainly DDL - that can be run successfully and committed
on one node, but then fail to apply to other nodes.

For example, node1 might have a user named `fred`{.LITERAL}, but that
user was not created on node2. [BDR]{.PRODUCTNAME} does not replicate
`CREATE USER`{.LITERAL} (see
[*`CREATE ROLE/USER/GROUP`{.VARNAME}*](ddl-replication-statements.md#DDL-CREATE-ROLE)
) so this situation can arise easily. If `fred`{.LITERAL} on node1
creates a table, it will be replicated with its owner set to
`fred`{.LITERAL}. When the DDL command is applied to node2 the DDL will
fail because there is no user named `fred`{.LITERAL}. This failure will
emit an `ERROR`{.LITERAL} in the PostgreSQL logs on node2 and increment
[bdr.pg_stat_bdr](catalog-pg-stat-bdr.md)`.nr_rollbacks`{.LITERAL}.

Administrator intervention is required to resolve this conflict by
creating the user `fred`{.LITERAL} on node2. (It need not have the same
permissions, but must exist).
:::

::: SECT2
## [9.2.5. Lock conflicts and deadlock aborts]{#AEN2413} {#lock-conflicts-and-deadlock-aborts .SECT2}

Because [BDR]{.PRODUCTNAME} apply processes operate very like normal
user sessions they are subject to the usual rules around row and table
locking. This can sometimes lead to [BDR]{.PRODUCTNAME} apply processes
waiting on locks held by user transactions, or even by each other.

Relevant locking includes;

-   explicit table-level locking (`LOCK TABLE ...`{.LITERAL}) by user
    sessions

-   explicit row level locking
    (`SELECT ... FOR UPDATE/FOR SHARE`{.LITERAL}) by user sessions

-   locking from foreign keys

-   implicit locking because of row `UPDATE`{.LITERAL}s,
    `INSERT`{.LITERAL}s or `DELETE`{.LITERAL}s, either from local
    activity or apply from other servers

It is even possible for a [BDR]{.PRODUCTNAME} apply process to deadlock
with a user transaction, where the user transaction is waiting on a lock
held by the apply process and vice versa. Two apply processes may also
deadlock with each other. PostgreSQL\'s deadlock detector will step in
and terminate one of the problem transactions. If the
[BDR]{.PRODUCTNAME} apply worker\'s process is terminated it will simply
retry and generally succeed.

All these issues are transient and generally require no administrator
action. If an apply process is stuck for a long time behind a lock on an
idle user session the administrator may choose to terminate the user
session to get replication flowing again, but this is no different to a
user holding a long lock that impacts another user session.

Use of the
[log_lock_waits](http://www.postgresql.org/docs/current/static/runtime-config-logging.html#GUC-LOG-LOCK-WAITS)
facility in PostgreSQL can help identify locking related replay stalls.
:::

::: SECT2
## [9.2.6. Divergent conflicts]{#CONFLICTS-DIVERGENT} {#divergent-conflicts .SECT2}

Divergent conflicts arise when data that should be the same on different
nodes differs unexpectedly. Divergent conflicts should not occur, but
not all such conflicts can be reliably prevented at time of writing.

::: WARNING
  -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  **Warning**
  Changing the `PRIMARY KEY`{.LITERAL} of a row can lead to a divergent conflict if another node changes the key of the same row before all nodes have replayed the change. Avoid changing primary keys, or change them only on one designated node. See [UPDATE conflicts on the PRIMARY KEY](conflicts-types.md#CONFLICTS-UPDATE-PK).
  -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
:::

Divergent conflicts involving row data generally require administrator
action to manually adjust the data on one of the nodes to be consistent
with the other one while replication is temporarily disabled using
[bdr.do_not_replicate](bdr-configuration-variables.md#GUC-BDR-DO-NOT-REPLICATE).
Such conflicts should not arise so long as [BDR]{.PRODUCTNAME} is used
as documented and settings or functions marked as unsafe are avoided.

The administrator must manually resolve such conflicts. Use of the
advanced options
[bdr.do_not_replicate](bdr-configuration-variables.md#GUC-BDR-DO-NOT-REPLICATE),
[bdr.skip_ddl_replication](bdr-configuration-variables.md#GUC-BDR-SKIP-DDL-REPLICATION)
and
[bdr.permit_unsafe_ddl_commands](bdr-configuration-variables.md#GUC-BDR-PERMIT-UNSAFE-DDL-COMMANDS)
may be required depending on the nature of the conflict. However,
careless use of these options can make things much worse and it isn\'t
possible to give general instructions for resolving all possible kinds
of conflict.
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------- ------------------------------------- -------------------------------------------------
  [Prev](conflicts-how.md){accesskey="P"}     [Home](index.md){accesskey="H"}     [Next](conflicts-avoidance.md){accesskey="N"}
  How conflicts happen                         [Up](conflicts.md){accesskey="U"}                   Avoiding or tolerating conflicts
  ------------------------------------------- ------------------------------------- -------------------------------------------------
:::
