  [BDR 2.0.7 Documentation](README.md)                                                                                                    
  [Prev](ddl-replication.md "DDL Replication")   [Up](ddl-replication.md)    Chapter 8. DDL Replication    [Next](ddl-replication-statements.md "Statement specific DDL replication concerns")  


# 8.1. Executing DDL on BDR systems

A BDR group is not the same as a standalone PostgreSQL server. It is
based on asynchronous Active-Active replication without a central
locking and transaction co-ordinator. This has important implications
when executing DDL.

BDR has to temporarily transform into a synchronous cluster to run DDL,
so it has to ensure all nodes are up and fully in sync.

To safely manipulate the database schema in an asynchronous
Active-Active setup, all pending changes have to be replicated first.
Otherwise it is possible that a row being replicated contains data for a
row that has been dropped, or has no data for a row that is marked
`NOT NULL`. More complex cases also exist. To handle this
problem, [BDR] acquires a so-called [DDL
lock](ddl-replication-advice.md#DDL-REPLICATION-LOCKING) the first
time in a transaction where schema changes are made.

Acquiring the global DDL lock requires contacting all nodes in a BDR
group, asking them to replicate all pending changes, and prevent further
changes from being made. Once all nodes are fully caught up, the
originator of the DDL lock is free to perform schema changes and
replicate them to the other nodes. [*While the global DDL lock is held
by a node, no nodes may perform any DDL or make any changes to
rows*].

This means that schema changes, unlike with data changes, can only be
performed while all configured nodes are reachable and keeping up
reasonably well with the current write rate. If DDL absolutely must be
performed while a node is down, it has to be removed from the
configuration (using
[bdr.bdr_part_by_node_names](functions-node-mgmt.md#FUNCTION-BDR-PART-BY-NODE-NAMES))
first. (Once removed, a node cannot be added back; it must be
decomissioned and a new node joined in its place.)

## 8.1.1. The DDL lock

DDL is a heavier weight operation than on standalone PostgreSQL.
Performing DDL on any node will acquire a \"global DDL lock\". The
global DDL lock may also be acquired manually with
[bdr.acquire_global_lock](functions-node-mgmt.md#FUNCTION-BDR-ACQUIRE-GLOBAL-LOCK).

This causes new transactions that attempt write operations [*on any node
except the node that acquired the lock*] to pause (block)
until the DDL lock is released or canceled. Existing write transactions
will be given a grace period (controlled by
[bdr.max_ddl_lock_delay](bdr-configuration-variables.md#GUC-BDR-MAX-DDL-LOCK-DELAY))
to complete and aborted (canceled) if they don\'t complete within the
grace period, with the error:

``` PROGRAMLISTING
FATAL:  terminating connection due to conflict with recovery
DETAIL:  User was holding a relation lock for too long.
     
```

BDR\'s DDL write lock does not affect writes on the node that acquired
it, only the other nodes in the BDR group. The node running the DDL can
continue to execute DML normally so long as the regular PostgreSQL locks
taken by its DDL operations permit. However, concurrent
[*DDL*] in another transaction on the same node is still not
permitted and will fail immediately with a DDL lock error.

Newly starting write operations on other nodes continue to be blocked
until the DDL operation has replicated to all nodes, been applied, and
all nodes have confirmed to the DDL originator that the changes have
been applied. Or until the transaction performing the DDL is canceled
(aborted) by the user or administrator. [*All writes will be blocked,
even if it does not affect the objects the currently in-progress DDL is
modifying.*]

> **Note:** See [DDL locking details](technotes-ddl-locking.md) for
> additional details on why DDL locking is required and how it\'s done.

While [*any*] transaction on any node holds the DDL lock, all
DDL from any other transaction on any node will immediately
`ERROR` with an error like:

``` PROGRAMLISTING
ERROR:  database is locked against ddl by another node
HINT:  Node (6313760193895071967,1,16385) in the cluster is already performing DDL
     
```

There is no grace period for conflicting DDL (schema changes), only DML
(row writes).

If the transaction holding the DDL lock is taking too long to complete,
or the DDL lock acquisition is getting stuck because of replication
delays or down nodes, you can cancel the transaction that\'s acquiring
the lock. Just `pg_terminate_backend()` the backend that\'s
taking/holding the DDL lock. It\'s all crash-safe.

If the node that holds the DDL lock goes down permanently while holding
the DDL lock, parting the node with
[`bdr.bdr_part_by_node_names()`](functions-node-mgmt.md#FUNCTION-BDR-PART-BY-NODE-NAMES)
will release the lock on other nodes.

You cannot see the global DDL lock in the `pg_locks` view, as
it is not implemented using a heavyweight lock. See
[Monitoring](monitoring.md) for guidance on monitoring BDR, including
DDL locking. The [bdr.bdr_locks](catalog-bdr-locks.md) view provides
diagnostic information on lock state.

BDR 2.0 allows some DDL that doesn\'t affect table structure to proceed
without blocking concurrent writes, only other DDL. See [Statement
specific DDL replication concerns](ddl-replication-statements.md) for
details. Most DDL still requires a full write lock.

## 8.1.2. Minimising the impact of DDL

To minimise the impact of DDL, transactions performing DDL should be
short, should not be combined with lots of row changes, and should avoid
long running foreign key or other constraint re-checks.

Multiple DDL statements should generally be bunched into a transaction
rather than fired as individual statements, so the DDL lock only has to
be taken once.

If DDL is holding the system up for too long, it is possible and safe to
cancel the DDL on the originating node like you would cancel any other
statement, e.g. with `Control-C` in [psql] or
with `pg_cancel_backend`.

Once the DDL operation has committed on the originating node, you cannot
cancel or abort it. You must wait for it to apply successfully on all
other nodes and for them to replay confirmation. This is why it is
important to keep DDL transactions short and fast.

Because DDL is disruptive in [BDR], it\'s possible to
configure the system so that transactions can\'t do DDL that requires a
heavy global lock by default. This is controlled by the
[bdr.permit_ddl_locking](bdr-configuration-variables.md#GUC-BDR-PERMIT-DDL-LOCKING)
setting. If set to `false`, any command that would acquire the
global DDL lock is rejected with an `ERROR` instead. This
helps prevent unintended global DDL lock acquisitions. You can make this
the default for a database, user or group with

``` PROGRAMLISTING
 ALTER ROLE username SET bdr.permit_ddl_locking = false;
     
```

or

``` PROGRAMLISTING
 ALTER DATABASE dbname SET bdr.permit_ddl_locking = false;
     
```

or set it globally in `postgresql.conf`.




  --------------------------------------------- ------------------------------------------- --------------------------------------------------------
  [Prev](ddl-replication.md)        [Home](README.md)        [Next](ddl-replication-statements.md)  
  DDL Replication                                [Up](ddl-replication.md)               Statement specific DDL replication concerns
  --------------------------------------------- ------------------------------------------- --------------------------------------------------------
