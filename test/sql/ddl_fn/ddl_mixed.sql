-- test for RT-#37869
SELECT pgactive.pgactive_replicate_ddl_command($DDL$ 
CREATE TABLE public.add_column (
    id serial primary key,
    data text
);
$DDL$);

INSERT INTO add_column (data) SELECT generate_series(1,100,10);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$ ALTER TABLE public.add_column ADD COLUMN other varchar(100); $DDL$);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
SELECT id, data, other FROM add_column ORDER BY id;

UPDATE add_column SET other = 'foobar'; 

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
SELECT id, data, other FROM add_column ORDER BY id;

SELECT pgactive.pgactive_replicate_ddl_command($DDL$ 
DROP TABLE public.add_column
$DDL$);

-- We allow pgactive nodes sending out changes for postgres logical replication
-- subscribers.
SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE PUBLICATION mypub FOR ALL TABLES;
$DDL$);

-- We do not allow pgactive nodes receiving changes from postgres logical
-- replication publishers.
SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE SUBSCRIPTION mysub CONNECTION '' PUBLICATION mypub;
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
ALTER SUBSCRIPTION mysub REFRESH PUBLICATION;
$DDL$);

SELECT pgactive.pgactive_replicate_ddl_command($DDL$
DROP PUBLICATION mypub;
$DDL$);

-- We do not allow external logical replication extensions to be created when
-- pgactive is active.
SELECT pgactive.pgactive_replicate_ddl_command($DDL$
CREATE EXTENSION pglogical;
$DDL$);
