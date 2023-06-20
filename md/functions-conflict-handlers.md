  [BDR 2.0.7 Documentation](README.md)                                                                                                              
  [Prev](functions-replication-sets.md "Replication Set functions")   [Up](functions.md)    Chapter 12. Functions    [Next](functions-information.md "Information functions")  


# 12.3. Conflict handler management functions

The following functions manage conflict handlers (\"conflict
triggers\"):


**Table 12-3. Conflict handler management functions**

  Function                                                                                                                                                                                                                                                   Return Type   Description
  `bdr.bdr_create_conflict_handler(`*`ch_rel`*`, `*`ch_name`*`, `*`ch_proc`*`, `*`ch_type`*`, `*`ch_timeframe`*`)`   void          Registers a conflict handler procedure named *`ch_name`* on table *`ch_rel`* to invoke the conflict handler procedure *`ch_proc`* when a conflict occurs within the interval *`ch_timeframe`*. See [Active-Active conflicts](conflicts.md) for details.
  `bdr.bdr_create_conflict_handler(`*`ch_rel`*`, `*`ch_name`*`, `*`ch_proc`*`, `*`ch_type`*`)`                                                void          The same as above, but always invoked irrespective of how different the two conflicting rows are in age, so takes no *`timeframe`* argument.
  `bdr.bdr_drop_conflict_handler(`*`ch_rel`*`, `*`ch_name`*`)`                                                                                                                                  void          Unregisters the conflict handler procedure named *`ch_name`* on table *`ch_rel`*. See [Active-Active conflicts](conflicts.md).



  -------------------------------------------------------- ------------------------------------- ---------------------------------------------------
  [Prev](functions-replication-sets.md)     [Home](README.md)     [Next](functions-information.md)  
  Replication Set functions                                 [Up](functions.md)                                Information functions
  -------------------------------------------------------- ------------------------------------- ---------------------------------------------------
