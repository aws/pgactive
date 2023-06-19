  [BDR 2.0.7 Documentation](README.md)                                                                                                                                         
  [Prev](quickstart-starting.md "Starting the BDR-enabled PostgreSQL nodes/instances")   [Up](quickstart.md)    Chapter 3. Quick-start guide    [Next](quickstart-enabling.md "Enabling BDR in SQL sessions for both of your nodes/instances")  


# [3.4. Creating the demo databases]

Create the databases for this demo on each node/instance from the
command line of your operating system:

``` PROGRAMLISTING
    createdb -p 5598 -U postgres bdrdemo
    createdb -p 5599 -U postgres bdrdemo
    
```



  ----------------------------------------------------- -------------------------------------- ---------------------------------------------------------------
  [Prev](quickstart-starting.md)         [Home](README.md)                    [Next](quickstart-enabling.md)  
  Starting the BDR-enabled PostgreSQL nodes/instances    [Up](quickstart.md)    Enabling BDR in SQL sessions for both of your nodes/instances
  ----------------------------------------------------- -------------------------------------- ---------------------------------------------------------------
