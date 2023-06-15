::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ------------------------------------------------------------------------------------ ------------------------------------- ----------------------------------- ------------------------------------------------------------------
  [Prev](conflicts-avoidance.md "Avoiding or tolerating conflicts"){accesskey="P"}   [Up](conflicts.md){accesskey="U"}    Chapter 9. Multi-master conflicts    [Next](conflicts-logging.md "Conflict logging"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [9.4. User defined conflict handlers]{#CONFLICTS-USER-DEFINED-HANDLERS} {#user-defined-conflict-handlers .SECT1}

[BDR]{.PRODUCTNAME} provides facilities for users to override the
default last-update-wins data row conflict resolution strategy on row
key conflicts.

A user defined conflict handler, if provided, is called before default
row conflict resolution is performed. The user defined handler may
choose to ignore the new row and keep the original local row, to apply
the new row, or to generate a new row (possibly merging old and new) and
apply that instead of the new incoming row. A conflict handler may also
choose to `ERROR`{.LITERAL} out, which can be useful if it wishes to
abort apply of a transaction and retry it later.

Conflict handlers cannot skip whole transactions.

::: NOTE
> **Note:** User-defined conflict handlers do not have access to both
> the old and new versions of the remote row, so they cannot tell which
> field(s) in the remote incoming tuple changed. It is thus not possible
> to do reliable row merging. Attempts to so for the general case will
> usually prove to be incorrect in an asynchronous replication
> envirionment. It\'s possible in some application-specific situations
> where the app \"knows\" more about the data.
:::

See also: [Conflict handler management
functions](functions-conflict-handlers.md)
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------------- ------------------------------------- -----------------------------------------------
  [Prev](conflicts-avoidance.md){accesskey="P"}     [Home](index.md){accesskey="H"}     [Next](conflicts-logging.md){accesskey="N"}
  Avoiding or tolerating conflicts                   [Up](conflicts.md){accesskey="U"}                                 Conflict logging
  ------------------------------------------------- ------------------------------------- -----------------------------------------------
:::
