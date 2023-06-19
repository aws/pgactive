  [BDR 2.0.7 Documentation](README.md)                                                                                                                                  
  [Prev](quickstart-editing.md "Editing the configuration files to enable BDR")   [Up](quickstart.md)    Chapter 3. Quick-start guide    [Next](quickstart-creating.md "Creating the demo databases")  


# [3.3. Starting the BDR-enabled PostgreSQL nodes/instances]

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

\... then take a look at the log files (`bdr5598.log` or
`bdr5599.log` depending on which one failed to start.) Most
likely you already have a PostgreSQL instance running on the target
port. It is also possible that your `$PATH` is not set to
point to BDR, so you\'re trying to use binaries from a different
PostgreSQL release that won\'t have the `bdr` extension or
understand some of the configuration parameters.

> **Note:** Because they were started manually and don\'t have an init
> script, these nodes/instances won\'t start automatically on re-boot.



  ------------------------------------------------ -------------------------------------- -------------------------------------------------
  [Prev](quickstart-editing.md)     [Home](README.md)      [Next](quickstart-creating.md)  
  Editing the configuration files to enable BDR     [Up](quickstart.md)                        Creating the demo databases
  ------------------------------------------------ -------------------------------------- -------------------------------------------------
