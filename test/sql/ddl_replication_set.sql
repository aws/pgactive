/* First test whether a table's replication set can be properly manipulated */
\c postgres
CREATE SCHEMA normalschema;
CREATE SCHEMA "strange.schema-IS";
CREATE TABLE switcheroo(id serial primary key, data text);
CREATE TABLE normalschema.sometbl_normalschema();
CREATE TABLE "strange.schema-IS".sometbl_strangeschema();

-- show initial replication sets
SELECT * FROM pgactive.pgactive_get_table_replication_sets('switcheroo');
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
SELECT * FROM pgactive.pgactive_get_table_replication_sets('switcheroo');

\c postgres
-- empty replication set (just 'all')
SELECT pgactive.pgactive_set_table_replication_sets('switcheroo', '{}');
SELECT * FROM pgactive.pgactive_get_table_replication_sets('switcheroo');
SELECT * FROM pgactive.pgactive_get_table_replication_sets('normalschema.sometbl_normalschema');
SELECT * FROM pgactive.pgactive_get_table_replication_sets('"strange.schema-IS".sometbl_strangeschema');
-- configure a couple
SELECT pgactive.pgactive_set_table_replication_sets('switcheroo', '{fascinating, is-it-not}');
SELECT * FROM pgactive.pgactive_set_table_replication_sets('normalschema.sometbl_normalschema', '{a}');
SELECT * FROM pgactive.pgactive_set_table_replication_sets('"strange.schema-IS".sometbl_strangeschema', '{a}');

SELECT * FROM pgactive.pgactive_get_table_replication_sets('switcheroo');
SELECT * FROM pgactive.pgactive_get_table_replication_sets('normalschema.sometbl_normalschema');
SELECT * FROM pgactive.pgactive_get_table_replication_sets('"strange.schema-IS".sometbl_strangeschema');

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
SELECT * FROM pgactive.pgactive_get_table_replication_sets('switcheroo');
SELECT * FROM pgactive.pgactive_get_table_replication_sets('normalschema.sometbl_normalschema');
SELECT * FROM pgactive.pgactive_get_table_replication_sets('"strange.schema-IS".sometbl_strangeschema');

\c postgres
-- make sure we can reset replication sets to the default again
-- configure a couple
SELECT pgactive.pgactive_set_table_replication_sets('switcheroo', NULL);
SELECT * FROM pgactive.pgactive_get_table_replication_sets('switcheroo');
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
SELECT * FROM pgactive.pgactive_get_table_replication_sets('switcheroo');

\c postgres
-- make sure reserved names can't be set
SELECT pgactive.pgactive_set_table_replication_sets('switcheroo', '{default,blubber}');
SELECT pgactive.pgactive_set_table_replication_sets('switcheroo', '{blubber,default}');
SELECT pgactive.pgactive_set_table_replication_sets('switcheroo', '{frakbar,all}');
--invalid characters
SELECT pgactive.pgactive_set_table_replication_sets('switcheroo', '{///}');
--too short/long
SELECT pgactive.pgactive_set_table_replication_sets('switcheroo', '{""}');
SELECT pgactive.pgactive_set_table_replication_sets('switcheroo', '{12345678901234567890123456789012345678901234567890123456789012345678901234567890}');

\c postgres
DROP TABLE switcheroo;
DROP TABLE normalschema.sometbl_normalschema;
DROP TABLE "strange.schema-IS".sometbl_strangeschema;

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

/*
 * Now test whether sets properly control the replication of data.
 *
 * node1: dbname regression, sets: default, important, for-node-1
 * node2: dbname postgres, sets: default, important, for-node-2
 */
\c postgres

CREATE TABLE settest_1(data text primary key);

INSERT INTO settest_1(data) VALUES ('should-replicate-via-default');

SELECT pgactive.pgactive_set_table_replication_sets('settest_1', '{}');
INSERT INTO settest_1(data) VALUES ('should-no-replicate-no-sets');

SELECT pgactive.pgactive_set_table_replication_sets('settest_1', '{unknown-set}');
INSERT INTO settest_1(data) VALUES ('should-no-replicate-unknown-set');

SELECT pgactive.pgactive_set_table_replication_sets('settest_1', '{for-node-2}');
INSERT INTO settest_1(data) VALUES ('should-replicate-via-for-node-2');

SELECT pgactive.pgactive_set_table_replication_sets('settest_1', '{}');
INSERT INTO settest_1(data) VALUES ('should-not-replicate-empty-again');

SELECT pgactive.pgactive_set_table_replication_sets('settest_1', '{unknown-set,for-node-2}');
INSERT INTO settest_1(data) VALUES ('should-replicate-via-for-node-2-even-though-unknown');

SELECT pgactive.pgactive_set_table_replication_sets('settest_1', '{unknown-set,important,for-node-2}');
INSERT INTO settest_1(data) VALUES ('should-replicate-via-for-node-2-and-important');

SELECT * FROM settest_1 ORDER BY data;
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
SELECT * FROM settest_1 ORDER BY data;
\c postgres
DROP TABLE settest_1;


/*
 * Now test configurations where only some actions are replicated.
 */
CREATE TABLE settest_2(data text primary key);

-- Test 1: ensure that inserts are replicated while update/delete are filtered
SELECT pgactive.pgactive_set_table_replication_sets('settest_2', '{for-node-2-insert}');
INSERT INTO pgactive.pgactive_replication_set_config(set_name, replicate_inserts, replicate_updates, replicate_deletes)
VALUES ('for-node-2-insert', true, false, false),
       ('for-node-2-update', false, true, false),
       ('for-node-2-delete', false, false, true);

INSERT INTO settest_2(data) VALUES ('repl-insert--insert-#1');
INSERT INTO settest_2(data) VALUES ('repl-insert--insert-#2-then-update');
INSERT INTO settest_2(data) VALUES ('repl-insert--insert-#3-then-delete');
UPDATE settest_2
SET data = 'repl-insert--insert-#2-update'
WHERE data = 'repl-insert--insert-#2-then-update';

DELETE FROM settest_2
WHERE data = 'repl-insert--insert-#3-then-delete';

-- Test 2: ensure that updates are replicated while inserts/deletes are filtered
-- insert before filtering
INSERT INTO settest_2(data) VALUES ('repl-update--insert-#1-then-update');
INSERT INTO settest_2(data) VALUES ('repl-update--insert-#2-then-delete');

SELECT pgactive.pgactive_set_table_replication_sets('settest_2', '{for-node-2-update}');

UPDATE settest_2
SET data = 'repl-update--insert-#1-update'
WHERE data = 'repl-update--insert-#1-then-update';

DELETE FROM settest_2
WHERE data = 'repl-update--insert-#2-then-delete';

INSERT INTO settest_2(data) VALUES ('repl-update--insert-#3');


-- verify that changing the pg_replication_sets row has effects
UPDATE pgactive.pgactive_replication_set_config
SET replicate_inserts = true
WHERE set_name = 'for-node-2-update';
INSERT INTO settest_2(data) VALUES ('repl-update--insert-#4');

-- but reset to normal meaning afterwards
UPDATE pgactive.pgactive_replication_set_config
SET replicate_inserts = false
WHERE set_name = 'for-node-2-update';


-- Test 3: ensure that deletes are replicated while inserts/updates are filtered
-- insert before filtering
SELECT pgactive.pgactive_set_table_replication_sets('settest_2', NULL);
INSERT INTO settest_2(data) VALUES ('repl-delete--insert-#1-then-update');
INSERT INTO settest_2(data) VALUES ('repl-delete--insert-#2-then-delete');
SELECT pgactive.pgactive_set_table_replication_sets('settest_2', '{for-node-2-delete}');

UPDATE settest_2
SET data = 'repl-delete--insert-#1-update'
WHERE data = 'repl-delete--insert-#1-then-update';

DELETE FROM settest_2
WHERE data = 'repl-delete--insert-#2-then-delete';

INSERT INTO settest_2(data) VALUES ('repl-delete--insert-#3');


-- Test 4: ensure that all partial sets together replicate everything
SELECT pgactive.pgactive_set_table_replication_sets('settest_2',
    '{for-node-2-insert,for-node-2-update,for-node-2-delete}');
INSERT INTO settest_2(data) VALUES ('repl-combined--insert-#1-then-update');
INSERT INTO settest_2(data) VALUES ('repl-combined--insert-#2-then-delete');

UPDATE settest_2
SET data = 'repl-combined--insert-#1-update'
WHERE data = 'repl-combined--insert-#1-then-update';

DELETE FROM settest_2
WHERE data = 'repl-combined--insert-#2-then-delete';

SELECT * FROM settest_2 ORDER BY data;
SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
SELECT * FROM settest_2 ORDER BY data;

-- Truncate required as execution on pgactive2.0/9.4 
-- gives 'violates unique constraint' during ddl_96/ddl_replication_set
\c postgres
TRUNCATE pgactive.pgactive_replication_set_config;
DROP TABLE settest_2;
DROP SCHEMA normalschema;
DROP SCHEMA "strange.schema-IS";
