SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER DATABASE regression RESET bdr.skip_ddl_replication; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ ALTER DATABASE postgres RESET bdr.skip_ddl_replication; $DDL$);
