SET bdr.skip_ddl_replication = true;
CREATE TABLE should_ok ( id integer );

RESET bdr.skip_ddl_replication;
CREATE TABLE create_ok (id integer);

SET bdr.skip_ddl_replication = true;
ALTER TABLE create_ok ADD COLUMN alter_should_ok text;

RESET bdr.skip_ddl_replication;
DROP TABLE create_ok;

-- Now for the rest of the DDL tests, presume they're allowed,
-- otherwise they'll get pointlessly verbose.
ALTER DATABASE regression RESET bdr.skip_ddl_replication;
ALTER DATABASE postgres RESET bdr.skip_ddl_replication;
