  [BDR 2.1.0 Documentation](README.md)

  [Prev](catalog-bdr-conflict-handlers.md "bdr.bdr_conflict_handlers")   [Up](catalogs-views.md)    Chapter 13. Catalogs and Views    [Next](catalog-bdr-global-locks.md "bdr.bdr_global_locks")


# 13.8. bdr.bdr_global_locks_info

`bdr.bdr_global_locks_info` is a view exposing the state of BDR\'s [global
DDL locking system](catalog-bdr-node-slots.md). It can be used to
diagnose DDL locking problems and monitor the system. Query this view
for lock state instead of using
[bdr.bdr_global_locks](catalog-bdr-global-locks.md) directly.

The information in this view is local to each node. It will not
necessarily be the same on every node in a BDR group. If nothing else,
the `lock_state` on the node acquiring or holding the
global DDL lock will always be different to the state on the other
nodes.


**Table 13-7. `bdr.bdr_global_locks_info` Columns**

  Name                                  Type               References                                                    Description
  `owner_is_my_node`      `boolean`                                                                 True unless another node is known to hold or be acquiring the global DDL lock
  `owner_sysid`           `text`      `bdr.bdr_nodes``.node_sysid`       Node identity of the node holding or acquiring the lock
  `owner_timeline`        `oid`       `bdr.bdr_nodes``.node_timeline`    Node identity of the node holding or acquiring the lock
  `owner_dboid`           `oid`       `bdr.bdr_nodes``.node_dboid`       Node identity of the node holding or acquiring the lock
  `owner_node_name`       `text`      `bdr.bdr_nodes``.node_name`        Node name of the node holding or acquiring the lock
  `lock_mode`             `text`                                                                    Requested/held lock mode for DDL lock, or null if no locking currently in progress. Current modes are `ddl_lock` and `write_lock`.
  `lock_state`            `text`                                                                    Progress of lock acquisition, explained in breakout below.
  `owner_local_pid`       `integer`                                                                 Process ID of backend acquiring lock if the locker is local to the current node. Null if there is no lock or the locker is on a different node. (A future version may report PIDs for remote nodes, so do not rely on this).
  `owner_is_my_backend`   `boolean`                                                                 True only if the currently querying backend on the local node is acquiring or holds the lock. A shortcut for testing `owner_local_pid` and `owner_node_name` etc.
  `owner_replorigin`      `oid`       `pg_catalog.pg_replorigin``.oid`   Replication origin ID for the node acquiring/holding the lock. You should usually look at the node name or identity tuple instead.
  `lockcount`             `integer`                                                                 Number of locks held. Debug parameter. (Always 0 or 1).
  `npeers`                `integer`                                                                 Number of nodes known to be participating in locking. Debug parameter.
  `npeers_confirmed`      `integer`                                                                 Number of peers for which a confirmation reply has been processed on this node, if lock acquisition is in progress (but not yet complete) and this node is the locker (state `acquire_tally_confirmations`). Debug parameter. Will rarely be equal to `npeers` since that successfully concludes a locking request.
  `npeers_declined`       `integer`                                                                 Number of peers for which a decline-lock reply has been processed on this node, if lock acquisition is in progress (but not yet complete) and this node is the locker (state `acquire_tally_confirmations`). Debug parameter. Likely to always be 0 since one decline terminates a locking request.
  `npeers_replayed`       `integer`                                                                 Number of peers that have confirmed successful replay up to *`replay_lsn`* for this node. Node will be in state `peer_catchup`.
  `replay_upto`           `pg_lsn`                                                                  LSN (Log Sequence Number, i.e. WAL position) of local node up to which peers must replay before they can send replay confirmation. The current replay position can be seen in peer nodes\' `pg_stat_replication`.`replay_location` entries for this node.

See also [DDL replication](ddl-replication.md) and
[Monitoring](monitoring.md). For more information on how global DDL
locking works, see [DDL
Locking](ddl-replication-advice.md#DDL-REPLICATION-LOCKING).

Possible lock states are:

-   `nolock` - There is no locking activity on the node

-   `acquire_tally_confirmations` - This node is acquiring the
    global DDL lock. `owner_local_pid` contains the pid of
    the acquiring transaction. It has taken the local DDL lock and has
    sent lock requests to peers. It is waiting for all peers to respond.
    The count of peer responses is tallied in
    `npeers_confirmed`.

-   `acquire_acquired` - This node has acquired the global DDL
    lock. `owner_local_pid` contains the pid of the
    acquiring transaction. All peers have confirmed that their local
    locks are acquired.

-   `peer_begin_catchup` - This node has just received a lock
    request from another node that wants to acquire the DDL lock.

-   `peer_cancel_xacts` - A peer node wants to acquire the
    global DDL lock in `write_lock` mode. This node is waiting
    for local write transactions to complete within their grace periods
    or respond to cancel requests.

-   `peer_catchup` - A peer node wants to acquire the global
    DDL lock in `write_lock` mode. This node has no local
    write transactions running. It has sent replay confirmation requests
    for peers to confirm replay up to lsn `replay_upto`
    from its peers and is waiting for their responses, which are tallied
    in `npeers_replayed`

-   `peer_confirmed` - A peer node wants to acquire the global
    DDL lock. This node has acquired its local DDL lock and sent
    confirmation to the peer.

These correspond to `BDRLockState` values in the source code.



  ----------------------------------------------------------- ------------------------------------------ ------------------------------------------------------
  [Prev](catalog-bdr-conflict-handlers.md)       [Home](README.md)        [Next](catalog-bdr-global-locks.md)
  bdr.bdr_conflict_handlers                                    [Up](catalogs-views.md)                                    bdr.bdr_global_locks
  ----------------------------------------------------------- ------------------------------------------ ------------------------------------------------------
