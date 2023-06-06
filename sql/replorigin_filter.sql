SELECT * FROM public.bdr_regress_variables()
\gset

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

SELECT pg_replication_origin_create('demo_origin');

SELECT pg_replication_origin_session_is_setup();

INSERT INTO origin_filter(id, n1) VALUES (2, 2);

SELECT pg_replication_origin_session_setup('demo_origin');

SELECT pg_replication_origin_session_is_setup();

INSERT INTO origin_filter(id, n1) VALUES (3, 3);

BEGIN;
SELECT pg_replication_origin_xact_setup('1/1', current_timestamp);
INSERT INTO public.origin_filter(id, n1) values (4, 4);
COMMIT;

SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);

SELECT * FROM origin_filter ORDER BY id;

\c :writedb2

SELECT pg_replication_origin_session_is_setup();

SELECT * FROM origin_filter ORDER BY id;

\c :writedb1

SELECT pg_replication_origin_session_is_setup();

SELECT bdr.bdr_replicate_ddl_command($DDL$
    DROP TABLE public.origin_filter;
$DDL$);

SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);

SELECT pg_replication_origin_drop('demo_origin');
