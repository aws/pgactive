# Reference Guide

Table of contents
- [Active-Active conflicts](#active-active-conflicts)
- [Public Functions](#public-functions)
- [Internal Functions](#internal-functions)
- [Private Functions](#private-functions)

## Active-Active conflicts

In Active-Active use of [pgactive] writes to the same or related table(s) from multiple different nodes can result in data conflicts.

Some clustering systems use distributed lock mechanisms to prevent concurrent access to data. These can perform reasonably when servers are very close but cannot support geographically distributed applications as very low latency is critical for acceptable performance.

Distributed locking is essentially a pessimistic approach, whereas pgactive advocates an optimistic approach: avoid conflicts where possible but allow some types of conflict to occur and and resolve them when they arise.

### How conflicts happen

Inter-node conflicts arise as a result of sequences of events that could not happen if all the involved transactions happened concurrently on the same node. Because the nodes only exchange changes after transactions commit, each transaction is individually valid on the node it committed on but would not be valid if run on another node that has done other work in the mean time. Since [pgactive] apply essentially replays the transaction on the other nodes, the replay operation can fail if there is a conflict between a transaction being applied and a transaction that was committed on the receiving node.

The reason most conflicts can't happen when all transactions run on a single node is that PostgreSQL has inter-transaction communication mechanisms to prevent it - UNIQUE indexes, SEQUENCEs, row and relation locking, SERIALIZABLE dependency tracking, etc. All of these mechanisms are ways to communicate between transactions to prevent undesirable concurrency issues.

[pgactive] does not have a distributed transaction manager or lock manager. That's part of why it performs well with latency and network partitions. As a result, so [transactions on different nodes execute entirely in isolation from each other]. Despite the usual perception that "more isolation is good" you actually need to reduce isolation to prevent conflicts.

### Types of conflicts

#### PRIMARY KEY or UNIQUE conflicts

The most common conflicts are row conflicts where two operations affect a row with the same key in ways they could not do on a single node. [pgactive] can detect most of those and apply last-update-wins conflict handling or invoke a user-defined conflict handler.

Row conflicts include:

    INSERT vs INSERT

    INSERT vs UPDATE

    UPDATE vs DELETE

    INSERT vs DELETE

    DELETE vs DELETE

##### INSERT/INSERT conflicts

The most common conflict, INSERT vs INSERT, arises where INSERTs on two different nodes create a tuple with the same PRIMARY KEY values (or the same values for a single UNIQUE constraint if no PRIMARY KEY exists). [pgactive] handles this by retaining the most recently inserted tuple of the two according to the originating host's timestamps unless a user-defined conflict handler overrides this.

No special administrator action is required to deal with these conflicts, but the user must understand that [one of the INSERTed tuples is effectively discarded on all nodes] - there is no data merging done unless a user defined conflict handler does it.

Conflict handling is only possible when [only one constraint is violated by the incoming insert from the remote node]; INSERTs that violate multiple UNIQUE constraints are more problematic.

##### INSERTs that violate multiple UNIQUE constraints

An INSERT/INSERT conflict can violate more than one UNIQUE constraint (of which one might be the PRIMARY KEY).

[pgactive] can only handle an INSERT/INSERT conflict on one unique constraint (including the PRIMARY KEY). If a new row conflicts with more than one UNIQUE constraint then the apply worker that's trying to apply the change will ERROR out with:

     ERROR: multiple unique constraints violated by remotely INSERTed tuple
     

(Older versions would report a "diverging uniqueness conflict" error instead).

In case of such a conflict, you must manually remove the conflicting tuple(s) from the local side by DELETEing it or by UPDATEing it so that it no longer conflicts with the new remote tuple. There may be more than one conflicting tuple. There is not currently any built-in facility to ignore, discard or merge tuples that violate more than one local unique constraint.

See also: UPDATEs that violate multiple UNIQUE constraints

##### UPDATE/UPDATE conflicts

Where two concurrent UPDATEs on different nodes change the same tuple (but not its PRIMARY KEY), an UPDATE/UPDATE conflict occurs on replay. These are resolved using last-update-wins handling or, if it exists, a user-defined conflict handler.

Because a PRIMARY KEY must exist in order to match tuples and perform conflict resolution, UPDATEs are rejected on tables without a PRIMARY KEY with:

      ERROR: Cannot run UPDATE or DELETE on table (tablename) because it does not have a primary key.
     

##### UPDATE conflicts on the PRIMARY KEY

[pgactive] cannot currently perform last-update-wins conflict resolution where the PRIMARY KEY is changed by an UPDATE operation. It is permissible to update the primary key, but you must ensure that no conflict with existing values is possible.

Conflicts on update of the primary key are divergent conflicts that require manual operator intervention; see Divergent conflicts.

##### UPDATEs that violate multiple UNIQUE constraints

Like INSERTs that violate multiple UNIQUE constraints, where an incoming UPDATE violates more than one UNIQUE index (and/or the PRIMARY KEY), [pgactive] cannot apply last-update-wins conflict resolution.

This is a divergent conflict that will require operator intervention; see Divergent conflicts.

##### UPDATE/DELETE conflicts

It is possible for one node to UPDATE a row that another node simultaneously DELETEs. In this case a UPDATE/DELETE conflict occurs on replay. The resolution of this conflict is to discard any UPDATE that arrives after the DELETE unless a user-defined conflict handler specifies otherwise.

Because a PRIMARY KEY must exist in order to match tuples and perform conflict resolution, DELETEs are rejected on tables without a PRIMARY KEY with:

      ERROR: Cannot run UPDATE or DELETE on table (tablename) because it does not have a primary key.
     

    Note: [pgactive] cannot currently differentiate between UPDATE/DELETE conflicts and INSERT/UPDATE conflicts. In both cases an UPDATE arrives that affects a nonexistent row. Since [pgactive] is asynchronous and there's no ordering of replay between nodes, it can't tell if this is an update to a new row we haven't yet received the insert for, or a row we've already replayed a delete for. In both cases the resolution is the same - the update is discarded.

##### INSERT/UPDATE conflicts

If one node INSERTs a row which is then replayed to a 2nd node and UPDATEd there, a 3rd node may receive the UPDATE from the 2nd node before it receives the INSERT from the 1st node. This is an INSERT/UPDATE conflict.

Unless a user defined conflict trigger determines otherwise these conflicts are handled by discarding the UPDATE. This can lead to [different data on different nodes]. See UPDATE/DELETE conflicts for details.

##### DELETE/DELETE conflicts

A DELETE/DELETE conflict arises where two different nodes concurrently delete the same tuple.

This conflict is harmless since both DELETEs have the same effect, so one of them can be safely ignored.

#### Foreign Key Constraint conflicts

Conflicts between a remote transaction being applied and existing local data can also occur for FOREIGN KEY constraints. These conflicts are usually transient issues that arise from transactions being applied in a different order to the order they appeared to occur logically on the nodes that originated them.

pgactive applies changes with session_replication_role = 'replica' so foreign keys are [not] checked when applying changes. In a Active-Active environment this can result in FK violations. Most are transient and only exist until replication catches up with changes from another node, but it's also possible to create dangling FKs because there's no inter-node row locking in pgactive. This is a necessary consequence of a partition-tolerant asynchronous Active-Active system, since one node can insert and commit a new child row in some FK relationship at the same time another node concurrently deletes the parent row. It's recommended that use of FKs be constrained to sets of closely related entities that are generally modified from only one node, are infrequently modified, or where the modification's concurrency is application-mediated.

#### Exclusion constraint conflicts

[pgactive] doesn't support exclusion constraints and restricts their creation.

    Important: If an existing stand-alone database is converted to a [pgactive] database then all exclusion constraints should be manually dropped.

In a distributed asynchronous system it is not possible to ensure that no set of rows that violates the constraint exists, because all transactions on different nodes are fully isolated. Exclusion constraints would lead to replay deadlocks where replay could not progress from any node to any other node because of exclusion constraint violations.

If you force [pgactive] to create an exclusion constraint, or you don't drop existing ones when converting a standalone database to [pgactive] you should expect replication to break. You can get it to progress again by removing or altering the local tuple(s) that an incoming remote tuple conflicts with so that the remote transaction can be applied.


#### Global data conflicts

Conflicts can also arise where nodes have global (PostgreSQL-system-wide) data, like roles, that differs. This can result in operations - mainly DDL - that can be run successfully and committed on one node, but then fail to apply to other nodes.

For example, node1 might have a user named fred, but that user was not created on node2. [pgactive] does not replicate CREATE USER (see CREATE ROLE/USER/GROUP ) so this situation can arise easily. If fred on node1 creates a table, it will be replicated with its owner set to fred. When the DDL command is applied to node2 the DDL will fail because there is no user named fred. This failure will emit an ERROR in the PostgreSQL logs on node2 and increment pgactive.pgactive_stats.nr_rollbacks.

Administrator intervention is required to resolve this conflict by creating the user fred on node2. (It need not have the same permissions, but must exist).

For example, node1 might have a table named foo created, but that this table was not created on node2. [pgactive] does not replicate DDL ( fesature to be added in future ) so this situation can arise easily. Any DML operations on foo table on node1 will fail on node2. The DML will fail because there is no table named foo.

Administrator intervention is required to resolve this conflict by creating the table foo on node2.

#### Lock conflicts and deadlock aborts

Because [pogactive] apply processes operate very like normal user sessions they are subject to the usual rules around row and table locking. This can sometimes lead to [pgactive] apply processes waiting on locks held by user transactions, or even by each other.

Relevant locking includes;

    explicit table-level locking (LOCK TABLE ...) by user sessions

    explicit row level locking (SELECT ... FOR UPDATE/FOR SHARE) by user sessions

    locking from foreign keys

    implicit locking because of row UPDATEs, INSERTs or DELETEs, either from local activity or apply from other servers

It is even possible for a [pgactive] apply process to deadlock with a user transaction, where the user transaction is waiting on a lock held by the apply process and vice versa. Two apply processes may also deadlock with each other. PostgreSQL's deadlock detector will step in and terminate one of the problem transactions. If the [pgactive] apply worker's process is terminated it will simply retry and generally succeed.

All these issues are transient and generally require no administrator action. If an apply process is stuck for a long time behind a lock on an idle user session the administrator may choose to terminate the user session to get replication flowing again, but this is no different to a user holding a long lock that impacts another user session.

Use of the log_lock_waits facility in PostgreSQL can help identify locking related replay stalls.

#### Divergent conflicts

Divergent conflicts arise when data that should be the same on different nodes differs unexpectedly. Divergent conflicts should not occur, but not all such conflicts can be reliably prevented at time of writing.

Warning Changing the PRIMARY KEY of a row can lead to a divergent conflict if another node changes the key of the same row before all nodes have replayed the change. Avoid changing primary keys, or change them only on one designated node. See UPDATE conflicts on the PRIMARY KEY.

Divergent conflicts involving row data generally require administrator action to manually adjust the data on one of the nodes to be consistent with the other one while replication is temporarily disabled using pgactive.pgactive_do_not_replicate. Such conflicts should not arise so long as [pgactive] is used as documented and settings or functions marked as unsafe are avoided.

The administrator must manually resolve such conflicts. Use of the advanced options pgactive.pgactive_do_not_replicate is required depending on the nature of the conflict. However, careless use of these options can make things much worse and it isn't possible to give general instructions for resolving all possible kinds of conflict.


### Avoiding or tolerating conflicts

In most cases appropriate application design can be used to avoid conflicts and/or the application can be made tolerant of conflicts.

Conflicts can only happen if there are things happening at the same time on multiple nodes, so the simplest way to avoid conflicts is to only ever write to one node, or to only ever write to independent subsets of the database on each node. For example, each node might have a separate schema, and while they all exchange data with each other, writes are only ever performed on the node that "owns" a given schema.

For INSERT vs INSERT conflicts, use of Global sequences can completely prevent conflicts.

pgactive users may sometimes find it useful to perform distributed locking at the application level in cases where conflicts are not acceptable.

The best course of action is frequently to allow conflicts to occur and design the application to work with [pgactive]'s conflict resolution mechansisms to cope with the conflict. See Types of conflict.

### User defined conflict handlers

User defined conflicts is a planned feature for the future.

### Conflict logging

To make diagnosis and handling of Active-Active conflicts easier, [pgactive] supports logging of each conflict incident in a pgactive.pgactive_conflict_history table.

Conflict logging to this table is only enabled when pgactive.log_conflicts_to_table is true. pgactive also logs conflicts to the PostgreSQL log file if log_min_messages is LOG or lower, irrespective of the value of pgactive.log_conflicts_to_table.

You can use the conflict history table to determine how rapidly your application creates conflicts and where those conflicts occur, allowing you to improve the application to reduce conflict rates. It also helps detect cases where conflict resolutions may not have produced the desired results, allowing you to identify places where a user defined conflict trigger or an application design change may be desirable.

Row values may optionally be logged for row conflicts. This is controlled by the global database-wide option pgactive.log_conflicts_to_table. There is no per-table control over row value logging at this time. Nor is there any limit applied on the number of fields a row may have, number of elements dumped in arrays, length of fields, etc, so it may not be wise to enable this if you regularly work with multi-megabyte rows that may trigger conflicts.

Because the conflict history table contains data on every table in the database so each row's schema might be different, if row values are logged they are stored as json fields. The json is created with row_to_json, just like if you'd called it on the row yourself from SQL. There is no corresponding json_to_row function in PostgreSQL at this time, so you'll need table-specific code (pl/pgsql, pl/python, pl/perl, whatever) if you want to reconstruct a composite-typed tuple from the logged json.

## Public Functions

### get\_last\_applied\_xact\_info

Gets last applied transaction info of apply worker for a given node.

### pgactive\_apply\_pause

Pause applying replication.

### pgactive\_apply\_resume

Resume applying replication.

### pgactive\_is\_apply\_paused

Chewck if replication apply is paused.

### pgactive\_create\_group

Create a pgactive group, turning a stand-alone database into the first node in a pgactive group.

### pgactive\_detach\_nodes

Detach node(s) from pgactive group.

### pgactive\_get\_connection\_replication\_sets

Get replication sets for the given node.

### pgactive\_get\_replication\_lag\_info

Gets replication lag info.

### pgactive\_get\_stats

Get pgactive replication stats.

### pgactive\_join\_group

Join an existing pgactive group by connecting to a member node and copying its contents.

### pgactive\_remove

Remove all traces of pgactive from the local node.

### pgactive\_snowflake\_id\_nextval

Generate sequence values unique to this node using a local sequence as a seed

### pgactive\_update\_node\_conninfo

Update pgactive node connection info.

### Internal Functions

These internal functions are not recommended for general use.

### check\_file\_system\_mount\_points

Checks if given two paths are on same file system mount points.

### get\_free\_disk\_space

Gets free disk space in bytes of filesystem to which given path is mounted.

### has\_required\_privs

Checks if current user has required privileges.

### pgactive\_acquire\_global\_lock

TBD

### pgactive\_assign\_seq\_ids\_post\_upgrade

TBD

### pgactive\_connections\_changed

Function to notify other background info to refresh connectiob.

### pgactive\_conninfo\_cmp

Checks if given two connectgions are same.

### pgactive\_create\_conflict\_handler 

TBD

### pgactive\_drop\_conflict\_handler

TBD

### pgactive\_fdw\_validator

TBD

### pgactive\_format\_replident\_name

TBD

### pgactive\_format\_slot\_name

TBD

### pgactive\_get\_connection\_replication\_sets

TBD

### pgactive\_get\_connection\_replication\_sets

TBD

### pgactive\_get\_last\_applied\_xact\_info

TBD

### pgactive\_get\_local\_node\_name

TBD

### pgactive\_get\_local\_nodeid

TBD

### pgactive\_get\_node\_identifier

TBD

### pgactive\_get\_table\_replication\_sets

TBD

### pgactive\_get\_workers\_info

TBD

### pgactive\_handle\_rejoin

TBD

### pgactive\_internal\_create\_truncate\_trigger

TBD

### pgactive\_is\_active\_in\_db

TBD

### pgactive\_min\_remote\_version\_num

TBD

### pgactive\_node\_status\_from\_char

TBD

### pgactive\_node\_status\_to\_char

TBD

### pgactive\_parse\_replident\_name

TBD

### pgactive\_parse\_slot\_name

TBD

### pgactive\_queue\_truncate

TBD

### pgactive\_replicate\_ddl\_command

TBD

### pgactive\_set\_connection\_replication\_sets

TBD

### pgactive\_set\_node\_read\_only

TBD

### pgactive\_set\_table\_replication\_sets

TBD

### pgactive\_skip\_changes

TBD

### pgactive\_terminate\_workers

TBD

### pgactive\_truncate\_trigger\_add

TBD

### pgactive\_variant

TBD

### pgactive\_version

TBD

### pgactive\_version\_num

TBD

### pgactive\_wait\_for\_node\_ready

TBD

### pgactive\_wait\_for\_slots\_confirmed\_flush\_lsn

TBD

### pgactive\_xact\_replication\_origin

TBD

## Private Functions

These private functions are not recommended for general use.

### \_pgactive\_begin\_join\_private

### \_pgactive\_begin\_join\_private

### \_pgactive\_check\_file\_system\_mount\_points

### \_pgactive\_destroy\_temporary\_dump\_directories\_private

### \_pgactive\_generate\_node\_identifier\_private

### \_pgactive\_get\_free\_disk\_space

### \_pgactive\_get\_node\_info\_private

### \_pgactive\_has\_required\_privs

### \_pgactive\_join\_node\_private

### \_pgactive\_nid\_shmem\_reset\_all\_private

### \_pgactive\_pause\_worker\_management\_private

### \_pgactive\_snowflake\_id\_nextval\_private

### \_pgactive\_update\_seclabel\_private

