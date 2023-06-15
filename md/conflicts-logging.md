::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ---------------------------------------------------------------------------------------------- ------------------------------------- ----------------------------------- -----------------------------------------------------------------
  [Prev](conflicts-user-defined-handlers.md "User defined conflict handlers"){accesskey="P"}   [Up](conflicts.md){accesskey="U"}    Chapter 9. Multi-master conflicts    [Next](global-sequences.md "Global Sequences"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [9.5. Conflict logging]{#CONFLICTS-LOGGING} {#conflict-logging .SECT1}

To make diagnosis and handling of multi-master conflicts easier,
[BDR]{.PRODUCTNAME} supports logging of each conflict incident in a
[bdr.bdr_conflict_history](catalog-bdr-conflict-history.md) table.

Conflict logging to this table is only enabled when
[bdr.log_conflicts_to_table](bdr-configuration-variables.md#GUC-BDR-LOG-CONFLICTS-TO-TABLE)
is `true`{.LITERAL}. BDR also logs conflicts to the PostgreSQL log file
if `log_min_messages`{.LITERAL} is `LOG`{.LITERAL} or lower,
irrespective of the value of `bdr.log_conflicts_to_table`{.LITERAL}.

You can use the conflict history table to determine how rapidly your
application creates conflicts and where those conflicts occur, allowing
you to improve the application to reduce conflict rates. It also helps
detect cases where conflict resolutions may not have produced the
desired results, allowing you to identify places where a user defined
conflict trigger or an application design change may be desirable.

Row values may optionally be logged for row conflicts. This is
controlled by the global database-wide option
[bdr.log_conflicts_to_table](bdr-configuration-variables.md#GUC-BDR-LOG-CONFLICTS-TO-TABLE).
There is no per-table control over row value logging at this time. Nor
is there any limit applied on the number of fields a row may have,
number of elements dumped in arrays, length of fields, etc, so it may
not be wise to enable this if you regularly work with multi-megabyte
rows that may trigger conflicts.

Because the conflict history table contains data on every table in the
database so each row\'s schema might be different, if row values are
logged they are stored as json fields. The json is created with
`row_to_json`{.FUNCTION}, just like if you\'d called it on the row
yourself from SQL. There is no corresponding `json_to_row`{.FUNCTION}
function in PostgreSQL at this time, so you\'ll need table-specific code
(pl/pgsql, pl/python, pl/perl, whatever) if you want to reconstruct a
composite-typed tuple from the logged json.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------------------------- ------------------------------------- ----------------------------------------------
  [Prev](conflicts-user-defined-handlers.md){accesskey="P"}     [Home](index.md){accesskey="H"}     [Next](global-sequences.md){accesskey="N"}
  User defined conflict handlers                                 [Up](conflicts.md){accesskey="U"}                                Global Sequences
  ------------------------------------------------------------- ------------------------------------- ----------------------------------------------
:::
