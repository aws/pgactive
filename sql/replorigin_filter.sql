SELECT * FROM public.bdr_regress_variables()
\gset

WITH funcnames(ro_pgmajor,ro_create,ro_session_setup,ro_xact_setup,ro_is_setup,ro_drop) AS (VALUES
(906, 'pg_replication_origin_create', 'pg_replication_origin_session_setup', 'pg_replication_origin_xact_setup',  'pg_replication_origin_session_is_setup', 'pg_replication_origin_drop'),
(904, 'pg_replication_identifier_create', 'pg_replication_identifier_setup_replaying_from', 'pg_replication_identifier_setup_tx_origin', 'pg_replication_identifier_is_replaying', 'pg_replication_identifier_drop'))
SELECT * FROM funcnames WHERE ro_pgmajor = (SELECT CASE WHEN current_setting('server_version_num')::int/100 = 904 THEN 904 ELSE 906 END) \gset

-- Because function names vary
\a \t

\c :writedb1

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE TABLE public.origin_filter (
   id integer primary key not null,
   n1 integer not null
);
$DDL$);

SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);

-- Simulate a write from some unknown peer node by defining a replication
-- origin and using it in our session. We must forward this write.

INSERT INTO origin_filter(id, n1) VALUES (1, 1);

SELECT :"ro_create" ('demo_origin');

SELECT :"ro_is_setup" ();

INSERT INTO origin_filter(id, n1) VALUES (2, 2);

SELECT :"ro_session_setup" ('demo_origin');

SELECT :"ro_is_setup" ();

INSERT INTO origin_filter(id, n1) VALUES (3, 3);

BEGIN;
SELECT :"ro_xact_setup" ('1/1', current_timestamp);
INSERT INTO public.origin_filter(id, n1) values (4, 4);
COMMIT;

SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);

SELECT * FROM origin_filter ORDER BY id;

\c :writedb2

SELECT :"ro_is_setup" ();

SELECT * FROM origin_filter ORDER BY id;

\c :writedb1

SELECT :"ro_is_setup" ();

SELECT bdr.bdr_replicate_ddl_command($DDL$
    DROP TABLE public.origin_filter;
$DDL$);

SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);

SELECT :"ro_drop" ('demo_origin');
