  [BDR 2.1.0 Documentation](README.md)                                                                                                                           
  [Prev](functions-conflict-handlers.md "Conflict handler management functions")   [Up](functions.md)    Chapter 12. Functions    [Next](functions-upgrade.md "Upgrade functions")  


# 12.4. Information functions

The following functions provide information about a BDR node:


**Table 12-4. Node information functions**

Function

Return Type

Description

`bdr.bdr_version()`

text

Report the [BDR] version in human-readable
*`major.minor.rev-yyyy-mm-dd-gitrev`* text form, with
build date and git revision, e.g. `0.9.0-2015-02-08-3f3fb7c`.

`bdr.bdr_version_num()`

integer

Report just the [BDR] version number in numeric AAAABBCC
form, (A: major, B: minor, C: rev) e.g. `0.9.0` is
`900` (00000900).

`bdr.bdr_min_remote_version_num()`

integer

Return the oldest version of the [BDR] extension that this
node can compatibly receive streamed changes from.


`bdr.bdr_get_local_node_name()`

text

Look up the local node in `bdr.bdr_nodes` and return the node
name - or null if the node is not a [BDR] peer


`bdr.bdr_get_local_nodeid()`

record

Returns a tuple containing the local node\'s `sysid`,
`timeline`, and `dboid`.

`bdr.bdr_get_remote_nodeinfo(`*`peer_dsn`*`)`

record

Connect to a remote node and interrogate it for [BDR]
information. This function is primarily for [BDR] internal
use during setup and connection establishment.

`bdr.bdr_test_remote_connectback(`*`peer_dsn`*`, `*`local_dsn`*`)`

record

Ask a remote node to connect back to this node. This function is
primarily for [BDR] internal use during setup and
connection establishment.



  --------------------------------------------------------- ------------------------------------- -----------------------------------------------
  [Prev](functions-conflict-handlers.md)     [Home](README.md)     [Next](functions-upgrade.md)  
  Conflict handler management functions                      [Up](functions.md)                                Upgrade functions
  --------------------------------------------------------- ------------------------------------- -----------------------------------------------
