::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                                                                                  
  ------------------------------------------------------------------------------------------------ -------------------------------------- ------------------------------ -------------------------------------------------------------------------------
  [Prev](quickstart-editing.md "Editing the configuration files to enable BDR"){accesskey="P"}   [Up](quickstart.md){accesskey="U"}    Chapter 3. Quick-start guide    [Next](quickstart-creating.md "Creating the demo databases"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [3.3. Starting the BDR-enabled PostgreSQL nodes/instances]{#QUICKSTART-STARTING} {#starting-the-bdr-enabled-postgresql-nodesinstances .SECT1}

Start your nodes/instances from the command line of your operating
system:

``` PROGRAMLISTING
    pg_ctl -l $HOME/2ndquadrant_bdr/bdr5598.log -D $HOME/2ndquadrant_bdr/bdr5598 -o "-p 5598" -w start
    pg_ctl -l $HOME/2ndquadrant_bdr/bdr5599.log -D $HOME/2ndquadrant_bdr/bdr5599 -o "-p 5599" -w start
    
```

Each node/instance will start up and then will run in the background.
You\'ll see the following:

``` PROGRAMLISTING
     waiting for server to start.... done
     server started
     
```

If you see an issue with starting your nodes/instances:

``` PROGRAMLISTING
     waiting for server to start........ stopped waiting
     pg_ctl: could not start server
     
```

\... then take a look at the log files (`bdr5598.log`{.FILENAME} or
`bdr5599.log`{.FILENAME} depending on which one failed to start.) Most
likely you already have a PostgreSQL instance running on the target
port. It is also possible that your `$PATH`{.LITERAL} is not set to
point to BDR, so you\'re trying to use binaries from a different
PostgreSQL release that won\'t have the `bdr`{.FILENAME} extension or
understand some of the configuration parameters.

::: NOTE
> **Note:** Because they were started manually and don\'t have an init
> script, these nodes/instances won\'t start automatically on re-boot.
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ------------------------------------------------ -------------------------------------- -------------------------------------------------
  [Prev](quickstart-editing.md){accesskey="P"}     [Home](index.md){accesskey="H"}      [Next](quickstart-creating.md){accesskey="N"}
  Editing the configuration files to enable BDR     [Up](quickstart.md){accesskey="U"}                        Creating the demo databases
  ------------------------------------------------ -------------------------------------- -------------------------------------------------
:::
