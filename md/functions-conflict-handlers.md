::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ------------------------------------------------------------------------------------ ------------------------------------- ----------------------- ---------------------------------------------------------------------------
  [Prev](functions-replication-sets.md "Replication Set functions"){accesskey="P"}   [Up](functions.md){accesskey="U"}    Chapter 12. Functions    [Next](functions-information.md "Information functions"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [12.3. Conflict handler management functions]{#FUNCTIONS-CONFLICT-HANDLERS} {#conflict-handler-management-functions .SECT1}

The following functions manage conflict handlers (\"conflict
triggers\"):

::: TABLE
[]{#AEN3216}

**Table 12-3. Conflict handler management functions**

  Function                                                                                                                                                                                                                                                   Return Type   Description
  ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- ------------- ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  `bdr.bdr_create_conflict_handler(`{.FUNCTION}*`ch_rel`{.REPLACEABLE}*`, `{.FUNCTION}*`ch_name`{.REPLACEABLE}*`, `{.FUNCTION}*`ch_proc`{.REPLACEABLE}*`, `{.FUNCTION}*`ch_type`{.REPLACEABLE}*`, `{.FUNCTION}*`ch_timeframe`{.REPLACEABLE}*`)`{.FUNCTION}   void          Registers a conflict handler procedure named *`ch_name`{.REPLACEABLE}* on table *`ch_rel`{.REPLACEABLE}* to invoke the conflict handler procedure *`ch_proc`{.REPLACEABLE}* when a conflict occurs within the interval *`ch_timeframe`{.REPLACEABLE}*. See [Multi-master conflicts](conflicts.md) for details.
  `bdr.bdr_create_conflict_handler(`{.FUNCTION}*`ch_rel`{.REPLACEABLE}*`, `{.FUNCTION}*`ch_name`{.REPLACEABLE}*`, `{.FUNCTION}*`ch_proc`{.REPLACEABLE}*`, `{.FUNCTION}*`ch_type`{.REPLACEABLE}*`)`{.FUNCTION}                                                void          The same as above, but always invoked irrespective of how different the two conflicting rows are in age, so takes no *`timeframe`{.REPLACEABLE}* argument.
  `bdr.bdr_drop_conflict_handler(`{.FUNCTION}*`ch_rel`{.REPLACEABLE}*`, `{.FUNCTION}*`ch_name`{.REPLACEABLE}*`)`{.FUNCTION}                                                                                                                                  void          Unregisters the conflict handler procedure named *`ch_name`{.REPLACEABLE}* on table *`ch_rel`{.REPLACEABLE}*. See [Multi-master conflicts](conflicts.md).
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  -------------------------------------------------------- ------------------------------------- ---------------------------------------------------
  [Prev](functions-replication-sets.md){accesskey="P"}     [Home](index.md){accesskey="H"}     [Next](functions-information.md){accesskey="N"}
  Replication Set functions                                 [Up](functions.md){accesskey="U"}                                Information functions
  -------------------------------------------------------- ------------------------------------- ---------------------------------------------------
:::
