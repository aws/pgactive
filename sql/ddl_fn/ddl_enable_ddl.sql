SET bdr.skip_ddl_replication = true;
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.should_fail ( id integer ) $DDL$);

RESET bdr.skip_ddl_replication;
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.create_ok (id integer) $DDL$);

SET bdr.skip_ddl_replication = true;
SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER TABLE public.create_ok ADD COLUMN alter_should_fail text $DDL$);

RESET bdr.skip_ddl_replication;
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.create_ok $DDL$);

-- Now for the rest of the DDL tests, presume they're allowed,
-- otherwise they'll get pointlessly verbose.
SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER DATABASE regression RESET bdr.skip_ddl_replication $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER DATABASE postgres RESET bdr.skip_ddl_replication $DDL$);
