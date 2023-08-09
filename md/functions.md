  [BDR 2.0.7 Documentation](README.md)                                                                                          
  [Prev](replication-sets-changetype.md "Change-type replication sets")   [Up](manual.md)        [Next](functions-node-mgmt.md "Node management functions")  


# Chapter 12. Functions

**Table of Contents**

12.1. [Node management functions](functions-node-mgmt.md)

12.1.1.
[`bdr.bdr_skip_changes`](functions-node-mgmt.md#FUNCTION-BDR-SKIP-CHANGES)

12.1.2. [Node management function
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
  Do [*not*] directly call functions with the prefix `_bdr` and suffix `_private`, they are intended for [BDR]\'s internal use only and may lack sanity checks present in the public-facing functions and [*could break your replication setup*]. Stick to using the functions documented here, others are subject to change without notice.

In the latest version of BDR, some of the user-facing extension SQL functions or views are renamed to be more redable and consistent, following is the list shows the changes:

| Old name                        | New name                               |
|---------------------------------|----------------------------------------|
| bdr_part_by_node_names          | bdr_detach_nodes                       |
| remove_bdr_from_local_node      | bdr_remove                             |
| bdr_node_join_wait_for_ready    | bdr_wait_for_node_ready                |
| bdr_apply_is_paused             | bdr_is_apply_paused                    |
| pg_stat_get_bdr                 | bdr_get_stats                          |
| pg_stat_bdr                     | bdr_stats                              |
| table_get_replication_sets      | bdr_get_table_replication_sets         |
| table_set_replication_sets      | bdr_set_table_replication_sets         |
| internal_node_join              | _bdr_join_node_private                 |
| internal_update_seclabel        | _bdr_update_seclabel_private           |
| internal_begin_join             | _bdr_begin_join_private                |
| bdr_group_join                  | bdr_join_group                         |
| bdr_group_create                | bdr_create_group                       |
| bdr_node_set_read_only          | bdr_set_node_read_only                 |
| skip_changes_upto               | bdr_skip_changes                       |
| connection_get_replication_sets | bdr_get_connection_replication_sets    |
| connection_set_replication_sets | bdr_set_connection_replication_sets    |
| _test_pause_worker_management   | _bdr_pause_worker_management_private   |
| acquire_global_lock             | bdr_acquire_global_lock                |
| global_lock_info                | bdr_get_global_locks_info              |
| bdr_locks                       | bdr_global_locks_info              |
| wait_slot_confirm_lsn           | bdr_wait_for_slots_confirmed_flush_lsn |
| queue_truncate                  | bdr_queue_truncate                     |
| node_status_from_char           | bdr_node_status_from_char              |
| node_status_to_char             | bdr_node_status_to_char                |
| get_transaction_replorigin      | bdr_xact_replication_origin            |
| upgrade_to_200                  | bdr_assign_seq_ids_post_upgrade        |
| global_seq_nextval              | bdr_snowflake_id_nextval               |
| global_seq_nextval_test         | _bdr_snowflake_id_nextval_private      |

  --------------------------------------------------------- ----------------------------------- -------------------------------------------------
  [Prev](replication-sets-changetype.md)    [Home](README.md)    [Next](functions-node-mgmt.md)  
  Change-type replication sets                               [Up](manual.md)                           Node management functions
  --------------------------------------------------------- ----------------------------------- -------------------------------------------------
