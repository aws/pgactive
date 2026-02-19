This troubleshooting guide addresses common issues, configuration errors, and operational challenges when using pgactive.

### 1. Installation & Setup Failures

#### Issue: `CREATE EXTENSION pgactive` fails

* Error: "pgactive must be loaded via shared_preload_libraries"
* Cause: The database parameter group hasn't been updated or the instance hasn't been rebooted.
* Fix:
1. Check your database configuration file.
2. Reboot the instance (applying parameters is not enough; a reboot is required to load the library).
3. Verify inside SQL: `SHOW shared_preload_libraries;` (should include `pgactive`).

#### Issue: `pgactive_create_group` or `pgactive_join_group` hangs or fails

* Error: Connection timeouts or "could not connect to server"
* Cause: Network connectivity issues between the instances.
* Fix:
* Ensure all instances allow inbound traffic on port `5432` from the *other* instance's.
* Ensure all instances are setup with correct authentication.
* Verification: Try creating a standard `postgres_fdw` connection manually between nodes to verify network reachability before using pgactive functions.

### 2. Replication Issues

#### Issue: Replication is Active but Data Isn't Syncing

* Cause: The table might be missing a Primary Key. `pgactive` relies on logical replication which strictly requires PKs to identify rows for updates/deletes.
* Fix:
1. Check the table definition: `\d your_table_name`
2. If no PK exists, `pgactive` essentially ignores updates for that table or fails silently depending on configuration.
3. Add a PK: `ALTER TABLE your_table ADD PRIMARY KEY (id);`

#### Issue: High Replication Lag

* Symptoms: Data written to Node A takes seconds/minutes to appear on Node B.
* Troubleshooting:
* Check Lag: Run this on the subscriber (destination) node:
```sql
SELECT subscription_name, 
       active, 
       pid,
       pg_size_pretty(pg_current_wal_lsn() - replay_lsn) AS replication_lag 
FROM pg_stat_subscription;

```

* Common Causes:
* Long Transactions: A long-running transaction on the publisher holds back the WAL stream.
* Network Latency: High latency between regions (e.g., us-east-1 to ap-northeast-1).
* No Primary Key: Updates to tables without PKs (if they somehow exist) cause full table scans on the subscriber, killing performance.

### 3. Conflict Resolution

`pgactive` uses a Last-Write-Wins strategy based on commit timestamps. If two nodes update the same row at the same time, the later timestamp overwrites the earlier one.

#### How to View Conflicts

Conflicts are not silent; they are logged.

* Query:
```sql
SELECT * FROM pgactive.pgactive_conflict_history 
ORDER BY conflict_time DESC;

```

* Analyze: Look at the `conflict_type` column.
* `update_missing`: Tried to update a row that doesn't exist (deleted on other node).
* `insert_exists`: Tried to insert a row that already exists (duplicate key).

#### Common Conflict Scenarios & Fixes

* Sequence Collisions: Using `serial` or `bigserial` on nodes will result in duplicate ID errors (`insert_exists`).
* Fix: Do not use standard sequences. Use `UUID` (v4 or v7) for primary keys. Alternatively, snowflake ID or use distinct ranges (Node A: 1-1B, Node B: 1B-2B), though UUIDs are preferred.

### 4. Schema Changes (DDL)

Critical Limitation: `pgactive` does not replicate DDL (Schema changes like `CREATE TABLE`, `ALTER TABLE`, `CREATE INDEX`).

#### Issue: "Relation does not exist" on Node B

* Cause: You created a table on Node A, but it didn't appear on Node B.
* Fix: You must manually run DDL on all nodes.
* Recommended Procedure:
1. Run `CREATE TABLE` on Node A.
2. Run `CREATE TABLE` on Node B.
3. Wait for them to be ready.
4. Data replication will then begin automatically for that table (if configured to replicate all tables).

#### Issue: Schema Mismatch Conflicts

* Cause: Node A has a new column `email`, but Node B does not. Inserting data into Node A fails to replicate to B.
* Fix: Always apply schema changes in a backward-compatible manner. Add the column to all nodes before your application starts writing to that column.

### 5. Essential Monitoring Queries

Check Node Status:

```sql
SELECT * FROM pgactive.pgactive_wait_for_node_ready();

```

Check Node and Replication Status:

```sql
SELECT *
FROM pgactive.pgactive_get_replication_lag_info();

```

*If `active` is `f` (false), the consumer is disconnected.*

*if `pending_wal_decoding` or `pending_wal_to_apply` values are constantly increasing, check the logs for error decoding or apply related errors.

### Summary of Anti-Patterns (Don't Do This)

* Don't use `pgactive` for load balancing writes (writing to both nodes for performance). It decreases performance due to conflict handling overhead and at the end the same amount of writes (as without pgactive) has to be done (because they will be replayed if not local). Use it for Global Availability (writing to the local region).
* Don't rely on `SERIAL` integers for Primary Keys.
* Don't forget to monitor `pgactive_wait_for_node_ready` and `pgactive_conflict_history`.
