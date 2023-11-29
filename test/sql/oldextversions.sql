-- Test old extension version entry points.
CREATE EXTENSION pgactive WITH VERSION '2.1.0';

-- List what version 2.1.0 contains.
\dx+ pgactive

SET pgactive.skip_ddl_replication = true;

-- Move to new version 2.1.1.
ALTER EXTENSION pgactive UPDATE TO '2.1.1';

-- List what version 2.1.1 contains.
\dx+ pgactive
