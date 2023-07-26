  [BDR 2.0.7 Documentation](README.md)                                                                                     
  [Prev](quickstart-testing.md "Testing your BDR-enabled system")   [Home](README.md)        [Next](settings.md "Configuration Settings")  



# II. BDR administration manual

**Table of Contents**

4\. [Configuration Settings](settings.md)

4.1. [Prerequisite [PostgreSQL]
parameters](settings-prerequisite.md)

4.2. [BDR specific configuration
variables](bdr-configuration-variables.md)

5\. [Node Management](node-management.md)

5.1. [Joining a node](node-management-joining.md)

5.2. [Detaching (removing) a node](node-management-removing.md)

5.3. [Completely removing BDR](node-management-disabling.md)

5.4. [Rejoining a BDR node](node-management-rejoining.md)

5.5. [n-safe synchronous replication](node-management-synchronous.md)

6\. [Command-line Utilities](commands.md)

6.1. [bdr_init_copy](command-bdr-init-copy.md)

7\. [Monitoring](monitoring.md)

7.1. [Why monitoring matters](monitoring-why.md)

7.2. [Monitoring node join/removal](monitoring-node-join-remove.md)

7.3. [Monitoring replication peers](monitoring-peers.md)

7.4. [Monitoring global DDL locks](monitoring-ddl-lock.md)

7.5. [Monitoring conflicts](monitoring-conflict-stats.md)

7.6. [PostgreSQL statistics views](monitoring-postgres-stats.md)

8\. [DDL Replication](ddl-replication.md)

8.1. [Executing DDL on BDR systems](ddl-replication-advice.md)

8.2. [Statement specific DDL replication
concerns](ddl-replication-statements.md)

9\. [Active-Active conflicts](conflicts.md)

9.1. [How conflicts happen](conflicts-how.md)

9.2. [Types of conflict](conflicts-types.md)

9.3. [Avoiding or tolerating conflicts](conflicts-avoidance.md)

9.4. [User defined conflict
handlers](conflicts-user-defined-handlers.md)

9.5. [Conflict logging](conflicts-logging.md)

10\. [Global Sequences](global-sequences.md)

10.1. [Purpose of global sequences](global-sequences-purpose.md)

10.2. [When to use global sequences](global-sequences-when.md)

10.3. [Using global sequences](global-sequence-usage.md)

10.4. [Global sequence limitations](global-sequence-limitations.md)

10.5. [Global sequences and ORMs](global-sequences-orms.md)

10.6. [Traditional approaches to sequences in distributed
DBs](global-sequences-alternatives.md)

11\. [Replication Sets](replication-sets.md)

11.1. [Replication Set Concepts](replication-sets-concepts.md)

11.2. [Creating replication sets](replication-sets-creation.md)

11.3. [Node Replication Control](replication-sets-nodes.md)

11.4. [Table Replication Control](replication-sets-tables.md)

11.5. [Change-type replication sets](replication-sets-changetype.md)

12\. [Functions](functions.md)

12.1. [Node management functions](functions-node-mgmt.md)

12.2. [Replication Set functions](functions-replication-sets.md)

12.3. [Conflict handler management
functions](functions-conflict-handlers.md)

12.4. [Information functions](functions-information.md)

12.5. [Upgrade functions](functions-upgrade.md)

13\. [Catalogs and Views](catalogs-views.md)

13.1. [bdr.bdr_nodes](catalog-bdr-nodes.md)

13.2. [bdr.bdr_connections](catalog-bdr-connections.md)

13.3. [bdr.bdr_node_slots](catalog-bdr-node-slots.md)

13.4. [bdr.bdr_stats](catalog-bdr-stats.md)

13.5. [bdr.bdr_conflict_history](catalog-bdr-conflict-history.md)

13.6.
[bdr.bdr_replication_set_config](catalog-bdr-replication-set-config.md)

13.7. [bdr.bdr_conflict_handlers](catalog-bdr-conflict-handlers.md)

13.8. [bdr.bdr_global_locks_info](catalog-bdr-global-locks-info.md)

13.9. [bdr.bdr_global_locks](catalog-bdr-global-locks.md)

13.10. [bdr.bdr_queued_commands](catalog-bdr-queued-commands.md)

13.11. [bdr.bdr_queued_drops](catalog-bdr-queued-drops.md)

14. [Upgrading [BDR]](upgrade.md)

  ------------------------------------------------ ----------------------------------- --------------------------------------
  [Prev](quickstart-testing.md)    [Home](README.md)    [Next](settings.md)  
  Testing your BDR-enabled system                                                                      Configuration Settings
  ------------------------------------------------ ----------------------------------- --------------------------------------
