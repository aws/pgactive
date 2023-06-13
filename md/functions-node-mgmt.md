::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  --------------------------------------------------- ------------------------------------- ----------------------- ------------------------------------------------------------------------------------
  [Prev](functions.md "Functions"){accesskey="P"}   [Up](functions.md){accesskey="U"}    Chapter 12. Functions    [Next](functions-replication-sets.md "Replication Set functions"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [12.1. Node management functions]{#FUNCTIONS-NODE-MGMT} {#node-management-functions .SECT1}

[BDR]{.PRODUCTNAME} uses functions to manage the addition and removal of
nodes and related replication control functions. See [Node
management](node-management.md) for more on how to manage
[BDR]{.PRODUCTNAME}.

The following functions exist to manage nodes:

::: TABLE
[]{#AEN2819}

**Table 12-1. Node management functions**

Function
:::
:::

Return Type

Description

[]{#FUNCTION-BDR-GROUP-CREATE}

`bdr.bdr_group_create(`{.FUNCTION}*`local_node_name`{.REPLACEABLE}*`, `{.FUNCTION}*`node_external_dsn`{.REPLACEABLE}*`, `{.FUNCTION}*`node_local_dsn DEFAULT NULL`{.REPLACEABLE}*`, `{.FUNCTION}*`apply_delay integer DEFAULT NULL`{.REPLACEABLE}*`, `{.FUNCTION}*`replication_sets text[] DEFAULT ARRAY['default']`{.REPLACEABLE}*`)`{.FUNCTION}

void

Create the first node in a future cluster of bdr nodes. May be run on an
empty database or one with existing data. An existing database may be a
previously standalone normal PostgreSQL databaseor an ex-BDR database
cleaned with
[bdr.remove_bdr_from_local_node](functions-node-mgmt.md#FUNCTION-BDR-REMOVE-BDR-FROM-LOCAL-NODE).
The \"dsn\" (data source name) parameters are [libpq connection
strings](https://www.postgresql.org/docs/9.4/static/libpq-connect.html#LIBPQ-CONNSTRING).
*`node_external_dsn`{.REPLACEABLE}* is an arbitrary node name, which
must be unique across the BDR group. *`node_external_dsn`{.REPLACEABLE}*
must be a connection string other nodes can use to connect to this node.
It must embed any required passwords unless passwordless authentication
is required or a suitable `.pgpass`{.FILENAME} file is created in the
postgres home directory. If specified, *`node_local_dsn`{.REPLACEABLE}*
should be a local loopback or unix socket connection string that the
node can use to connect to its self; this is only used during initial
setup to make the database restore faster. *`apply_delay`{.REPLACEABLE}*
allows you to slow down transaction apply, and is mainly for debugging.
*`replication_sets`{.REPLACEABLE}* is the collection of replication sets
this node should receive. See [Joining a
node](node-management-joining.md) for details on node joining and
creation, and [Replication Sets](replication-sets.md) for more on how
replication sets work.

[]{#FUNCTION-BDR-GROUP-JOIN}

`bdr.bdr_group_join(`{.FUNCTION}*`local_node_name`{.REPLACEABLE}*`, `{.FUNCTION}*`node_external_dsn`{.REPLACEABLE}*`, `{.FUNCTION}*`join_using_dsn`{.REPLACEABLE}*`, `{.FUNCTION}*`node_local_dsn DEFAULT NULL`{.REPLACEABLE}*`, `{.FUNCTION}*`apply_delay integer DEFAULT NULL`{.REPLACEABLE}*`, `{.FUNCTION}*`replication_sets text[] DEFAULT ARRAY['default']`{.REPLACEABLE}*`)`{.FUNCTION}

void

Join this database to a cluster of existing bdr nodes. This will
initiate connections to and from all nother nodes. The function returns
immediately, without waiting for the join process to complete, and only
starts work when the calling transaction commits.
[bdr.bdr_node_join_wait_for_ready](functions-node-mgmt.md#FUNCTION-BDR-NODE-JOIN-WAIT-FOR-READY)
may be used to wait until join completes. If there are problems with the
join, check the PostgreSQL logs on both systems for more information.
The parameters are the same as `bdr.bdr_group_create()`{.FUNCTION}
except for the additional required parameter
*`join_using_dsn`{.REPLACEABLE}*. This must be the libpq connection
string of the node to initialize from, i.e. the other node\'s
*`node_external_dsn`{.REPLACEABLE}*. Any node may be chosen as the join
target, but if possible a node with a fast and reliable network link to
the new node should be preferred. Note that
`bdr.bdr_group_join()`{.FUNCTION} can [*not*]{.emphasis} \"re-join\" a
node you removed with `bdr.bdr_part_by_node_names()`{.FUNCTION}. See
[Joining a node](node-management-joining.md) for details on node
joining and creation, and [Replication Sets](replication-sets.md) for
more on how replication sets work.

[]{#FUNCTION-BDR-PART-BY-NODE-NAMES}

`bdr.bdr_part_by_node_names(`{.FUNCTION}*`p_nodes text[]`{.REPLACEABLE}*`)`{.FUNCTION}

void

Removes all the nodes - identified by the node names in the array. All
the remaining nodes in the cluster have to be reachable for this to
succeed. This function must be run on a node that is not being removed.
There is no way to re-join a node once removed; a new node must be
created and joined to replace the parted one if required.

[]{#FUNCTION-BDR-REMOVE-BDR-FROM-LOCAL-NODE}

`bdr.remove_bdr_from_local_node(`{.FUNCTION}*`force boolean`{.REPLACEABLE}*`, `{.FUNCTION}*`convert_global_sequences boolean`{.REPLACEABLE}*`)`{.FUNCTION}

void

Remove BDR slots, replication identifiers, security labels including
replication sets, etc from a BDR-enabled database, so the BDR extension
can be dropped and the database used for normal PostgreSQL. Will refuse
to run on a node that hasn\'t already been parted from the cluster
unless `force`{.LITERAL} is true. Global sequences are converted into
local sequences unless `convert_global_sequences`{.LITERAL} is false.
See [Turning a BDR node back into a normal
database](node-management-disabling.md) for details, including
important caveats with conversion of sequences.

[]{#FUNCTION-BDR-NODE-JOIN-WAIT-FOR-READY}

`bdr.bdr_node_join_wait_for_ready()`{.FUNCTION}

void

Wait till all in-progress node joins have completed.

[]{#FUNCTION-BDR-IS-ACTIVE-IN-DB}

`bdr.bdr_is_active_in_db()`{.FUNCTION}

boolean

Report whether the current database has BDR active. Will be true if BDR
is configured, whether or not there are active connections or any peer
nodes added yet. Also true on a parted node until/unless
[bdr.remove_bdr_from_local_node](functions-node-mgmt.md#FUNCTION-BDR-REMOVE-BDR-FROM-LOCAL-NODE)
is called.

`bdr.bdr_apply_pause()`{.FUNCTION}

void

Temporarily stop applying changes from remote nodes to the local node,
until resume is requested with `bdr.bdr_apply_resume()`{.FUNCTION}.
Connections to the remote node(s) are retained but no data is read from
them. The effects of pausing apply are not persistent, so replay will
resume if PostgreSQL is restarted or the postmaster does crash recovery
after a backend crash. Terminating individual backends using
`pg_terminate_backend`{.FUNCTION} will not cause replay to resume; nor
will reloading the postmaster without a full restart. There is no option
to pause replay from only one peer node.

`bdr.bdr_apply_resume()`{.FUNCTION}

void

Resume replaying changes from peer nodes after replay has been paused by
`bdr.bdr_apply_pause()`{.FUNCTION}.

[]{#FUNCTION-BDR-APPLY-IS-PAUSED}

`bdr.bdr_apply_is_paused()`{.FUNCTION}

boolean

Report whether replay is paused (e.g. with
`bdr.bdr_apply_pause()`{.FUNCTION}). A false return does not mean replay
is actually progressing, only that it\'s not intentionally paused.

`bdr.bdr_node_set_read_only(`{.LITERAL}*`node_name`{.REPLACEABLE}*` `{.FUNCTION}`text`{.LITERAL}`, `{.FUNCTION}*`read_only`{.REPLACEABLE}*` `{.FUNCTION}`boolean`{.LITERAL}`)`{.FUNCTION}

void

Turn read-only mode for a node on or off. A read-only node rejects all
direct local writes to replicateable tables, only allowing writes via
replication from other nodes. Read-only mode may be set or unset from
any node. If not set directly on the local node it takes effect as soon
as the peer node replicates the transaction that made it read-only from
the peer that asked it to become read-only. Writes to
`UNLOGGED`{.LITERAL} and `TEMPORARY`{.LITERAL} tables are still
permitted on read-only nodes, it\'s only tables that can be replicated
(whether or not they are actually in a replication set) that have writes
restricted. Note that read-only mode is persistent across restarts.
`bdr.bdr_get_local_node_name()`{.LITERAL} can be used to supply the node
name of the local node.
[bdr.permit_unsafe_ddl_commands](bdr-configuration-variables.md#GUC-BDR-PERMIT-UNSAFE-DDL-COMMANDS)
can override read-only mode on a per-session basis.

[]{#FUNCTION-BDR-REPLICATE-DDL-COMMAND}

`bdr.bdr_replicate_ddl_command(`{.FUNCTION}*`cmd text`{.REPLACEABLE}*`)`{.FUNCTION}

void

Execute the SQL (usually DDL) *`cmd`{.REPLACEABLE}* on the local node
and queue it for extension on all peer nodes. The same limitations apply
to this function as to DDL run directly by the user, except that DDL not
normally replicated by [BDR]{.PRODUCTNAME} will be replicated if run
with this function; see [DDL replication](ddl-replication.md).

References to objects in DDL must be fully schema-qualified (e.g.
`public.mytable`{.LITERAL} not just `mytable`{.LITERAL}), otherwise the
error `no schema has been selected to create in`{.LITERAL} will be
emitted. Alternately, it is safe to prefix the DDL command block with
`SET LOCAL search_path = 'public';`{.LITERAL} or similar, then use
unqualified names.

BDR disallows DML (`INSERT`{.LITERAL}, `UPDATE`{.LITERAL} and
`DELETE`{.LITERAL}) within `bdr.bdr_replicate_ddl_command`{.FUNCTION}.
That\'s because their effects would get replicated twice - once as a
statement, and once as rows. Possibly with different or conflicting
results. If intermixed with DDL you can also break replication
completely.

Wrap individual DDL commands in
`bdr.bdr_replicate_ddl_command`{.FUNCTION}, rather than entire scripts.

[]{#FUNCTION-BDR-ACQUIRE-GLOBAL-LOCK}

`bdr.acquire_global_lock(`{.FUNCTION}*`mode text`{.REPLACEABLE}*`)`{.FUNCTION}

void

Acquire the [global DDL
lock](ddl-replication-advice.md#DDL-REPLICATION-LOCKING) in
*`mode`{.REPLACEABLE}* and block until the lock is fully acquired.
Raises an `ERROR`{.LITERAL} if lock acqusition cannot succeed at this
time. May block indefinitely if a node is down/unreachable or extremely
lagged, so consider setting a `statement_timeout`{.LITERAL}. The lock is
released when the acquiring transaction commits or rolls back (and not
before). BDR automatically acquires this lock when required, so this
function is mostly useful for test and diagnostic purposes. Possible
lock modes are `ddl_lock`{.LITERAL} and `write_lock`{.LITERAL}. See also
[bdr.bdr_locks](catalog-bdr-locks.md).

[]{#FUNCTION-BDR-WAIT-SLOT-CONFIRM-LSN}

`bdr.wait_slot_confirm_lsn(`{.FUNCTION}*`slotname name`{.REPLACEABLE}*`, `{.FUNCTION}*`upto pg_lsn`{.REPLACEABLE}*`)`{.FUNCTION}

void

Wait until *`slotname`{.REPLACEABLE}* (or all slots, if
`NULL`{.LITERAL}) has passed specified *`upto`{.REPLACEABLE}* lsn (or
the local server\'s current xlog insert lsn, if `NULL`{.LITERAL}).

This function is mosty typically used as
`SELECT bdr.wait_slot_confirm_lsn(NULL, NULL)`{.LITERAL} to wait for all
peers to catch up to the last committed state of the local node.

`pg_xlog_wait_remote_apply(`{.FUNCTION}*`lsn pg_lsn`{.REPLACEABLE}*`, `{.FUNCTION}*`pid integer`{.REPLACEABLE}*`)`{.FUNCTION}

void

Present in Postgres-BDR 9.4 only. Deprecated. Use
[`bdr.wait_slot_confirm_lsn`{.FUNCTION}](functions-node-mgmt.md#FUNCTION-BDR-WAIT-SLOT-CONFIRM-LSN)
instead.

`pg_xlog_wait_remote_receive(`{.FUNCTION}*`lsn pg_lsn`{.REPLACEABLE}*`, `{.FUNCTION}*`pid integer`{.REPLACEABLE}*`)`{.FUNCTION}

void

Same as `pg_xlog_wait_remote_apply(...)`{.FUNCTION}, but returns as soon
as the remote confirms it has received the changes, not necessarily
applied them.

`bdr.bdr_get_workers_info(OUT sysid text, OUT timeline oid, OUT dboid oid, OUT worker_type text, OUT pid int4)`{.FUNCTION}

boolean

Get information about BDR workers that are present on the node.

`bdr.bdr_terminate_workers(sysid text, timeline oid,oid,text)`{.FUNCTION}

boolean

Terminate BDR worker(s) of a node identified by
(`sysid`{.LITERAL},`timeline`{.LITERAL},`dboid`{.LITERAL}) and type
`worker_type`{.LITERAL} (apply/per-db/walsender).

`bdr.skip_changes_upto(`{.FUNCTION}*`sysid text`{.REPLACEABLE}*`, `{.FUNCTION}*`timeline oid`{.REPLACEABLE}*`, `{.FUNCTION}*`dboid oid`{.REPLACEABLE}*`, `{.FUNCTION}*`skip_to_lsn pg_lsn`{.REPLACEABLE}*`)`{.FUNCTION}

void

Discard (skip over) changes in the replication stream. Used for
recovering from replication failures. See [details
below](functions-node-mgmt.md#FUNCTION-BDR-SKIP-CHANGES-UPTO).

::: SECT2
## [12.1.1. `bdr.skip_changes_upto`{.LITERAL}]{#FUNCTION-BDR-SKIP-CHANGES-UPTO} {#bdr.skip_changes_upto .SECT2}

Discard (skip over) changes not yet replayed from the peer with identity
(*`sysid`{.REPLACEABLE}*,*`timeline`{.REPLACEABLE}*,*`dboid`{.REPLACEABLE}*),
resuming replay at the first commit that begins after
*`skip_to_lsn`{.REPLACEABLE}*. A commit that begins exactly at the
specified LSN is skipped, not replayed.

::: WARNING
  ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  **Warning**
  This function is [*very dangerous*]{.emphasis}. Improper use can completely break your replication setup, and almost any use will cause your cluster nodes to become inconsistent with each other. It is intended only for recovery from situations where replication is broken by un-replayable DDL or similar.
  ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
:::

Use the
[`bdr.trace_replay`{.LITERAL}](bdr-configuration-variables.md#GUC-BDR-TRACE-REPLAY)
setting to enable replay tracing and find the problem change to skip, or
look at the error context emitted in log messages if there\'s an error.
You may need to set `log_error_verbosity = verbose`{.LITERAL} in
`postgresql.conf`{.FILENAME} to see the full error context. Skip to the
commit LSN reported in the error, like
`"in commit 0123/ABCD0123"`{.LITERAL}.

Because the change is still committed on the node it originated from and
possibly on other nodes, to restore consistency you\'ll have to perform
some transactions manually with
[`bdr.do_not_replicate`{.LITERAL}](bdr-configuration-variables.md#GUC-BDR-DO-NOT-REPLICATE),
[`bdr.permit_unsafe_ddl_commands`{.LITERAL}](bdr-configuration-variables.md#GUC-BDR-PERMIT-UNSAFE-DDL-COMMANDS)
and/or
[`bdr.skip_ddl_replication`{.LITERAL}](bdr-configuration-variables.md#GUC-BDR-SKIP-DDL-REPLICATION)
options set to restore consistency by manually undoing the operations on
some nodes or manually applying them on the other nodes.

::: NOTE
> **Note:** BDR still cares about dropped columns in tables, so if you
> dropped a column in one node then skipped the drop in another, you
> [*must*]{.emphasis} manually drop the column in the one you skipped it
> in; adding the column back on the other side does [*not*]{.emphasis}
> have the same effect because BDR uses the underlying column attribute
> numbers from `pg_catalog.pg_attribute`{.LITERAL}, not column names, to
> replicate\... and those numbers change when you drop and re-create a
> column.
:::
:::

::: SECT2
## [12.1.2. `bdr.bdr_subscribe`{.FUNCTION}]{#FUNCTIONS-NODE-MGMT-SUBSCRIBE} {#bdr.bdr_subscribe .SECT2}

The function `bdr.bdr_subscribe`{.FUNCTION} has been removed from BDR.
For uni-directional replication, look at the [pglogical
project](http://2ndquadrant.com/pglogical) project or
tools like Londiste.
:::

::: SECT2
## [12.1.3. Node management function examples]{#FUNCTIONS-NODE-MGMT-EXAMPLES} {#node-management-function-examples .SECT2}

These examples show libpq connection strings without a host or hostadd.

To create a [BDR]{.PRODUCTNAME} group on \'node1\':

``` PROGRAMLISTING
    SELECT bdr.bdr_group_create(
       local_node_name := 'node1',
       node_external_dsn := 'port=5598 dbname=bdrdemo');

```

To join \'node2\' to [BDR]{.PRODUCTNAME} group created above:

``` PROGRAMLISTING
    SELECT bdr.bdr_group_join(
       local_node_name := 'node2',
       node_external_dsn := 'port=5559 dbname=bdrdemo',
       join_using_dsn := 'port=5558 dbname=bdrdemo');

```

To remove \'node2\' from the [BDR]{.PRODUCTNAME} group created above:

``` PROGRAMLISTING
   SELECT bdr.bdr_part_by_node_names('{node2}');

```

To see if your node is ready for replication (if you see a NULL result
set, your node is ready):

``` PROGRAMLISTING
   SELECT bdr.bdr_node_join_wait_for_ready();

```
:::

::: NAVFOOTER

------------------------------------------------------------------------

  --------------------------------------- ------------------------------------- --------------------------------------------------------
  [Prev](functions.md){accesskey="P"}     [Home](index.md){accesskey="H"}     [Next](functions-replication-sets.md){accesskey="N"}
  Functions                                [Up](functions.md){accesskey="U"}                                 Replication Set functions
  --------------------------------------- ------------------------------------- --------------------------------------------------------
:::
