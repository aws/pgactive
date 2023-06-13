::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ------------------------------------------------------------------------------------------------- ------------------------------------- ----------------------- -------------------------------------------------------------------
  [Prev](functions-conflict-handlers.md "Conflict handler management functions"){accesskey="P"}   [Up](functions.md){accesskey="U"}    Chapter 12. Functions    [Next](functions-upgrade.md "Upgrade functions"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [12.4. Information functions]{#FUNCTIONS-INFORMATION} {#information-functions .SECT1}

The following functions provide information about a BDR node:

::: TABLE
[]{#AEN3270}

**Table 12-4. Node information functions**

Function
:::
:::

Return Type

Description

`bdr.bdr_version()`{.FUNCTION}

text

Report the [BDR]{.PRODUCTNAME} version in human-readable
*`major.minor.rev-yyyy-mm-dd-gitrev`{.REPLACEABLE}* text form, with
build date and git revision, e.g. `0.9.0-2015-02-08-3f3fb7c`{.LITERAL}.

`bdr.bdr_version_num()`{.FUNCTION}

integer

Report just the [BDR]{.PRODUCTNAME} version number in numeric AAAABBCC
form, (A: major, B: minor, C: rev) e.g. `0.9.0`{.LITERAL} is
`900`{.LITERAL} (00000900).

`bdr.bdr_min_remote_version_num()`{.FUNCTION}

integer

Return the oldest version of the [BDR]{.PRODUCTNAME} extension that this
node can compatibly receive streamed changes from.

[]{#FUNCTIONS-BDR-GET-LOCAL-NODE-NAME}

`bdr.bdr_get_local_node_name()`{.FUNCTION}

text

Look up the local node in `bdr.bdr_nodes`{.LITERAL} and return the node
name - or null if the node is not a [BDR]{.PRODUCTNAME} peer

[]{#FUNCTIONS-BDR-GET-LOCAL-NODE-ID}

`bdr.bdr_get_local_nodeid()`{.FUNCTION}

record

Returns a tuple containing the local node\'s `sysid`{.LITERAL},
`timeline`{.LITERAL}, and `dboid`{.LITERAL}.

`bdr.bdr_get_remote_nodeinfo(`{.FUNCTION}*`peer_dsn`{.REPLACEABLE}*`)`{.FUNCTION}

record

Connect to a remote node and interrogate it for [BDR]{.PRODUCTNAME}
information. This function is primarily for [BDR]{.PRODUCTNAME} internal
use during setup and connection establishment.

`bdr.bdr_test_remote_connectback(`{.FUNCTION}*`peer_dsn`{.REPLACEABLE}*`, `{.FUNCTION}*`local_dsn`{.REPLACEABLE}*`)`{.FUNCTION}

record

Ask a remote node to connect back to this node. This function is
primarily for [BDR]{.PRODUCTNAME} internal use during setup and
connection establishment.

::: NAVFOOTER

------------------------------------------------------------------------

  --------------------------------------------------------- ------------------------------------- -----------------------------------------------
  [Prev](functions-conflict-handlers.md){accesskey="P"}     [Home](index.md){accesskey="H"}     [Next](functions-upgrade.md){accesskey="N"}
  Conflict handler management functions                      [Up](functions.md){accesskey="U"}                                Upgrade functions
  --------------------------------------------------------- ------------------------------------- -----------------------------------------------
:::
