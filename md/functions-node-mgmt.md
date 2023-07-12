  [BDR 2.0.7 Documentation](README.md)                                                                             
  [Prev](functions.md "Functions")   [Up](functions.md)    Chapter 12. Functions    [Next](functions-replication-sets.md "Replication Set functions")  


# 12.1. Node management functions

[BDR] uses functions to manage the addition and removal of
nodes and related replication control functions. See [Node
management](node-management.md) for more on how to manage
[BDR].

The following functions exist to manage nodes:


**Table 12-1. Node management functions**

Function

Return Type

Description


`bdr.bdr_create_group(`*`local_node_name`*`, `*`node_external_dsn`*`, `*`node_local_dsn DEFAULT NULL`*`, `*`apply_delay integer DEFAULT NULL`*`, `*`replication_sets text[] DEFAULT ARRAY['default']`*`)`

void

Create the first node in a future cluster of bdr nodes. May be run on an
empty database or one with existing data. An existing database may be a
previously standalone normal PostgreSQL databaseor an ex-BDR database
cleaned with
[bdr.bdr_remove](functions-node-mgmt.md#FUNCTION-BDR-REMOVE).
The \"dsn\" (data source name) parameters are [libpq connection
strings](https://www.postgresql.org/docs/9.4/static/libpq-connect.html#LIBPQ-CONNSTRING).
*`node_external_dsn`* is an arbitrary node name, which
must be unique across the BDR group. *`node_external_dsn`*
must be a connection string other nodes can use to connect to this node.
It must embed any required passwords unless passwordless authentication
is required or a suitable `.pgpass` file is created in the
postgres home directory. If specified, *`node_local_dsn`*
should be a local loopback or unix socket connection string that the
node can use to connect to its self; this is only used during initial
setup to make the database restore faster. *`apply_delay`*
allows you to slow down transaction apply, and is mainly for debugging.
*`replication_sets`* is the collection of replication sets
this node should receive. See [Joining a
node](node-management-joining.md) for details on node joining and
creation, and [Replication Sets](replication-sets.md) for more on how
replication sets work.


`bdr.bdr_join_group(`*`local_node_name`*`, `*`node_external_dsn`*`, `*`join_using_dsn`*`, `*`node_local_dsn DEFAULT NULL`*`, `*`apply_delay integer DEFAULT NULL`*`, `*`replication_sets text[] DEFAULT ARRAY['default']`*`)`

void

Join this database to a cluster of existing bdr nodes. This will
initiate connections to and from all nother nodes. The function returns
immediately, without waiting for the join process to complete, and only
starts work when the calling transaction commits.
[bdr.bdr_wait_for_node_ready](functions-node-mgmt.md#FUNCTION-BDR-WAIT-FOR-NODE-READY)
may be used to wait until join completes. If there are problems with the
join, check the PostgreSQL logs on both systems for more information.
The parameters are the same as `bdr.bdr_create_group()`
except for the additional required parameter
*`join_using_dsn`*. This must be the libpq connection
string of the node to initialize from, i.e. the other node\'s
*`node_external_dsn`*. Any node may be chosen as the join
target, but if possible a node with a fast and reliable network link to
the new node should be preferred. Note that
`bdr.bdr_join_group()` can [*not*] \"re-join\"
physically a node you removed with
`bdr.bdr_detach_nodes()`. See [Joining a
node](node-management-joining.md) for details on node joining and
creation, and [Replication Sets](replication-sets.md) for more on how
replication sets work.


`bdr.bdr_detach_nodes(`*`p_nodes text[]`*`)`

void

Removes all the nodes - identified by the node names in the array. All
the remaining nodes in the cluster have to be reachable for this to
succeed. This function must be run on a node that is not being removed.
There is no way to re-join a node once removed; a new node must be
created and joined to replace the parted one if required.


`bdr.bdr_remove(`*`force boolean`*`, `*`convert_global_sequences boolean`*`)`

void

Remove BDR slots, replication identifiers, security labels including
replication sets, etc from a BDR-enabled database, so the BDR extension
can be dropped and the database used for normal PostgreSQL. Will refuse
to run on a node that hasn\'t already been parted from the cluster
unless `force` is true. Global sequences are converted into
local sequences unless `convert_global_sequences` is false.
See [Turning a BDR node back into a normal
database](node-management-disabling.md) for details, including
important caveats with conversion of sequences.


`bdr.bdr_wait_for_node_ready()`

void

Wait till all in-progress node joins have completed.


`bdr.bdr_is_active_in_db()`

boolean

Report whether the current database has BDR active. Will be true if BDR
is configured, whether or not there are active connections or any peer
nodes added yet. Also true on a parted node until/unless
[bdr.bdr_remove](functions-node-mgmt.md#FUNCTION-BDR-REMOVE)
is called.

`bdr.bdr_generate_node_identifier()`

numeric

Generate a BDR node identifier, write it to BDR control file, and return
the generated id. This node identifier is used by BDR to uniquely
identify and track BDR-enabled databases on this node via
`bdr.bdr_nodes` table. Typically, this is not supposed to be
used by users direclty, BDR uses it internally while creating a new BDR
group or joining a node to existing BDR group. Use of this function is
restricted to superusers by default, but access may be granted to others
using `GRANT`.

`bdr.bdr_get_node_identifier()`

numeric

Get BDR node identifier from BDR control file. Use of this function is
restricted to superusers by default, but access may be granted to others
using `GRANT`.

`bdr.bdr_remove_node_identifier()`

boolean

Remove BDR node identifier from BDR control file. Actually, it removes
BDR control file itself, because the BDR control file currently holds
only BDR node identifier. It doesn\'t remove the BDR control file if BDR
is active on any of the database on this node. Return true if
successfully removed, otherwise false. Typically, this is not supposed
to be used by users direclty, BDR uses it internally while removing BDR
from local node. Use of this function is restricted to superusers by
default, but access may be granted to others using `GRANT`.

`bdr.bdr_apply_pause()`

void

Temporarily stop applying changes from remote nodes to the local node,
until resume is requested with `bdr.bdr_apply_resume()`.
Connections to the remote node(s) are retained but no data is read from
them. The effects of pausing apply are not persistent, so replay will
resume if PostgreSQL is restarted or the postmaster does crash recovery
after a backend crash. Terminating individual backends using
`pg_terminate_backend` will not cause replay to resume; nor
will reloading the postmaster without a full restart. There is no option
to pause replay from only one peer node.

`bdr.bdr_apply_resume()`

void

Resume replaying changes from peer nodes after replay has been paused by
`bdr.bdr_apply_pause()`.


`bdr.bdr_is_apply_paused()`

boolean

Report whether replay is paused (e.g. with
`bdr.bdr_apply_pause()`). A false return does not mean replay
is actually progressing, only that it\'s not intentionally paused.

`bdr.bdr_set_node_read_only(`*`node_name`*` ``text``, `*`read_only`*` ``boolean``)`

void

Turn read-only mode for a node on or off. A read-only node rejects all
direct local writes to replicateable tables, only allowing writes via
replication from other nodes. Read-only mode may be set or unset from
any node. If not set directly on the local node it takes effect as soon
as the peer node replicates the transaction that made it read-only from
the peer that asked it to become read-only. Writes to
`UNLOGGED` and `TEMPORARY` tables are still
permitted on read-only nodes, it\'s only tables that can be replicated
(whether or not they are actually in a replication set) that have writes
restricted. Note that read-only mode is persistent across restarts.
`bdr.bdr_get_local_node_name()` can be used to supply the node
name of the local node.

`bdr.bdr_replicate_ddl_command(`*`cmd text`*`)`

void

Execute the SQL (usually DDL) *`cmd`* on the local node
and queue it for extension on all peer nodes. The same limitations apply
to this function as to DDL run directly by the user, except that DDL not
normally replicated by [BDR] will be replicated if run
with this function; see [DDL replication](ddl-replication.md).

References to objects in DDL must be fully schema-qualified (e.g.
`public.mytable` not just `mytable`), otherwise the
error `no schema has been selected to create in` will be
emitted. Alternately, it is safe to prefix the DDL command block with
`SET LOCAL search_path = 'public';` or similar, then use
unqualified names.

BDR disallows DML (`INSERT`, `UPDATE` and
`DELETE`) within `bdr.bdr_replicate_ddl_command`.
That\'s because their effects would get replicated twice - once as a
statement, and once as rows. Possibly with different or conflicting
results. If intermixed with DDL you can also break replication
completely.

Wrap individual DDL commands in
`bdr.bdr_replicate_ddl_command`, rather than entire scripts.

`bdr.bdr_replicate_ddl_command` errors out if executed while
`bdr.skip_ddl_replication` is set to true.

`bdr.bdr_acquire_global_lock(`*`mode text`*`)`

void

Acquire the [global DDL
lock](ddl-replication-advice.md#DDL-REPLICATION-LOCKING) in
*`mode`* and block until the lock is fully acquired.
Raises an `ERROR` if lock acqusition cannot succeed at this
time. May block indefinitely if a node is down/unreachable or extremely
lagged, so consider setting a `statement_timeout`. The lock is
released when the acquiring transaction commits or rolls back (and not
before). BDR automatically acquires this lock when required, so this
function is mostly useful for test and diagnostic purposes. Possible
lock modes are `ddl_lock` and `write_lock`. See also
[bdr.bdr_global_locks_info](catalog-bdr-global-locks-info.md).


`bdr.bdr_wait_for_slots_confirmed_flush_lsn(`*`slotname name`*`, `*`upto pg_lsn`*`)`

void

Wait until *`slotname`* (or all slots, if
`NULL`) has passed specified *`upto`* lsn (or
the local server\'s current xlog insert lsn, if `NULL`).

This function is mosty typically used as
`SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL, NULL)` to wait for all
peers to catch up to the last committed state of the local node.

`pg_xlog_wait_remote_apply(`*`lsn pg_lsn`*`, `*`pid integer`*`)`

void

Present in Postgres-BDR 9.4 only. Deprecated. Use
[`bdr.bdr_wait_for_slots_confirmed_flush_lsn`](functions-node-mgmt.md#FUNCTION-BDR-WAIT-FOR-SLOTS-CONFIRMED-FLUSH-LSN)
instead.

`pg_xlog_wait_remote_receive(`*`lsn pg_lsn`*`, `*`pid integer`*`)`

void

Same as `pg_xlog_wait_remote_apply(...)`, but returns as soon
as the remote confirms it has received the changes, not necessarily
applied them.

`bdr.bdr_get_workers_info(OUT sysid text, OUT timeline oid, OUT dboid oid, OUT worker_type text, OUT pid int4)`

boolean

Get information about BDR workers that are present on the node.

`bdr.bdr_terminate_workers(sysid text, timeline oid,oid,text)`

boolean

Terminate BDR worker(s) of a node identified by
(`sysid`,`timeline`,`dboid`) and type
`worker_type` (apply/per-db/walsender).

`bdr.bdr_skip_changes(`*`sysid text`*`, `*`timeline oid`*`, `*`dboid oid`*`, `*`skip_to_lsn pg_lsn`*`)`

void

Discard (skip over) changes in the replication stream. Used for
recovering from replication failures. See [details
below](functions-node-mgmt.md#FUNCTION-BDR-SKIP-CHANGES).

## 12.1.1. `bdr.bdr_skip_changes`

Discard (skip over) changes not yet replayed from the peer with identity
(*`sysid`*,*`timeline`*,*`dboid`*),
resuming replay at the first commit that begins after
*`skip_to_lsn`*. A commit that begins exactly at the
specified LSN is skipped, not replayed.

  **Warning**
  This function is [*very dangerous*]. Improper use can completely break your replication setup, and almost any use will cause your cluster nodes to become inconsistent with each other. It is intended only for recovery from situations where replication is broken by un-replayable DDL or similar.

Use the
[`bdr.trace_replay`](bdr-configuration-variables.md#GUC-BDR-TRACE-REPLAY)
setting to enable replay tracing and find the problem change to skip, or
look at the error context emitted in log messages if there\'s an error.
You may need to set `log_error_verbosity = verbose` in
`postgresql.conf` to see the full error context. Skip to the
commit LSN reported in the error, like
`"in commit 0123/ABCD0123"`.

Because the change is still committed on the node it originated from and
possibly on other nodes, to restore consistency you\'ll have to perform
some transactions manually with
[`bdr.do_not_replicate`](bdr-configuration-variables.md#GUC-BDR-DO-NOT-REPLICATE)
and/or
[`bdr.skip_ddl_replication`](bdr-configuration-variables.md#GUC-BDR-SKIP-DDL-REPLICATION)
options set to restore consistency by manually undoing the operations on
some nodes or manually applying them on the other nodes.

> **Note:** BDR still cares about dropped columns in tables, so if you
> dropped a column in one node then skipped the drop in another, you
> [*must*] manually drop the column in the one you skipped it
> in; adding the column back on the other side does [*not*]
> have the same effect because BDR uses the underlying column attribute
> numbers from `pg_catalog.pg_attribute`, not column names, to
> replicate\... and those numbers change when you drop and re-create a
> column.

## 12.1.2. Node management function examples

These examples show libpq connection strings without a host or hostadd.

To create a [BDR] group on \'node1\':

``` PROGRAMLISTING
    SELECT bdr.bdr_create_group(
       local_node_name := 'node1',
       node_external_dsn := 'port=5598 dbname=bdrdemo');
   
```

To join \'node2\' to [BDR] group created above:

``` PROGRAMLISTING
    SELECT bdr.bdr_join_group(
       local_node_name := 'node2',
       node_external_dsn := 'port=5559 dbname=bdrdemo',
       join_using_dsn := 'port=5558 dbname=bdrdemo');
   
```

To remove \'node2\' from the [BDR] group created above:

``` PROGRAMLISTING
   SELECT bdr.bdr_detach_nodes(ARRAY['node2']);
   
```

To see if your node is ready for replication (if you see a NULL result
set, your node is ready):

``` PROGRAMLISTING
   SELECT bdr.bdr_wait_for_node_ready();
   
```



  --------------------------------------- ------------------------------------- --------------------------------------------------------
  [Prev](functions.md)     [Home](README.md)     [Next](functions-replication-sets.md)  
  Functions                                [Up](functions.md)                                 Replication Set functions
  --------------------------------------- ------------------------------------- --------------------------------------------------------
