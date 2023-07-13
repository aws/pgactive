# BDR Design Document


Bi-Directional Replication (BDR) provides loosely coupled asynchronous primary-primary logical replication between databases with mesh technology. This means that you can write to any of the databases in the BDR group and they will first be committed on the instance they were written to and then sent row by row to all of the other instances in the cluster.
[Image: BDR.drawio.png]

#### Active-Active

A BDR group is a collection of databases, not PostgreSQL instances. The distinction here is that an instance is a collection of 1 or more databases being managed by a PostgreSQL backend. Where as a database resides in an instance.
Each database participating in the BDR group receives all of the updates from all the other members and can be written to directly. It is not necessary to write to all of the instances. All but one can be written to, and the others will receive all of the changes. 

#### Asynchronous

Changes made on one BDR node are not replicated to other nodes before they are committed locally. As a result the data is not exactly the same on all nodes at any given time; some nodes will have data that has not yet arrived on other nodes. When combined with active-active, asynchronous replication is often called an "eventually consistent" architecture, however on any individual node the data is consistent. At any given time the data can look different when viewed from different nodes, but over time the nodes sync with each other. If writes stop then after a while all nodes will be the same. In BDR this means that **foreign key constraints may be temporarily violated** as data replicates from multiple nodes.

#### Loosely Coupled

Nodes in BDR are loosely-coupled because there is not much inter-node co-ordination traffic. There is no global transaction manager, no global lock manager, etc. Locks are node-local and there's no inter-node row or relation locking; the only inter-node locking is done for schema changes (DDL). This means nodes run more independently and are highly tolerant of network interruptions and partitions. But it also means that replication conflicts may occur.


#### Logical

Logical (row-based) replication is replication using individual row values. It contrasts with physical (block-based) replication where changes to data blocks are sent. Logical replication is at a different level - it's a lot like the difference between sending a set of files and sending the hard drive the files are on. Logical replication has both advantages and disadvantages compared to physical replication; see section below for Differences between Logical and Physical replication.


#### Replication

Replication is the process of copying data from one place to another. In BDR refers to the fact that BDR is not a shared-storage architecture; each node has its own copy of the database, including all relevant indexes etc. Nodes can satisfy queries without needing to communicate with other nodes, but must also have enough storage space to hold all the data in the database.

#### Mesh Topology

BDR uses a mesh topology, where every node can communicate directly with every other node. It doesn't support circular replication, forwarding, cascading, etc. Each pair of nodes communicates over a pair of (mostly) uni-directional
channels, one to stream data from node A=\>B and one to stream data from node B=\>A. This means each node must be able to connect directly to each other node. Firewalls, NAT, etc must be configured accordingly.
Every BDR node must have a [replication slot](https://www.postgresql.org/docs/current/logicaldecoding-explanation.html#LOGICALDECODING-REPLICATION-SLOTS) on every other BDR node so it can replay changes from the node, and
every node must have a [replication origin](https://www.postgresql.org/docs/current/replication-origins.html) for each other node so it can keep track of replay progress. If nodes were allowed to join while another was offline or unreachable due to a network partition, it would have no way to replay any changes made on that node and the BDR group would get out of sync. Since BDR does not change forwarding during normal operation, that de-synchronization would not get resolved. The addition of enhanced change forwarding support could allow for cascading nodes isolated from the rest of the mesh, allow new nodes to join and lazily switch over to directly receiving data from a node when it becomes reachable, etc. It's not fundamentally necessary for all nodes to be reachable during node join, it's just a requirement for the current implementation. There's already limited change forwarding support in place and used for initial node clone. DDL locking enhancements would also be required.

#### DDL 

DDL replication is disabled by default. If needed, the configuration parameter bdr.skip_ddl_replication needs to be set to false on both the node and its upstream node(s).

### Security

#### Users

The user specified in the connection DSN must be a superuser otherwise they do not have access to `bdr.bdr_nodes` table and other objects upon whom access to public is revoked.

Changes on the replica are applied by a background worker. There are a number of background workers; supervisor, database worker, and the apply worker. The background workers must run as superuser.

### Node Management

#### Joining a node

When a new BDR node is joined to an existing BDR group, node is subscribed to an upstream peer, the system must copy the existing data from the peer node(s) to the local node before replication can begin. This copy has to be carefully coordinated so that the local and remote data starts out ***identical***, so it's not sufficient to just use pg_dump yourself. The extension provides built-in facilities for making this initial copy.
Every BDR node must be ***online and reachable*** when an attempt to join a new node is made. Otherwise the join will hang indefinitely or fail. BDR is a mesh where every node must be able to communicate with every other node, and while it is tolerant of network partitions and interruptions all nodes need to know about every other node that exist. See [BDR mesh network](https://file+.vscode-resource.vscode-cdn.net/Users/davecra/projects/amazon/abba/abba-pg-bdr/md/technotes-mesh.md).
There are two ways to join a new BDR node: logical or physical copy. After the initial copy is done there is no significant difference between physical or logical initialization of a BDR node, so the choice is down to which setup method will be quickest and easiest for your particular needs.
In a logical copy, a blank database in an existing standalone PostgreSQL instance is enabled for BDR via SQL functions calls. The BDR extension makes a connection to an upstream node designated by the user and takes a schema and data dump of that node. The dump is then applied to the local blank database before replication begins. Only the specified database is copied. With a logical copy you don't have to create new init scripts, run separate instances on separate ports, etc, as everything happens in your existing PostgreSQL instance.
In a physical copy, the [bdr_init_copy](https://file+.vscode-resource.vscode-cdn.net/Users/davecra/projects/amazon/abba/abba-pg-bdr/md/command-bdr-init-copy.md) is used to clone a user-designated upstream node. This clone is then reconfigured and started up as a new node before replication begins. All databases on the remote node are copied, though only the specified database is initially activated for BDR. (Support for multiple database join may be added at a later date). After a physical node join or subscribe the admin will generally need to separately register the new PostgreSQL instance with the operating system to auto-start, as PostgreSQL does not do this automatically. You may also need to select a different PostgreSQL port if there is already a local PostgreSQL instance.
The advantages and disadvantages of each approach roughly mirror those of a logical backup using pg_dump and pg_restore vs a physical copy using pg_basebackup. See the http://www.postgresql.org/docs/current/static/backup.html for more information.
In general it's more convenient to use logical join when you have an existing PostgreSQL instance, a reasonably small database, and other databases you might not also want to copy/replicate. Physical join is more appropriate for big databases that are the only database in a given PostgreSQL install.


#### Removing a node

Because BDR can recover from extended node outages it is necessary to explicitly tell the system if you are removing a node permanently. If you permanently shut down a node and don't tell the other nodes then performance will suffer and eventually the whole system will stop working.

Each node saves up change information using one replication slot for each peer node so it can replay changes to a temporarily unreachable node. If a peer node remains offline indefinitely this accumulating change information will cause the node to run out of storage space for PostgreSQL transaction logs (WAL, in pg_xlog), likely causing the database server to shut down with an error like:

    **PANIC: could not write to file "pg_xlog/xlogtemp.559": No space left on device**

or report other out-of-disk related symptoms.

**NOTE**

Note: Administrators should monitor for node outages (see: Monitoring and make sure nodes have sufficient free disk space. :::

A node is removed with the `bdr.bdr_detach_nodes()` function. You must specify the node name (as passed during node creation) to remove a node. You should call `bdr.bdr_detach_nodes()` from a node that will remain in the BDR group, not the node to be removed. Multiple nodes may be removed at once. No value is returned; the removal status may be seen by checking the `status` field in `bdr.bdr_nodes` for that node.

To remove node1

  `SELECT bdr.bdr_detach_nodes(ARRAY['node-1']);`

or to remove multiple nodes at once:

 `SELECT bdr.bdr_detach_nodes(ARRAY['node-1', 'node-2', 'node-3']);`



#### Removing BDR from a node

To take a BDR node that has already been removed, or one that has been restored from a base backup, and turn it back into a normal PostgreSQL database you may use the `bdr.bdr_remove()` function.

After running `bdr.bdr_remove()` it is safe to `DROP EXTENSION bdr`. At this point all BDR-specific elements will have been removed from the local database and it may be used as a standalone database. Global sequences are converted into local sequences and may be used normally. All BDR triggers, event triggers, security labels, slots, replication identifiers etc are removed from the local node.

Alternately, after `bdr.bdr_remove()`, it is possible to `bdr.bdr_create_group()` a new BDR group with this database as the starting node. The new group will be completely independent from the existing group.

If BDR thinks it's still joined with an existing node group then `bdr.bdr_remove()` will refuse to run as a safety measure to prevent inconsistently removing a running node.

If you are sure the node has really been removed from its group or is a duplicate copy of a node that's still running normally, you may force removal by calling `bdr.bdr_remove(true)`. Do **not** do so unless you're certain the node you're running it on is already isolated from the group - say, if it's been removed while disconnected, or has been restored from a PITR backup or disk snapshot. Otherwise you will leave dangling replication slots etc on the other nodes, causing problems on the remaining nodes. Always `bdr.bdr_detach_nodes()` the node first.

#### N-safe synchronous replication

BDR can be configured to use PostgreSQL's 9.6+'s underlying n-safe synchronous replication support. Each node may have a priority-ordered of other nodes set in [synchronous_standby_names](https://www.postgresql.org/docs/current/static/runtime-config-replication.html#GUC-SYNCHRONOUS-STANDBY-NAMES) along with the minimum number that must confirm replay before the commit is accepted on the upstream. PostgreSQL will delay confirmation of `COMMIT` to the client until the highest-priority currently-connected node on the list has confirmed that the commit has been replayed and locally flushed.


The [application_name](https://www.postgresql.org/docs/current/static/runtime-config-logging.html#GUC-APPLICATION-NAME) of each BDR apply worker's connection to its upstream nodes is `nodename:send`. This is what appears in `pg_stat_activity` for connections from peers and what's used in `synchronous_standby_names`. The node name must be "double quoted" for use in `synchronous_standby_names`

A typical configuration is 4 nodes arranged in two mutually synchronous 1-safe pairs. If the nodes names are A, B, C and D and we want A to be synchronous with B and vice versa, and C to be synchronous with D and vice versa, each node's configuration would be:

```
   # on node A:
   synchronous_standby_names = '1 ("B:send")'
   bdr.synchronous_commit = on
   # on node B:
   synchronous_standby_names = '1 ("A:send")'
   bdr.synchronous_commit = on
   # on node C:
   synchronous_standby_names = '1 ("D:send")'
   bdr.synchronous_commit = on
   # on node D:
   synchronous_standby_names = '1 ("C:send")'
   bdr.synchronous_commit = on
```

With this configuration, commits on A will hang indefinitely if B goes down or vice versa. If this is not desired, each node can use the other nodes as secondary synchronous options (possibly with higher latency over a WAN), e.g.
```
   # on node A, prefer sync rep to B, but if B is down allow COMMIT
   # confirmation if either C or D are reachable and caught up:
   synchronous_standby_names = '1 ("B:send","C:send","D:send")'
```
If confirmation from all three other nodes is required before local commit, use 3-safe:
```
   # Require that B, C and D all confirm commit replay before local commit
   # on A becomes visible.
   synchronous_standby_names = '3 ("B:send","C:send","D:send")'
```
See the PostgreSQL manual on [synchronous replication](https://www.postgresql.org/docs/current/static/warm-standby.html#SYNCHRONOUS-REPLICATION) for a discussion of how synchronous replication works in PostgreSQL. Most of the same principles apply when the other end is a BDR node not a physical standby.

Note: PostgreSQL's synchronous replication commits on the upstream before replicating to the downstream(s), it just hides the commit from other concurrent transactions until the downstreams complete. If the upstream is restarted the hidden commit(s) become visible even if the downstreams have not replied yet, so node restarts effectively momentarily disable synchronous replication.

It's generally a good idea to set `bdr.synchronous_commit = on` on all peers listed in `synchronous_standby_names` if using synchronous replication, since this speeds up acknowledgement of commits by peers and thus helps `COMMIT` return with minimal delay.

To reduce the delay in `COMMIT` acknowledgement and increase throughput, users may wish to run unimportant transactions with

`SET LOCAL synchronous_commit = off;`

This effectively disables synchronous replication for individual transactions.

Unlike PostgreSQL's physical replication, logical decoding (and therefore BDR) cannot begin replicating a transaction to peer nodes until it has committed on the originating node. This means that large transactions can be subject to long delays on `COMMIT` when synchronous replication is in use. Even if large transactions are run with `synchronous_commit = off` they may delay commit confirmation for small synchronous transactions that commit after the big transactions because logical decoding processes transactions in strict commit-order.

Even if synchronous replication is enabled, conflicts are still possible even in a 2-node mutually synchronous configuration since no inter-node locking is performed.


#### Global Sequences

Many applications require unique values be assigned to database entries.Some applications use `UUID/GUIDs` generated by external programs, some use database-supplied values. This is important with optimistic conflict resolution schemes (like that in BDR) because uniqueness violations can result in discarded inserts during conflict resolution. The SQL standard requires `SEQUENCE` objects which generate unique values. These can then be used to supply default values using `DEFAULT nextval('mysequence')`, as with PostgreSQL's `SERIAL` pseudo-type. PostgreSQL doesn't provide any facilities to synchronise or replicate sequences, so they're purely node-local. A typical approach for sharded or multi-node applications is to use split-step or partitioned sequences, where all nodes increment the sequence by the same fixed value and each node has a fixed offset within the sequence. So node 1 generates IDs 1, 101, 201, 301, \...; node 2 generates IDs 2, 102, 202, 302, \...; etc. This is easily done with PostgreSQL's existing sequences, but becomes a major problem if you don't allow enough room for growth - in the above, if you have 101 nodes you're in serious trouble. It's also awkward, requiring node-specific DDL and setup. It also makes replacing failed nodes difficult as each table must be scanned to determine what ID each sequence was up to on the node before failure, or a new (very finite) node ID must be allocated. To help avoid Active-Active conflicts on concurrent inserts BDR provides a global sequence mapping function. This allows a normal sequence to be used in a globally-unique manner by qualifying its result with a unique node ID and timestamp. Specifically we use 40 bits of timestamp, 10 bits of node_id and 14 sequence bits. Using the timestamp provides a rough chronological ordering of inserts across the cluster.  BDR manages the node IDs internally (see the ``bdr.bdr_nodes.node_seq_id`` column). Node IDs for detached nodes are re-used, so node ID exhaustion is not a concern for environments that regularly detach and join nodes, such as for load balancing.


#### Using Global Sequences

To use a global sequence, create a local sequence with `CREATE SEQUENCE ...` like normal. Then instead of using `nextval(seqname)` to get values from it, use `bdr.bdr_snowflake_id_nextval(seqname)`. The destination column
must be `BIGINT` as the result is 64 bits wide.

```
  CREATE TABLE gstest (
    id bigint primary key,
    parrot text
  );

  CREATE SEQUENCE gstest_id_seq OWNED BY gstest.id;

  ALTER TABLE gstest ALTER COLUMN id SET DEFAULT bdr.bdr_snowflake_id_nextval('gstest_id_seq');
```

If you normally create the sequence as a `BIGSERIAL` column you may continue to do so. To enable global sequence use on the column you must `ALTER` the `DEFAULT` expression after table creation. There is currently no facility to do this automatically and transparently so you need to do it in a follow up command like:


`ALTER TABLE my_table ALTER COLUMN my_bigserial SET DEFAULT bdr.bdr_snowflake_id_nextval('my_table_my_bigserial_seq');`


### Conflicts

In multi-master use of BDR writes to the same or related table(s) from multiple different nodes can result in data conflicts. Some clustering systems use distributed lock mechanisms to prevent concurrent access to data. These can perform reasonably when servers are very close but cannot support geographically distributed applications as very low latency is critical for acceptable performance. Distributed locking is essentially a pessimistic approach, whereas BDR advocates an optimistic approach: avoid conflicts where possible but allow some types of conflict to occur and and resolve them when they arise.

#### How Conflicts Happen

Inter-node conflicts arise as a result of sequences of events that could not happen if all the involved transactions happened concurrently on the same node. Because the nodes only exchange changes after transactions commit, each transaction is individually valid on the node it committed on but would not be valid if run on another node that has done other work in the mean time. Since BDR apply essentially replays the transaction on the other nodes, the replay operation can fail if there is a conflict between a transaction being applied and a transaction that was committed on the receiving node.
The reason most conflicts can't happen when all transactions run on a single node is that PostgreSQL has inter-transaction communication mechanisms to prevent it - `UNIQUE` indexes, `SEQUENCE`, row and relation locking, `SERIALIZABLE` dependency tracking, etc. All of these mechanisms are ways to communicate between transactions to prevent undesirable concurrency issues.
BDR does not have a distributed transaction manager or lock manager. That's part of why it performs well with latency and network partitions. As a result, so ***transactions on different nodes execute entirely in isolation from each other***. Despite the usual perception that "more isolation is good" you actually need to reduce isolation to prevent conflicts. :::



#### Avoiding or tolerating conflicts

In most cases appropriate application design can be used to avoid conflicts and/or the application can be made tolerant of conflicts.
Conflicts can only happen if there are things happening at the same time on multiple nodes, so the simplest way to avoid conflicts is to only ever write to one node, or to only ever write to independent subsets of the database on each node. For example, each node might have a separate schema, and while they all exchange data with each other, writes are only ever performed on the node that "owns" a given schema.
For `INSERT` vs `INSERT` conflicts, use of [Global sequences](https://file+.vscode-resource.vscode-cdn.net/Users/davecra/projects/amazon/abba/abba-pg-bdr/md/global-sequences.md) can completely prevent conflicts.
BDR users may sometimes find it useful to perform distributed locking at the application level in cases where conflicts are not acceptable.
The best course of action is frequently to allow conflicts to occur and design the application to work with BDR's conflict resolution mechanisms to cope with the conflict.



#### Conflict logging

To make diagnosis and handling of multi-master conflicts easier, BDR supports logging of each conflict incident in a [`bdr.bdr_conflict_history`](https://file+.vscode-resource.vscode-cdn.net/Users/davecra/projects/amazon/abba/abba-pg-bdr/md/catalog-bdr-conflict-history.md) table.
Conflict logging to this table is only enabled when [bdr.log_conflicts_to_table](https://file+.vscode-resource.vscode-cdn.net/Users/davecra/projects/amazon/abba/abba-pg-bdr/md/bdr-configuration-variables.md#GUC-BDR-LOG-CONFLICTS-TO-TABLE) is `true` BDR also logs conflicts to the PostgreSQL log file if `log_min_messages` is `LOG `or lower, irrespective of the value of `bdr.log_conflicts_to_table`
You can use the conflict history table to determine how rapidly your application creates conflicts and where those conflicts occur, allowing you to improve the application to reduce conflict rates. It also helps detect cases where conflict resolutions may not have produced the desired results, allowing you to identify places where a user defined conflict trigger or an application design change may be desirable.
Row values may optionally be logged for row conflicts. This is controlled by the global database-wide option [`bdr.log_conflicts_to_table`](https://file+.vscode-resource.vscode-cdn.net/Users/davecra/projects/amazon/abba/abba-pg-bdr/md/bdr-configuration-variables.md#GUC-BDR-LOG-CONFLICTS-TO-TABLE). There is no per-table control over row value logging at this time. Nor is there any limit applied on the number of fields a row may have, number of elements dumped in arrays, length of fields, etc, so it may not be wise to enable this if you regularly work with multi-megabyte rows that may trigger conflicts.
Because the conflict history table contains data on every table in the database so each row's schema might be different, if row values are logged they are stored as JSON fields. The JSON is created with `row_to_json()`, just like if you'd called it on the row yourself from SQL. There is no corresponding `json_to_row()` function in PostgreSQL at this time, so you'll need table-specific code (pl/pgsql, pl/python, pl/perl, whatever) if you want to reconstruct a composite-typed tuple from the logged JSON.




#### Differences between Logical and Physical Replication

The major differences between physical replication and logical replication as implemented by BDR are:

* Multi-master replication is possible. All members are writable nodes that replicate changes.
* Data from index writes, `VACUUM`{.LITERAL}, hint bits, etc are not sent over the network, so bandwidth requirements may be reduced - especially when compared to physical replication with `full_page_writes`{.LITERAL}.
* There is no need to use [`hot_standby_feedback`{.LITERAL}](http://www.postgresql.org/docs/current/static/runtime-config-replication.html#GUC-HOT-STANDBY-FEEDBACK){target="_top"} or to cancel long running queries on hot standbys, so there aren't any ["cancelling statement due to conflict with recovery"]{.QUOTE} errors.
* Temporary tables may be used on replicas.
* Tables that aren't being replicated from elsewhere may be written to BDR.
* Replication across major versions (e.g. 9.4 to 9.5) can be supported (though BDR imposes limitations on that, [pglogical](http://2ndquadrant.com/pglogical){target="_top"} supports it well).
* Replication across architectures and OSes (e.g. PPC64 Linux to x86_64 OS X) is supported.
* Replication is per-database (or even table-level), whereas physical replication can and must replicate all databases. ([pglogical](http://2ndquadrant.com/pglogical){target="_top"} even supports row- and column-level filtering of replication).
* BDR's logical replication implementation imposes some restrictions on supported DDL (see: [DDL replication](https://file+.vscode-resource.vscode-cdn.net/Users/davecra/projects/amazon/abba/abba-pg-bdr/md/ddl-replication.md)) that do not apply for physical replication
* Because it's database-level not cluster-level, commands that affect all databases, like `ALTER SYSTEM`{.LITERAL} or `CREATE ROLE`{.LITERAL} are [*not*]{.emphasis} replicated by BDR and must be managed by the administrator.
* Disk random I/O requirements and flush frequency may be higher than for physical replication.
* Only completed transactions are replicated. Big transactions may have longer replication delays because replication doesn't start until the transaction completes. Aborted transactions' writes are never replicated at all.
* Logical replication requires at least PostgreSQL 9.4.
* Logical replication cannot be used for point-in-time recovery (though it can support a replication delay). It's technically possible to add this capability if someone needs it, though.
* Logical replication only works via streaming, not WAL file archiving, and requires the use of a [replication slot](http://www.postgresql.org/docs/current/static/logicaldecoding-explanation.html){target="_top"}.
* Cascading replication is not (yet) supported by logical replication.
* Large objects (pg_largeobject, lo_create, and so on) are not handled by logical decoding, so it cannot be replicated by BDR
* Sequence updates are not replicated by logical replication, as the underlying logical decoding facility does not support them. Traditional sequences don't work in an active-active environment anyway, so BDR offers alternatives.




