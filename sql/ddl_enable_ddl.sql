SET bdr.permit_ddl_locking = false;
CREATE TABLE should_fail ( id integer );

RESET bdr.permit_ddl_locking;
CREATE TABLE create_ok (id integer);

SET bdr.permit_ddl_locking = false;
ALTER TABLE create_ok ADD COLUMN alter_should_fail text;

RESET bdr.permit_ddl_locking;
DROP TABLE create_ok;

-- Now for the rest of the DDL tests, presume they're allowed,
-- otherwise they'll get pointlessly verbose.
ALTER DATABASE regression RESET bdr.permit_ddl_locking;
ALTER DATABASE postgres RESET bdr.permit_ddl_locking;
