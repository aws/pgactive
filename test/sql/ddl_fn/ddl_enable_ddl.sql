SET pgactive.skip_ddl_replication = true;
SELECT pgactive.pgactive_replicate_ddl_command($DDL$ CREATE TABLE public.should_fail ( id integer ) $DDL$);

RESET pgactive.skip_ddl_replication;
SELECT pgactive.pgactive_replicate_ddl_command($DDL$ CREATE TABLE public.create_ok (id integer) $DDL$);

SET pgactive.skip_ddl_replication = true;
SELECT pgactive.pgactive_replicate_ddl_command($DDL$ ALTER TABLE public.create_ok ADD COLUMN alter_should_fail text $DDL$);

RESET pgactive.skip_ddl_replication;
SELECT pgactive.pgactive_replicate_ddl_command($DDL$ DROP TABLE public.create_ok $DDL$);

-- Now for the rest of the DDL tests, presume they're allowed,
-- otherwise they'll get pointlessly verbose.
SELECT pgactive.pgactive_replicate_ddl_command($DDL$ ALTER DATABASE regression RESET pgactive.skip_ddl_replication $DDL$);
SELECT pgactive.pgactive_replicate_ddl_command($DDL$ ALTER DATABASE postgres RESET pgactive.skip_ddl_replication $DDL$);
