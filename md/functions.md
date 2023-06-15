::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ---------------------------------------------------------------------------------------- ---------------------------------- -- -----------------------------------------------------------------------------
  [Prev](replication-sets-changetype.md "Change-type replication sets"){accesskey="P"}   [Up](manual.md){accesskey="U"}        [Next](functions-node-mgmt.md "Node management functions"){accesskey="N"}

------------------------------------------------------------------------
:::

::: CHAPTER
# []{#FUNCTIONS}Chapter 12. Functions

::: TOC
**Table of Contents**

12.1. [Node management functions](functions-node-mgmt.md)

12.1.1.
[`bdr.skip_changes_upto`{.LITERAL}](functions-node-mgmt.md#FUNCTION-BDR-SKIP-CHANGES-UPTO)

12.1.2.
[`bdr.bdr_subscribe`{.FUNCTION}](functions-node-mgmt.md#FUNCTIONS-NODE-MGMT-SUBSCRIBE)

12.1.3. [Node management function
examples](functions-node-mgmt.md#FUNCTIONS-NODE-MGMT-EXAMPLES)

12.2. [Replication Set functions](functions-replication-sets.md)

12.3. [Conflict handler management
functions](functions-conflict-handlers.md)

12.4. [Information functions](functions-information.md)

12.5. [Upgrade functions](functions-upgrade.md)
:::

[BDR]{.PRODUCTNAME} management is primarily accomplished via
SQL-callable functions. Functions intended for direct use by the end
user are documented here.

All functions in [BDR]{.PRODUCTNAME} are exposed in the `bdr`{.LITERAL}
schema. Unless you put this on your `search_path`{.LITERAL} you\'ll need
to schema-qualify their names.

::: WARNING
  ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  **Warning**
  Do [*not*]{.emphasis} directly call functions with the prefix `internal`{.LITERAL}, they are intended for [BDR]{.PRODUCTNAME}\'s internal use only and may lack sanity checks present in the public-facing functions and [*could break your replication setup*]{.emphasis}. Stick to using the functions documented here, others are subject to change without notice.
  ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  --------------------------------------------------------- ----------------------------------- -------------------------------------------------
  [Prev](replication-sets-changetype.md){accesskey="P"}    [Home](index.md){accesskey="H"}    [Next](functions-node-mgmt.md){accesskey="N"}
  Change-type replication sets                               [Up](manual.md){accesskey="U"}                           Node management functions
  --------------------------------------------------------- ----------------------------------- -------------------------------------------------
:::
