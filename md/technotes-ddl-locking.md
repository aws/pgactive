  [BDR 2.0.7 Documentation](README.md)                                                                                                    
  -------------------------------------------------------------------- ------------------------------------- ----------------------------- ----------------------------------------------------------------------
  [Prev](technotes-mesh.md "BDR network structure")   [Up](technotes.md)    Appendix C. Technical notes    [Next](technotes-rewrites.md "Full table rewrites")  


# [C.2. DDL locking details]

To ensure complete consistency of some types of schema change operations
(DDL), BDR must be able to sometimes go into fully synchronous mode,
where all nodes flush all pending changes to each other, replay a
change, and confirm that change before any of them can proceed with new
work. See [DDL replication](ddl-replication.md). This also means all
nodes must be reachable, and it means that if we add a new node it must
be impossible for existing nodes that are currently down or unreachable
to gain a DDL lock and make schema changes until they can communicate
with the new node. This would require extra inter-node communication and
DDL locking protocol enhancements.

If BDR didn\'t go synchronous for schema changes, multiple nodes could
make conflicting schema changes. Worse, outstanding changes for the old
format of a table might not make sense when arriving at a node that has
the new format for a table. For example, the new table might have added
a new not-null column, but the incoming row doesn\'t have a value for
it. More complex cases also exist, and there\'s no simple resolution to
all such possible problems.

Some optimisations have already been made here. In particular, DDL that
won\'t cause apply conflicts only takes a weaker lock mode that doesn\'t
block writes. The weaker DDL lock mode also allows locking to proceed
without every server handshaking to every other server; it only needs
the requesting server to communicate with all its peers, not
transitively with their peers in turn. Only the DDL write lock now
requires that all nodes confirm that they have flushed all pending
transactions to all other nodes.

## [C.2.1. How the DDL lock works]

If you don\'t care how the global DDL lock works you can skip this
section, but understanding it will be useful when you\'re diagnosing
issues.

There are two levels to the DDL lock: the global DDL lock, which only
one node at a time may hold, and the local DDL lock, which each node has
separately. When the global DDL lock is held then all the local DDL
locks are held too.

Inter-node communication is done via WAL messages written to the
transaction logs and replayed by apply workers. So replication and
replay lag will result in lag with DDL locking too.

There are also two (currently) DDL lock modes. The weak \'ddl\' lock,
and the \'write\' lock. The global \'ddl\' mode prevents other nodes
from running any DDL while it is held by holding each node\'s local DDL
lock. The \'write\' mode further requires that all nodes complete all
in-progress transactions, disallow new write operations, and make sure
they have fully replayed all changes from their peers. BDR versions
prior to 1.0 only had the heavier-weight \'write\' mode lock.

The (somewhat simplified) process of DDL lock acquision is:

1.  A normal user backend attempts something that requires the DDL lock

2.  The BDR command filter notices that the DDL lock is needed, pauses
    the user\'s command, and requests that the local BDR node acquire
    the global DDL lock

3.  The local BDR node acquires its own local DDL lock. It will now
    reject any incoming lock requests from other nodes and will also
    reject attempts by other local transactions to perform DDL. DML is
    still permitted.

4.  The local DDL node writes a message in its replication stream to ask
    every other node to take their local DDL locks and reply to confirm
    they\'ve done so

5.  Every node that gets the request acquires the local DDL lock to
    prevent concurrent DDL and possibly writes, then replies to the
    requestor to confirm that its lock DDL lock is taken.

6.  When all peers have confirmed lock acquisition, the requesting node
    knows it now holds the global DDL lock. If it\'s acquiring a weak
    DDL lock it\'s done now. If it\'s acquiring a write lock it must
    wait until all peers confirm replay.

7.  If the DDL lock request was a write-lock request, each node
    receiving a lock request checks with every other node to see that
    they\'ve all replayed all outstanding changes from each other and
    waits for them all to reply with confirmation, then sends its own
    catchup confirmation.

8.  Once it has the global DDL lock, and (for write locks) knows all
    peers are caught up to each other, the requesting node is ready to
    proceed.

9.  The requesting node makes the required schema changes

10. The requesting node writes the fact that it\'s done with the DDL
    lock to its WAL in the form of a lock release message

11. The requesting node releases its local DDL lock and resumes normal
    write operations

12. The other nodes replay the lock release message and release their
    local DDL locks, resuming normal write operations

Critically, this means that for write-locks [*every BDR node must
complete a two-way communication with every other BDR node before the
DDL lock can be granted*]. This communication is done via the
replication stream, so replication lag and delays, network slowness or
outages, etc in turn delay the DDL locking process. While the system is
in the process of acquiring the DDL lock, many nodes will hold their
local DDL locks and will be rejecting other DDL requests or, if the lock
mode requires, rejecting writes.

Schema changes and anything else that takes the DDL lock should only be
performed when all nodes are reachable and there isn\'t a big
replication delay between any pair of nodes.

It also means that if the system gets stuck waiting for a down node,
everything stops while we wait.

If the DDL lock request is canceled by the requesting node, all the
other reachable nodes will release their locks. So if your system is
hung up on a DDL lock request that\'s making no progress you can just
cancel the statement that\'s requesting the DDL lock and everything will
resume normal operation.

Full details can be found in the comments on `bdr_locks.c`.



  -------------------------------------------- ------------------------------------- ------------------------------------------------
  [Prev](technotes-mesh.md)     [Home](README.md)     [Next](technotes-rewrites.md)  
  BDR network structure                         [Up](technotes.md)                               Full table rewrites
  -------------------------------------------- ------------------------------------- ------------------------------------------------
