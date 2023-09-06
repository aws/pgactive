SELECT pgactive.pgactive_replicate_ddl_command($DDL$ ALTER DATABASE regression RESET pgactive.skip_ddl_replication; $DDL$);
SELECT pgactive.pgactive_replicate_ddl_command($DDL$ ALTER DATABASE postgres RESET pgactive.skip_ddl_replication; $DDL$);
