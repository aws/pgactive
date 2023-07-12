-- Tests of DDL locking and state probes for simple 1-peer configurations with
-- other peer up. Can't do catchup write-mode tests with only two nodes.
--
-- (More complex tests will be done in TAP framework)

SELECT bdr.bdr_acquire_global_lock('');
SELECT bdr.bdr_acquire_global_lock(NULL);
SELECT bdr.bdr_acquire_global_lock('bogus');

BEGIN;
SET LOCAL bdr.skip_ddl_replication = true;
SELECT bdr.bdr_acquire_global_lock('ddl_lock');
ROLLBACK;

SELECT * FROM ddl_info;

-- Simple write lock
BEGIN;
SELECT * FROM ddl_info;
SELECT bdr.bdr_acquire_global_lock('write_lock');
SELECT * FROM ddl_info;
ROLLBACK;

SELECT * FROM ddl_info;

-- Simple ddl lock
BEGIN;
SELECT * FROM ddl_info;
SELECT bdr.bdr_acquire_global_lock('ddl_lock');
SELECT * FROM ddl_info;
COMMIT;

SELECT * FROM ddl_info;

-- Lock upgrade
BEGIN;
SELECT * FROM ddl_info;
SELECT bdr.bdr_acquire_global_lock('ddl_lock');
SELECT * FROM ddl_info;
SELECT bdr.bdr_acquire_global_lock('write_lock');
SELECT * FROM ddl_info;
ROLLBACK;

SELECT * FROM ddl_info;

-- Log upgrade in rollbacked subxact
BEGIN;
SELECT * FROM ddl_info;
SELECT bdr.bdr_acquire_global_lock('ddl_lock');
SAVEPOINT ddllock;
SELECT bdr.bdr_acquire_global_lock('write_lock');
SELECT * FROM ddl_info;
ROLLBACK TO SAVEPOINT ddllock;
-- We really should go back to 'ddl_lock' mode, but we actually
-- stay in 'write_lock' mode here. Even if the acquire of write
-- mode fails (or is not completed yet) above.
-- BUG 2ndQuadrant/bdr-private#77
SELECT * FROM ddl_info;
SELECT bdr.bdr_acquire_global_lock('write_lock');
SELECT * FROM ddl_info;
COMMIT;

SELECT * FROM ddl_info;
