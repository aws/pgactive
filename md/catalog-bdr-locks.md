::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                                                               
  --------------------------------------------------------------------------------------- ------------------------------------------ -------------------------------- -----------------------------------------------------------------------------
  [Prev](catalog-bdr-conflict-handlers.md "bdr.bdr_conflict_handlers"){accesskey="P"}   [Up](catalogs-views.md){accesskey="U"}    Chapter 13. Catalogs and Views    [Next](catalog-bdr-global-locks.md "bdr.bdr_global_locks"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [13.8. bdr.bdr_locks]{#CATALOG-BDR-LOCKS} {#bdr.bdr_locks .SECT1}

`bdr.bdr_locks`{.LITERAL} is a view exposing the state of BDR\'s [global
DDL locking system](catalog-bdr-node-slots.md). It can be used to
diagnose DDL locking problems and monitor the system. Query this view
for lock state instead of using
[bdr.bdr_global_locks](catalog-bdr-global-locks.md) directly.

The information in this view is local to each node. It will not
necessarily be the same on every node in a BDR group. If nothing else,
the `lock_state`{.STRUCTFIELD} on the node acquiring or holding the
global DDL lock will always be different to the state on the other
nodes.

::: TABLE
[]{#AEN4182}

**Table 13-7. `bdr.bdr_locks`{.STRUCTNAME} Columns**

  Name                                  Type               References                                                    Description
  ------------------------------------- ------------------ ------------------------------------------------------------- ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  `owner_is_my_node`{.STRUCTFIELD}      `boolean`{.TYPE}                                                                 True unless another node is known to hold or be acquiring the global DDL lock
  `owner_sysid`{.STRUCTFIELD}           `text`{.TYPE}      `bdr.bdr_nodes`{.STRUCTNAME}`.node_sysid`{.STRUCTFIELD}       Node identity of the node holding or acquiring the lock
  `owner_timeline`{.STRUCTFIELD}        `oid`{.TYPE}       `bdr.bdr_nodes`{.STRUCTNAME}`.node_timeline`{.STRUCTFIELD}    Node identity of the node holding or acquiring the lock
  `owner_dboid`{.STRUCTFIELD}           `oid`{.TYPE}       `bdr.bdr_nodes`{.STRUCTNAME}`.node_dboid`{.STRUCTFIELD}       Node identity of the node holding or acquiring the lock
  `owner_node_name`{.STRUCTFIELD}       `text`{.TYPE}      `bdr.bdr_nodes`{.STRUCTNAME}`.node_name`{.STRUCTFIELD}        Node name of the node holding or acquiring the lock
  `lock_mode`{.STRUCTFIELD}             `text`{.TYPE}                                                                    Requested/held lock mode for DDL lock, or null if no locking currently in progress. Current modes are `ddl_lock`{.LITERAL} and `write_lock`{.LITERAL}.
  `lock_state`{.STRUCTFIELD}            `text`{.TYPE}                                                                    Progress of lock acquisition, explained in breakout below.
  `owner_local_pid`{.STRUCTFIELD}       `integer`{.TYPE}                                                                 Process ID of backend acquiring lock if the locker is local to the current node. Null if there is no lock or the locker is on a different node. (A future version may report PIDs for remote nodes, so do not rely on this).
  `owner_is_my_backend`{.STRUCTFIELD}   `boolean`{.TYPE}                                                                 True only if the currently querying backend on the local node is acquiring or holds the lock. A shortcut for testing `owner_local_pid`{.STRUCTFIELD} and `owner_node_name`{.STRUCTFIELD} etc.
  `owner_replorigin`{.STRUCTFIELD}      `oid`{.TYPE}       `pg_catalog.pg_replorigin`{.STRUCTNAME}`.oid`{.STRUCTFIELD}   Replication origin ID for the node acquiring/holding the lock. You should usually look at the node name or identity tuple instead.
  `lockcount`{.STRUCTFIELD}             `integer`{.TYPE}                                                                 Number of locks held. Debug parameter. (Always 0 or 1).
  `npeers`{.STRUCTFIELD}                `integer`{.TYPE}                                                                 Number of nodes known to be participating in locking. Debug parameter.
  `npeers_confirmed`{.STRUCTFIELD}      `integer`{.TYPE}                                                                 Number of peers for which a confirmation reply has been processed on this node, if lock acquisition is in progress (but not yet complete) and this node is the locker (state `acquire_tally_confirmations`{.LITERAL}). Debug parameter. Will rarely be equal to `npeers`{.STRUCTFIELD} since that successfully concludes a locking request.
  `npeers_declined`{.STRUCTFIELD}       `integer`{.TYPE}                                                                 Number of peers for which a decline-lock reply has been processed on this node, if lock acquisition is in progress (but not yet complete) and this node is the locker (state `acquire_tally_confirmations`{.LITERAL}). Debug parameter. Likely to always be 0 since one decline terminates a locking request.
  `npeers_replayed`{.STRUCTFIELD}       `integer`{.TYPE}                                                                 Number of peers that have confirmed successful replay up to *`replay_lsn`{.REPLACEABLE}* for this node. Node will be in state `peer_catchup`{.LITERAL}.
  `replay_upto`{.STRUCTFIELD}           `pg_lsn`{.TYPE}                                                                  LSN (Log Sequence Number, i.e. WAL position) of local node up to which peers must replay before they can send replay confirmation. The current replay position can be seen in peer nodes\' `pg_stat_replication`{.STRUCTNAME}.`replay_location`{.STRUCTFIELD} entries for this node.
:::

See also [DDL replication](ddl-replication.md) and
[Monitoring](monitoring.md). For more information on how global DDL
locking works, see [DDL
Locking](ddl-replication-advice.md#DDL-REPLICATION-LOCKING).

Possible lock states are:

-   `nolock`{.LITERAL} - There is no locking activity on the node

-   `acquire_tally_confirmations`{.LITERAL} - This node is acquiring the
    global DDL lock. `owner_local_pid`{.STRUCTFIELD} contains the pid of
    the acquiring transaction. It has taken the local DDL lock and has
    sent lock requests to peers. It is waiting for all peers to respond.
    The count of peer responses is tallied in
    `npeers_confirmed`{.STRUCTFIELD}.

-   `acquire_acquired`{.LITERAL} - This node has acquired the global DDL
    lock. `owner_local_pid`{.STRUCTFIELD} contains the pid of the
    acquiring transaction. All peers have confirmed that their local
    locks are acquired.

-   `peer_begin_catchup`{.LITERAL} - This node has just received a lock
    request from another node that wants to acquire the DDL lock.

-   `peer_cancel_xacts`{.LITERAL} - A peer node wants to acquire the
    global DDL lock in `write_lock`{.LITERAL} mode. This node is waiting
    for local write transactions to complete within their grace periods
    or respond to cancel requests.

-   `peer_catchup`{.LITERAL} - A peer node wants to acquire the global
    DDL lock in `write_lock`{.LITERAL} mode. This node has no local
    write transactions running. It has sent replay confirmation requests
    for peers to confirm replay up to lsn `replay_upto`{.STRUCTFIELD}
    from its peers and is waiting for their responses, which are tallied
    in `npeers_replayed`{.STRUCTFIELD}

-   `peer_confirmed`{.LITERAL} - A peer node wants to acquire the global
    DDL lock. This node has acquired its local DDL lock and sent
    confirmation to the peer.

These correspond to `BDRLockState`{.TYPE} values in the source code.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ----------------------------------------------------------- ------------------------------------------ ------------------------------------------------------
  [Prev](catalog-bdr-conflict-handlers.md){accesskey="P"}       [Home](index.md){accesskey="H"}        [Next](catalog-bdr-global-locks.md){accesskey="N"}
  bdr.bdr_conflict_handlers                                    [Up](catalogs-views.md){accesskey="U"}                                    bdr.bdr_global_locks
  ----------------------------------------------------------- ------------------------------------------ ------------------------------------------------------
:::
