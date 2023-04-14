::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                              
  ------------------------------------------------------------ -------------------------------------- ------------------------------ ------------------------------------------------------------------------------------------------
  [Prev](quickstart.md "Quick-start guide"){accesskey="P"}   [Up](quickstart.md){accesskey="U"}    Chapter 3. Quick-start guide    [Next](quickstart-editing.md "Editing the configuration files to enable BDR"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [3.1. Creating BDR-enabled PostgreSQL nodes/instances]{#QUICKSTART-INSTANCES} {#creating-bdr-enabled-postgresql-nodesinstances .SECT1}

Since we\'re creating two new PostgreSQL node/instances for this
example, run:

``` PROGRAMLISTING
     mkdir -p $HOME/2ndquadrant_bdr
     initdb -D $HOME/2ndquadrant_bdr/bdr5598 -A trust -U postgres
     initdb -D $HOME/2ndquadrant_bdr/bdr5599 -A trust -U postgres
     
```

Adjust the data directory path (the path after `-D`{.LITERAL}) if you
want to use a different location. The rest of these instructions will
assume you ran exactly the commands given above.

These commands do [*not*]{.emphasis} start BDR, or connect the two
instances. They just create two independent PostgreSQL instances, ready
to be configured and started.

::: NOTE
> **Note:** In a production install you should use the operating system
> and package management system\'s standard locations for the data
> directory and startup scripts. Manual initdb is mainly is suitable for
> test and experimentation.
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ---------------------------------------- -------------------------------------- ------------------------------------------------
  [Prev](quickstart.md){accesskey="P"}     [Home](index.md){accesskey="H"}      [Next](quickstart-editing.md){accesskey="N"}
  Quick-start guide                         [Up](quickstart.md){accesskey="U"}     Editing the configuration files to enable BDR
  ---------------------------------------- -------------------------------------- ------------------------------------------------
:::
