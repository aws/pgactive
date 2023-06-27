  [BDR 2.0.7 Documentation](README.md)                                                                                              
  [Prev](quickstart.md "Quick-start guide")   [Up](quickstart.md)    Chapter 3. Quick-start guide    [Next](quickstart-editing.md "Editing the configuration files to enable BDR")  


# 3.1. Creating BDR-enabled PostgreSQL nodes/instances

Since we\'re creating two new PostgreSQL node/instances for this
example, run:

``` PROGRAMLISTING
     mkdir -p $HOME/2ndquadrant_bdr
     initdb -D $HOME/2ndquadrant_bdr/bdr5598 -A trust -U postgres
     initdb -D $HOME/2ndquadrant_bdr/bdr5599 -A trust -U postgres
     
```

Adjust the data directory path (the path after `-D`) if you
want to use a different location. The rest of these instructions will
assume you ran exactly the commands given above.

These commands do [*not*] start BDR, or connect the two
instances. They just create two independent PostgreSQL instances, ready
to be configured and started.

> **Note:** In a production install you should use the operating system
> and package management system\'s standard locations for the data
> directory and startup scripts. Manual initdb is mainly is suitable for
> test and experimentation.



  ---------------------------------------- -------------------------------------- ------------------------------------------------
  [Prev](quickstart.md)     [Home](README.md)      [Next](quickstart-editing.md)  
  Quick-start guide                         [Up](quickstart.md)     Editing the configuration files to enable BDR
  ---------------------------------------- -------------------------------------- ------------------------------------------------
