# Quick Start Guide

To run the examples in this post, you’ll need to provision at least two PostgreSQL instances running PostgreSQL 12 or higher. Also ensure that these PostgreSQL instances are network accessible to each other.

In this example we will create a database "app", some table, enable / setup pgactive to accept writes on all the PostgreSQL instance, and thus creating an Active-Active PostgreSQL database.

## Use pgactive to deploy an active-active PostgreSQL database

### 1. On each PostgreSQL instance, run the following command to create a database and switch to the new database:

```
CREATE DATABASE app;
\c app
```

### 2. Retrieve the network endpoints for the two PostgreSQL instances

For the purposes of this example, we call these endpoint1 and endpoint2.

### 3.To set up the PostgreSQL instance at endpoint1, log in to the instance at endpoint1 and first ensure that the shared_preload_libraries parameter contains pgactive:

```
SELECT setting ~ 'pgactive' 
FROM pg_catalog.pg_settings
WHERE name = 'shared_preload_libraries';
If pgactive is in shared_preload_libraries, you’ll observe the following output:

 ?column? 
----------
 t
```

### 4. Use the following commands to create a table that contains product information and add several products:

```
CREATE SCHEMA inventory;

CREATE TABLE inventory.products (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  product_name text NOT NULL,
  created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO inventory.products (product_name)
VALUES ('soap'), ('shampoo'), ('conditioner');
```

### 5. Now install pgactive into the database with the following command:

```
CREATE EXTENSION IF NOT EXISTS pgactive;
```

Next, you set up the connection information and credentials for the replication accounts to log into each node. You need to set up the connection information for both nodes, including the node you’re running the commands on, because pgactive needs information to connect both to the remote node and back to itself. pgactive uses the foreign data interface to securely manage and store these credentials.

### 6. On endpoint1, run the following commands to set up the connection information, substituting the placeholders with your actual values:

```
-- connection info for endpoint1
CREATE SERVER pgactive_server_endpoint1
    FOREIGN DATA WRAPPER pgactive_fdw
    OPTIONS (host '<endpoint1>', dbname 'app');
CREATE USER MAPPING FOR postgres
    SERVER pgactive_server_endpoint1
    OPTIONS (user 'postgres', password '<password>');

-- connection info for endpoint2
CREATE SERVER pgactive_server_endpoint2
    FOREIGN DATA WRAPPER pgactive_fdw
    OPTIONS (host '<endpoint2>', dbname 'app');
CREATE USER MAPPING FOR postgres
    SERVER pgactive_server_endpoint2
    OPTIONS (user 'postgres', password '<password>');
```

### 7. Now you can initialize the replication group and add this first instance:

```
SELECT pgactive.pgactive_create_group(
    node_name := 'endpoint1-app',
    node_dsn := 'user_mapping=postgres pgactive_foreign_server=pgactive_server_endpoint1'

);

SELECT pgactive.pgactive_wait_for_node_ready();
If the commands succeed, you’ll see the following output:

 pgactive_wait_for_node_ready 
------------------------------
 
(1 row)
```

### 8. Log in to the instance at endpoint2 and first ensure that the shared_preload_libraries parameter contains pgactive:

```
SELECT setting ~ 'pgactive' 
FROM pg_catalog.pg_settings
WHERE name = 'shared_preload_libraries';
If pgactive is in shared_preload_libraries, the preceding command will return the following:

 ?column? 
----------
 t
```

### 9. Ensure pgactive is installed in the database with the following command:

```
CREATE EXTENSION IF NOT EXISTS pgactive;
```
Next, you set up the connection information and credentials for the replication accounts to log into each node. pgactive uses the foreign data interface to securely manage and store these credentials.

### 10. On endpoint2, run the following commands to set up the connection information:

```
-- connection info for endpoint1
CREATE SERVER pgactive_server_endpoint1
    FOREIGN DATA WRAPPER pgactive_fdw
    OPTIONS (host '<endpoint1>', dbname 'app');
CREATE USER MAPPING FOR postgres
    SERVER pgactive_server_endpoint1
    OPTIONS (user 'postgres', password '<password>');

-- connection info for endpoint2
CREATE SERVER pgactive_server_endpoint2
    FOREIGN DATA WRAPPER pgactive_fdw
    OPTIONS (host '<endpoint2>', dbname 'app');
CREATE USER MAPPING FOR postgres
    SERVER pgactive_server_endpoint2
    OPTIONS (user 'postgres', password '<password>');
```

### 11. Join the PostgreSQL instance at endpoint2 to the active-active replication group:

```
SELECT pgactive.pgactive_join_group(
    node_name := 'endpoint2-app',
    node_dsn := 'user_mapping=postgres pgactive_foreign_server=pgactive_server_endpoint2',
    join_using_dsn := 'user_mapping=postgres pgactive_foreign_server=pgactive_server_endpoint1'
);

SELECT pgactive.pgactive_wait_for_node_ready();
```

If the commands succeed, pgactive will try to synchronize the database. If the command runs successfully, you will see output similar to the following:

```
NOTICE:  restoring database 'app', 6% of 7483 MB complete
NOTICE:  restoring database 'app', 42% of 7483 MB complete
NOTICE:  restoring database 'app', 77% of 7483 MB complete
NOTICE:  restoring database 'app', 98% of 7483 MB complete
NOTICE:  successfully restored database 'app' from node node1-app in 00:04:12.274956
 pgactive_wait_for_node_ready 
------------------------------
 
(1 row)

```

### 12. After a minute, at endpoint2, check to see that the products are present:

```
SELECT count(*) FROM inventory.products;
If the data successfully synchronized, you’ll see the following results:

 count
-------
 3
```

Let’s test that our setup works.

### 13. From endpoint2, run the following command:

```
INSERT INTO inventory.products (product_name)
VALUES ('lotion');
```

### 14. Connect to the app database at endpoint1 and run the following query:

```
SELECT count(*) FROM inventory.products;
You should see the following result:

 count
-------
 4
```

Your active-active PostgreSQL cluster is now initialized

### Monitoring replication lag

Monitoring and alerting replication lag is crucial for pgactive. pgactive can have lag at the decoding node or/and at the applying node. When receiving node is down due to maintenance, networking issue, or hardware issue, WAL will accumulate on the WAL sender node and if issue is not rectified on-time, sender node may run of disc space or WAL accumulate can get to a point where receiving node may never catch up. When at receiving node WAL apply results in error due to schema differences, unique/primary key violation, or other reasons, WAL will get accumulated on receiving node and eventually node will run out of disc space if issue is not rectified on time.


Monitoring replication lag lets you diagnose potential issues with your active-active replication and helps mitigate the risk of introducing conflicting changes into your system or running of disc space.

pgactive provide a handy function `pgactive.pgactive_get_replication_lag_info()`, this function can be executed from any pgactive node to capture at a glance lag info for the whole cluster.

Following output is from a three node pgactive setup where `pgactive.pgactive_get_replication_lag_info()` was executed on pgactive1 node.  In this output, lag between pgactive1 and pagactive2, pgactive2 and pgactive3, and pgactive3 and pgactive1 is shown.

```
postgres=# SELECT * FROM pgactive.pgactive_get_replication_lag_info();
-[ RECORD 1 ]--------+---------------------------------------------
node_name            | pgactive2
node_sysid           | 7412711618745234863
application_name     | pgactive:7412711618745234863:send
slot_name            | pgactive_16385_7412711618745234863_0_16385__
active               | t
active_pid           | 214501
pending_wal_decoding | 0
pending_wal_to_apply | 0
restart_lsn          | 0/1996D58
confirmed_flush_lsn  | 0/1996D90
sent_lsn             | 0/1996D90
write_lsn            | 0/1996D90
flush_lsn            | 0/1996D90
replay_lsn           | 0/1996D90
-[ RECORD 2 ]--------+---------------------------------------------
node_name            | pgactive3
node_sysid           | 7412711671844476412
application_name     | pgactive:7412711671844476412:send
slot_name            | pgactive_16385_7412711671844476412_0_16385__
active               | t
active_pid           | 214576
pending_wal_decoding | 0
pending_wal_to_apply | 0
restart_lsn          | 0/1996D58
confirmed_flush_lsn  | 0/1996D90
sent_lsn             | 0/1996D90
write_lsn            | 0/1996D90
flush_lsn            | 0/1996D90
replay_lsn           | 0/1996D90
-[ RECORD 3 ]--------+---------------------------------------------
node_name            | pgactive1
node_sysid           | 7412711576138986882
application_name     | pgactive:7412711576138986882:send
slot_name            | pgactive_16385_7412711576138986882_0_16385__
active               | t
active_pid           | 214505
pending_wal_decoding | 0
pending_wal_to_apply | 0
restart_lsn          | 0/198F1B0
confirmed_flush_lsn  | 0/198F1E8
sent_lsn             | 0/198F1E8
write_lsn            | 0/198F1E8
flush_lsn            | 0/198F1E8
replay_lsn           | 0/198F1E8
-[ RECORD 4 ]--------+---------------------------------------------
node_name            | pgactive3
node_sysid           | 7412711671844476412
application_name     | pgactive:7412711671844476412:send
slot_name            | pgactive_16385_7412711671844476412_0_16385__
active               | t
active_pid           | 214577
pending_wal_decoding | 0
pending_wal_to_apply | 0
restart_lsn          | 0/198F1B0
confirmed_flush_lsn  | 0/198F1E8
sent_lsn             | 0/198F1E8
write_lsn            | 0/198F1E8
flush_lsn            | 0/198F1E8
replay_lsn           | 0/198F1E8
-[ RECORD 5 ]--------+---------------------------------------------
node_name            | pgactive1
node_sysid           | 7412711576138986882
application_name     | pgactive:7412711576138986882:send
slot_name            | pgactive_16385_7412711576138986882_0_16385__
active               | t
active_pid           | 214582
pending_wal_decoding | 0
pending_wal_to_apply | 0
restart_lsn          | 0/198EC30
confirmed_flush_lsn  | 0/198EC68
sent_lsn             | 0/198EC68
write_lsn            | 0/198EC68
flush_lsn            | 0/198EC68
replay_lsn           | 0/198EC68
-[ RECORD 6 ]--------+---------------------------------------------
node_name            | pgactive2
node_sysid           | 7412711618745234863
application_name     | pgactive:7412711618745234863:send
slot_name            | pgactive_16385_7412711618745234863_0_16385__
active               | t
active_pid           | 214585
pending_wal_decoding | 0
pending_wal_to_apply | 0
restart_lsn          | 0/198EC30
confirmed_flush_lsn  | 0/198EC68
sent_lsn             | 0/198EC68
write_lsn            | 0/198EC68
flush_lsn            | 0/198EC68
replay_lsn           | 0/198EC68
```
At a minimum following alerts shall be monitored:

- Alert when active is false
- Alert when pending_wal_decoding is growing
- Alert when pending_wal_to_apply is growing


## Reviewing and correcting write conflicts

Conflicts in asynchronous active-active replication can occur when two active instances simultaneously modify the same row. Using the data in our endpoint1 and endpoint2 clusters as an example, let’s suppose a transaction on endpoint1 modifies product_name from soap to be sapone while a transaction on endpoint2 modifies product_name from soap to be savon before sapone is applied.

By default, pgactive logs all conflicts and uses the last-update-wins strategy of resolving conflicts, where it will accept the changes from the transaction with the latest timestamp. In our example, the change of product_name from soap to sapone on endpoint1 was made at t=1 and the change to product_name from soap to savon was made at t=2 on endpoint2, so pgactive will resolve the conflict on endpoint1 and changes product_name from sapone to savon because endpoint2 update is latest.

You can view all the conflicting transactions and how they were resolved in the pgactive.pgactive_conflict_history table, as shown in the following code:

```
SELECT * FROM pgactive.pgactive_conflict_history;

 -[ RECORD 1 ]---------------+------------------------------
conflict_id                 | 1
local_node_sysid            | 7254092437219470229
local_conflict_xid          | 0
local_conflict_lsn          | 0/1DCBEA8
local_conflict_time         | 2023-08-31 12:22:10.062739+00
object_schema               | inventory
object_name                 | products
remote_node_sysid           | 7254092429617437576
remote_txid                 | 738
remote_commit_time          | 2023-08-31 12:23:10.062739+00
remote_commit_lsn           | 0/1DCDBF0
conflict_type               | update_update
conflict_resolution         | last_update_wins_keep_remote
local_tuple                 | {"id":"20605a2f-f43a-47f9-bcb7-8fe200bc8143","product_name":"sapone", "created_at": "2023-08-31 12:22:10.062739+00"}
remote_tuple                | {"id":"20605a2f-f43a-47f9-bcb7-8fe200bc8143","product_name":"savon", "created_at": "2023-08-31 12:23:10.062739+00"}
local_tuple_xmin            | 
local_tuple_origin_sysid    | 7254092437219470229
error_message               | 
error_sqlstate              | 
error_querystring           | 
error_cursorpos             | 
error_detail                | 
error_hint                  | 
error_context               | 
error_columnname            | 
error_typename              | 
error_constraintname        | 
error_filename              | 
error_lineno                | 
error_funcname              | 
remote_node_timeline        | 1
remote_node_dboid           | 16385
local_tuple_origin_timeline | 1
local_tuple_origin_dboid    | 16385
local_commit_time           | 
```

Incase a updated tuple after the conflict resolution is not meeting the accuracy due to many factors, database administrator can manually update the tuple by running an update DML and fix the the value.

## Clean up

If you created PostgreSQL instances to run this example and no longer need to use them, you can delete them at this time. In this case, you don’t need to complete any of the following steps.

To remove a pgactive instance from an application set and prepare to drop the pgactive extension, you must first detach the node from the group. For example, to detach both nodes from the earlier example, you can run the following command:

```
SELECT pgactive.pgactive_detach_nodes(ARRAY['endpoint1-app', 'endpoint2-app']);
```

After you have detached the nodes, you can run the pgactive.pgactive_remove() command on each instance to disable pgactive:

```
SELECT pgactive.pgactive_remove();
``
After you have successfully run these commands, you can drop the pgactive extension:

```
DROP EXTENSION pgactive;
```
