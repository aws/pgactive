  [BDR 2.0.7 Documentation](README.md)                                                                                                    
  [Prev](node-management.md "Node Management")   [Up](node-management.md)    Chapter 5. Node Management    [Next](node-management-removing.md "Parting (removing) a node")  


# [5.1. Joining a node]

When a new BDR node is joined to an existing BDR group, node is
subscribed to an upstream peer, the system must copy the existing data
from the peer node(s) to the local node before replication can begin.
This copy has to be carefully co-ordinated so that the local and remote
data starts out [*identical*], so it\'s not sufficient to
just use [pg_dump] yourself. The extension provides
built-in facilities for making this initial copy.

Every BDR node must be [*online and reachable*] when an
attempt to join a new node is made. Otherwise the join will hang
indefinitely or fail. BDR is a mesh where every node must be able to
communicate with every other node, and while it is tolerant of network
partitions and interruptions all nodes need to know about every other
node that exist. See [BDR mesh network](technotes-mesh.md).

There are two ways to join a new BDR node: logical or physical copy.
After the initial copy is done there is no significant difference
between physical or logical initialization of a BDR node, so the choice
is down to which setup method will be quickest and easiest for your
particular needs.

In a logical copy, a blank database in an existing standalone PostgreSQL
instance is enabled for BDR via SQL functions calls. The BDR extension
makes a connection to an upstream node designated by the user and takes
a schema and data dump of that node. The dump is then applied to the
local blank database before replication begins. Only the specified
database is copied. With a logical copy you don\'t have to create new
init scripts, run separate instances on separate ports, etc, as
everything happens in your existing PostgreSQL instance.

In a physical copy, the [bdr_init_copy](command-bdr-init-copy.md) is
used to clone a user-designated upstream node. This clone is then
reconfigured and started up as a new node before replication begins. All
databases on the remote node are copied, though only the specified
database is initially activated for BDR. (Support for multiple database
join may be added at a later date). After a physical node join or
subscribe the admin will generally need to separately register the new
PostgreSQL instance with the operating system to auto-start, as
PostgreSQL does not do this automatically. You may also need to select a
different PostgreSQL port if there is already a local PostgreSQL
instance.

The advantages and disadvantages of each approach roughly mirror those
of a logical backup using [pg_dump] and
[pg_restore] vs a physical copy using
[pg_basebackup]. See the [PostgreSQL documentation on
backup and
restore](http://www.postgresql.org/docs/current/static/backup.html)
for more information.

In general it\'s more convenient to use logical join when you have an
existing PostgreSQL instance, a reasonably small database, and other
databases you might not also want to copy/replicate. Physical join is
more appropriate for big databases that are the only database in a given
PostgreSQL install.

For the details, see [Joining or creating a BDR
node](node-management-joining.md#NODE-MANAGEMENT-JOINING-BDR).

## [5.1.1. Joining or creating a BDR node]

> **Note:** Read [Joining a node](node-management-joining.md) before
> this section.

For BDR every node has to have a connection to every other node. To make
configuration easy, when a new node joins it automatically configures
all existing nodes to connect to it. For this reason, every node,
including the first BDR node created, must know the [PostgreSQL
connection
string](https://www.postgresql.org/docs/9.4/static/libpq-connect.html#LIBPQ-CONNSTRING)
(sometimes referred to as a DSN, for \"data source name\") that other
nodes can use to connect to it.

The SQL function
[bdr.bdr_group_create](functions-node-mgmt.md#FUNCTION-BDR-GROUP-CREATE)
is used to create the first node of a BDR cluster from a standalone
PostgreSQL database. Doing so makes BDR active on that database and
allows other nodes to join the BDR cluster (which consists out of one
node at that point). You must specify the connection string that other
nodes will use to connect to this node at the time of creation.

Whether you plan on using logical or physical copy to join subsequent
nodes, the first node must always be created using
[bdr.bdr_group_create](functions-node-mgmt.md#FUNCTION-BDR-GROUP-CREATE).

Once the initial node is created every further node can join the BDR
cluster using the
[bdr.bdr_group_join](functions-node-mgmt.md#FUNCTION-BDR-GROUP-JOIN)
function or using [bdr_init_copy](command-bdr-init-copy.md).

Either way, when joining you must nominate a single node that is already
a member of the BDR group as the join target. This node\'s contents are
copied to become the initial state of the newly joined node. The new
node will then synchronise with the other nodes to ensure it has the
same contents as the others.

Generally you should pick whatever node is closest to the new node in
network terms as the join target.

Which node you choose to copy only really matters if you are using
non-default [Replication Sets](replication-sets.md). See the
replication sets documentation for more information on this.

See also: [Node management functions](functions-node-mgmt.md),
[bdr_init_copy](command-bdr-init-copy.md).



  --------------------------------------------- ------------------------------------------- ------------------------------------------------------
  [Prev](node-management.md)        [Home](README.md)        [Next](node-management-removing.md)  
  Node Management                                [Up](node-management.md)                               Parting (removing) a node
  --------------------------------------------- ------------------------------------------- ------------------------------------------------------
