  [BDR 2.0.7 Documentation](README.md)                                                                                                         
  [Prev](conflicts-how.md "How conflicts happen")   [Up](conflicts.md)    Chapter 9. Active-Active conflicts    [Next](conflicts-avoidance.md "Avoiding or tolerating conflicts")  


# 9.2. Types of conflict

## 9.2.1. `PRIMARY KEY` or `UNIQUE` conflicts

The most common conflicts are row conflicts where two operations affect
a row with the same key in ways they could not do on a single node.
[BDR] can detect most of those and apply last-update-wins
conflict handling or invoke a user-defined conflict handler.

Row conflicts include:

-   `INSERT` vs `INSERT`

-   `INSERT` vs `UPDATE`

-   `UPDATE` vs `DELETE`

-   `INSERT` vs `DELETE`

-   `DELETE` vs `DELETE`

### 9.2.1.1. INSERT/INSERT conflicts

The most common conflict, `INSERT` vs `INSERT`,
arises where `INSERT`s on two different nodes create a tuple
with the same `PRIMARY KEY` values (or the same values for a
single `UNIQUE` constraint if no `PRIMARY KEY`
exists). [BDR] handles this by retaining the most recently
inserted tuple of the two according to the originating host\'s
timestamps unless a user-defined conflict handler overrides this.

No special administrator action is required to deal with these
conflicts, but the user must understand that [*one of the
`INSERT`ed tuples is effectively discarded on all
nodes*] - there is no data merging done unless a user defined
conflict handler does it.

Conflict handling is only possible when [*only one constraint is
violated by the incoming insert from the remote node*];
[INSERTs that violate multiple UNIQUE
constraints](conflicts-types.md#CONFLICTS-INSERT-UNIQUE-MULTIPLE-INDEX)
are more problematic.

### 9.2.1.2. INSERTs that violate multiple UNIQUE constraints

An `INSERT`/`INSERT` conflict can violate more than
one `UNIQUE` constraint (of which one might be the
`PRIMARY KEY`).

[BDR] can only handle an
`INSERT`/`INSERT` conflict on one unique constraint
(including the `PRIMARY KEY`). If a new row conflicts with
more than one `UNIQUE` constraint then the apply worker
that\'s trying to apply the change will `ERROR` out with:

``` PROGRAMLISTING
     ERROR: multiple unique constraints violated by remotely INSERTed tuple
     
```

(Older versions would report a
`"diverging uniqueness conflict"` error instead).

In case of such a conflict, you must manually remove the conflicting
tuple(s) from the local side by `DELETE`ing it or by
`UPDATE`ing it so that it no longer conflicts with the new
remote tuple. There may be more than one conflicting tuple. There is not
currently any built-in facility to ignore, discard or merge tuples that
violate more than one local unique constraint.

See also: [UPDATEs that violate multiple UNIQUE
constraints](conflicts-types.md#CONFLICTS-UPDATE-UNIQUE-MULTIPLE-INDEX)

### 9.2.1.3. UPDATE/UPDATE conflicts

Where two concurrent `UPDATE`s on different nodes change the
same tuple (but not its `PRIMARY KEY`), an
`UPDATE`/`UPDATE` conflict occurs on replay. These
are resolved using last-update-wins handling or, if it exists, a
user-defined conflict handler.

Because a `PRIMARY KEY` must exist in order to match tuples
and perform conflict resolution, `UPDATE`s are rejected on
tables without a `PRIMARY KEY` with:

``` PROGRAMLISTING
      ERROR: Cannot run UPDATE or DELETE on table (tablename) because it does not have a primary key.
     
```

### 9.2.1.4. UPDATE conflicts on the PRIMARY KEY

[BDR] cannot currently perform last-update-wins conflict
resolution where the `PRIMARY KEY` is changed by an
`UPDATE` operation. It is permissible to update the primary
key, but you must ensure that no conflict with existing values is
possible.

Conflicts on update of the primary key are divergent conflicts that
require manual operator intervention; see [Divergent
conflicts](conflicts-types.md#CONFLICTS-DIVERGENT).

### 9.2.1.5. UPDATEs that violate multiple UNIQUE constraints

Like [INSERTs that violate multiple UNIQUE
constraints](conflicts-types.md#CONFLICTS-INSERT-UNIQUE-MULTIPLE-INDEX),
where an incoming `UPDATE` violates more than one
`UNIQUE` index (and/or the `PRIMARY KEY`),
[BDR] cannot apply last-update-wins conflict resolution.

This is a divergent conflict that will require operator intervention;
see [Divergent conflicts](conflicts-types.md#CONFLICTS-DIVERGENT).

### 9.2.1.6. UPDATE/DELETE conflicts

It is possible for one node to `UPDATE` a row that another
node simultaneously `DELETE`s. In this case a
`UPDATE`/`DELETE` conflict occurs on replay. The
resolution of this conflict is to discard any `UPDATE` that
arrives after the `DELETE` unless a user-defined conflict
handler specifies otherwise.

Because a `PRIMARY KEY` must exist in order to match tuples
and perform conflict resolution, `DELETE`s are rejected on
tables without a `PRIMARY KEY` with:

``` PROGRAMLISTING
      ERROR: Cannot run UPDATE or DELETE on table (tablename) because it does not have a primary key.
     
```

> **Note:** [BDR] cannot currently differentiate between
> `UPDATE`/`DELETE` conflicts and [INSERT/UPDATE
> conflicts](conflicts-types.md#CONFLICTS-INSERT-UPDATE). In both
> cases an `UPDATE` arrives that affects a nonexistent row.
> Since [BDR] is asynchronous and there\'s no ordering of
> replay between nodes, it can\'t tell if this is an update to a new row
> we haven\'t yet received the insert for, or a row we\'ve already
> replayed a delete for. In both cases the resolution is the same - the
> update is discarded.

### 9.2.1.7. INSERT/UPDATE conflicts

If one node `INSERT`s a row which is then replayed to a 2nd
node and `UPDATE`d there, a 3rd node may receive the
`UPDATE` from the 2nd node before it receives the
`INSERT` from the 1st node. This is an
`INSERT`/`UPDATE` conflict.

Unless a user defined conflict trigger determines otherwise these
conflicts are handled by discarding the `UPDATE`. This can
lead to [*different data on different nodes*]. See
[UPDATE/DELETE conflicts](conflicts-types.md#CONFLICTS-UPDATE-DELETE)
for details.

### 9.2.1.8. DELETE/DELETE conflicts

A `DELETE`/`DELETE` conflict arises where two
different nodes concurrently delete the same tuple.

This conflict is harmless since both `DELETE`s have the same
effect, so one of them can be safely ignored.

## 9.2.2. Foreign Key Constraint conflicts

Conflicts between a remote transaction being applied and existing local
data can also occur for `FOREIGN KEY` constraints. These
conflicts are usually transient issues that arise from transactions
being applied in a different order to the order they appeared to occur
logically on the nodes that originated them.

BDR applies changes with
`session_replication_role = 'replica'` so foreign keys are
[*not*] checked when applying changes. In a Active-Active
environment this can result in FK violations. Most are transient and
only exist until replication catches up with changes from another node,
but it\'s also possible to create dangling FKs because there\'s no
inter-node row locking in BDR. This is a necessary consequence of a
partition-tolerant asynchronous Active-Active system, since one node can
insert and commit a new child row in some FK relationship at the same
time another node concurrently deletes the parent row. It\'s recommended
that use of FKs be constrained to sets of closely related entities that
are generally modified from only one node, are infrequently modified, or
where the modification\'s concurrency is application-mediated.

It\'s also possible to `bdr.acquire_global_lock('ddl')` to
lock out other nodes from making concurrent changes, but this is a
heavyweight option and must be done in [*all*] transactions
that may modify the related tables. So it\'s best used for rarely
modified data where consistency is crucial.

## 9.2.3. Exclusion constraint conflicts

[BDR] doesn\'t support exclusion constraints and restricts
their creation.

> **Important:** If an existing stand-alone database is converted to a
> [BDR] database then all exclusion constraints should be
> manually dropped.

In a distributed asynchronous system it is not possible to ensure that
no set of rows that violates the constraint exists, because all
transactions on different nodes are fully isolated. Exclusion
constraints would lead to replay deadlocks where replay could not
progress from any node to any other node because of exclusion constraint
violations.

If you force [BDR] to create an exclusion constraint, or
you don\'t drop existing ones when converting a standalone database to
[BDR] you should expect replication to break. You can get
it to progress again by removing or altering the local tuple(s) that an
incoming remote tuple conflicts with so that the remote transaction can
be applied.

## 9.2.4. Global data conflicts

Conflicts can also arise where nodes have global
(PostgreSQL-system-wide) data, like roles, that differs. This can result
in operations - mainly DDL - that can be run successfully and committed
on one node, but then fail to apply to other nodes.

For example, node1 might have a user named `fred`, but that
user was not created on node2. [BDR] does not replicate
`CREATE USER` (see
[*`CREATE ROLE/USER/GROUP`*](ddl-replication-statements.md#DDL-CREATE-ROLE)
) so this situation can arise easily. If `fred` on node1
creates a table, it will be replicated with its owner set to
`fred`. When the DDL command is applied to node2 the DDL will
fail because there is no user named `fred`. This failure will
emit an `ERROR` in the PostgreSQL logs on node2 and increment
[bdr.pg_stat_bdr](catalog-pg-stat-bdr.md)`.nr_rollbacks`.

Administrator intervention is required to resolve this conflict by
creating the user `fred` on node2. (It need not have the same
permissions, but must exist).

## 9.2.5. Lock conflicts and deadlock aborts

Because [BDR] apply processes operate very like normal
user sessions they are subject to the usual rules around row and table
locking. This can sometimes lead to [BDR] apply processes
waiting on locks held by user transactions, or even by each other.

Relevant locking includes;

-   explicit table-level locking (`LOCK TABLE ...`) by user
    sessions

-   explicit row level locking
    (`SELECT ... FOR UPDATE/FOR SHARE`) by user sessions

-   locking from foreign keys

-   implicit locking because of row `UPDATE`s,
    `INSERT`s or `DELETE`s, either from local
    activity or apply from other servers

It is even possible for a [BDR] apply process to deadlock
with a user transaction, where the user transaction is waiting on a lock
held by the apply process and vice versa. Two apply processes may also
deadlock with each other. PostgreSQL\'s deadlock detector will step in
and terminate one of the problem transactions. If the
[BDR] apply worker\'s process is terminated it will simply
retry and generally succeed.

All these issues are transient and generally require no administrator
action. If an apply process is stuck for a long time behind a lock on an
idle user session the administrator may choose to terminate the user
session to get replication flowing again, but this is no different to a
user holding a long lock that impacts another user session.

Use of the
[log_lock_waits](http://www.postgresql.org/docs/current/static/runtime-config-logging.html#GUC-LOG-LOCK-WAITS)
facility in PostgreSQL can help identify locking related replay stalls.

## 9.2.6. Divergent conflicts

Divergent conflicts arise when data that should be the same on different
nodes differs unexpectedly. Divergent conflicts should not occur, but
not all such conflicts can be reliably prevented at time of writing.

  **Warning**
  Changing the `PRIMARY KEY` of a row can lead to a divergent conflict if another node changes the key of the same row before all nodes have replayed the change. Avoid changing primary keys, or change them only on one designated node. See [UPDATE conflicts on the PRIMARY KEY](conflicts-types.md#CONFLICTS-UPDATE-PK).

Divergent conflicts involving row data generally require administrator
action to manually adjust the data on one of the nodes to be consistent
with the other one while replication is temporarily disabled using
[bdr.do_not_replicate](bdr-configuration-variables.md#GUC-BDR-DO-NOT-REPLICATE).
Such conflicts should not arise so long as [BDR] is used
as documented and settings or functions marked as unsafe are avoided.

The administrator must manually resolve such conflicts. Use of the
advanced options
[bdr.do_not_replicate](bdr-configuration-variables.md#GUC-BDR-DO-NOT-REPLICATE)
and
[bdr.skip_ddl_replication](bdr-configuration-variables.md#GUC-BDR-SKIP-DDL-REPLICATION)
may be required depending on the nature of the conflict. However,
careless use of these options can make things much worse and it isn\'t
possible to give general instructions for resolving all possible kinds
of conflict.



  ------------------------------------------- ------------------------------------- -------------------------------------------------
  [Prev](conflicts-how.md)     [Home](README.md)     [Next](conflicts-avoidance.md)  
  How conflicts happen                         [Up](conflicts.md)                   Avoiding or tolerating conflicts
  ------------------------------------------- ------------------------------------- -------------------------------------------------
