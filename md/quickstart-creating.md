::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ------------------------------------------------------------------------------------------------------- -------------------------------------- ------------------------------ -----------------------------------------------------------------------------------------------------------------
  [Prev](quickstart-starting.md "Starting the BDR-enabled PostgreSQL nodes/instances"){accesskey="P"}   [Up](quickstart.md){accesskey="U"}    Chapter 3. Quick-start guide    [Next](quickstart-enabling.md "Enabling BDR in SQL sessions for both of your nodes/instances"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [3.4. Creating the demo databases]{#QUICKSTART-CREATING} {#creating-the-demo-databases .SECT1}

Create the databases for this demo on each node/instance from the
command line of your operating system:

``` PROGRAMLISTING
    createdb -p 5598 -U postgres bdrdemo
    createdb -p 5599 -U postgres bdrdemo

```
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ----------------------------------------------------- -------------------------------------- ---------------------------------------------------------------
  [Prev](quickstart-starting.md){accesskey="P"}         [Home](index.md){accesskey="H"}                    [Next](quickstart-enabling.md){accesskey="N"}
  Starting the BDR-enabled PostgreSQL nodes/instances    [Up](quickstart.md){accesskey="U"}    Enabling BDR in SQL sessions for both of your nodes/instances
  ----------------------------------------------------- -------------------------------------- ---------------------------------------------------------------
:::
