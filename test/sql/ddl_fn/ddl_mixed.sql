-- test for RT-#37869
SELECT bdr.bdr_replicate_ddl_command($DDL$ 
CREATE TABLE public.add_column (
    id serial primary key,
    data text
);
$DDL$);

INSERT INTO add_column (data) SELECT generate_series(1,100,10);

SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER TABLE public.add_column ADD COLUMN other varchar(100); $DDL$);

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
SELECT id, data, other FROM add_column ORDER BY id;

UPDATE add_column SET other = 'foobar'; 

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
SELECT id, data, other FROM add_column ORDER BY id;

SELECT bdr.bdr_replicate_ddl_command($DDL$ 
DROP TABLE public.add_column
$DDL$);

-- We allow BDR nodes sending out changes for postgres logical replication
-- subscribers.
SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE PUBLICATION mypub FOR ALL TABLES;
$DDL$);

-- We do not allow BDR nodes receiving changes from postgres logical
-- replication publishers.
SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE SUBSCRIPTION mysub CONNECTION '' PUBLICATION mypub;
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
ALTER SUBSCRIPTION mysub REFRESH PUBLICATION;
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
DROP PUBLICATION mypub;
$DDL$);

-- We do not allow external logical replication extensions to be created when
-- BDR is active.
SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE EXTENSION pglogical;
$DDL$);
