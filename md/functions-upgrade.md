::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  --------------------------------------------------------------------------- ------------------------------------- ----------------------- -----------------------------------------------------------------
  [Prev](functions-information.md "Information functions"){accesskey="P"}   [Up](functions.md){accesskey="U"}    Chapter 12. Functions    [Next](catalogs-views.md "Catalogs and Views"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [12.5. Upgrade functions]{#FUNCTIONS-UPGRADE} {#upgrade-functions .SECT1}

The following function(s) are used when upgrading [BDR]{.PRODUCTNAME} to
a new version:

::: TABLE
[]{#AEN3352}

**Table 12-5. Upgrade functions**

  Function                                                                                                                                                                                                 Return Type   Description
  -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- ------------- ------------------------------------------------------------------------------------------------------------------
  `bdr.bdr_upgrade_to_090(`{.FUNCTION}*`my_conninfo cstring`{.REPLACEABLE}*`, `{.FUNCTION}*`local_conninfo cstring`{.REPLACEABLE}*`, `{.FUNCTION}*`remote_conninfo cstring`{.REPLACEABLE}*`)`{.FUNCTION}   void          Used during upgrade from 0.7.x or 0.8.x to [BDR]{.PRODUCTNAME} version 0.9.x. See [Upgrading BDR](upgrade.md).
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  --------------------------------------------------- ------------------------------------- --------------------------------------------
  [Prev](functions-information.md){accesskey="P"}     [Home](index.md){accesskey="H"}     [Next](catalogs-views.md){accesskey="N"}
  Information functions                                [Up](functions.md){accesskey="U"}                            Catalogs and Views
  --------------------------------------------------- ------------------------------------- --------------------------------------------
:::
