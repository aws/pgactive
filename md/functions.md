  [BDR 2.0.7 Documentation](README.md)                                                                                          
  [Prev](replication-sets-changetype.md "Change-type replication sets")   [Up](manual.md)        [Next](functions-node-mgmt.md "Node management functions")  


# []{#FUNCTIONS}Chapter 12. Functions

**Table of Contents**

12.1. [Node management functions](functions-node-mgmt.md)

12.1.1.
[`bdr.skip_changes_upto`](functions-node-mgmt.md#FUNCTION-BDR-SKIP-CHANGES-UPTO)

12.1.2.
[`bdr.bdr_subscribe`](functions-node-mgmt.md#FUNCTIONS-NODE-MGMT-SUBSCRIBE)

12.1.3. [Node management function
examples](functions-node-mgmt.md#FUNCTIONS-NODE-MGMT-EXAMPLES)

12.2. [Replication Set functions](functions-replication-sets.md)

12.3. [Conflict handler management
functions](functions-conflict-handlers.md)

12.4. [Information functions](functions-information.md)

12.5. [Upgrade functions](functions-upgrade.md)

[BDR] management is primarily accomplished via
SQL-callable functions. Functions intended for direct use by the end
user are documented here.

All functions in [BDR] are exposed in the `bdr`
schema. Unless you put this on your `search_path` you\'ll need
to schema-qualify their names.

  **Warning**
  Do [*not*] directly call functions with the prefix `internal`, they are intended for [BDR]\'s internal use only and may lack sanity checks present in the public-facing functions and [*could break your replication setup*]. Stick to using the functions documented here, others are subject to change without notice.



  --------------------------------------------------------- ----------------------------------- -------------------------------------------------
  [Prev](replication-sets-changetype.md)    [Home](README.md)    [Next](functions-node-mgmt.md)  
  Change-type replication sets                               [Up](manual.md)                           Node management functions
  --------------------------------------------------------- ----------------------------------- -------------------------------------------------
