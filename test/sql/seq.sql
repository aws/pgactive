\c regression

SELECT datname, node_seq_id
FROM bdr.bdr_nodes
INNER JOIN pg_database ON (node_dboid = pg_database.oid);

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE SEQUENCE public.dummy_seq;
$DDL$);

-- Generate enough values to wrap around the sequence bits twice.
-- If a machine can generate 16k sequence values per second it could
-- wrap. Force materialization to a tuplestore so we don't slow down
-- generation.
--
-- We should get no duplicates.
WITH vals(val) AS (
   SELECT bdr.bdr_snowflake_id_nextval('dummy_seq'::regclass)
   FROM generate_series(1, (2 ^ 14)::bigint * 2)
   OFFSET 0
)
SELECT val, 'duplicate'
FROM vals
GROUP BY val
HAVING count(val) > 1
UNION ALL
SELECT count(distinct VAL), 'ndistinct'
FROM vals;

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE SEQUENCE public.dummy_seq2;
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE TABLE public.seqvalues (id bigint);
$DDL$);

SELECT node_seq_id FROM bdr.bdr_nodes
WHERE (node_sysid, node_timeline, node_dboid) = bdr.bdr_get_local_nodeid();

-- Generate enough sequences to almost wrap by forcing
-- the same timestamp to be re-used.
INSERT INTO seqvalues(id)
SELECT bdr._bdr_snowflake_id_nextval_private('dummy_seq2'::regclass, '530605914245317'::bigint)
FROM generate_series(0, (2 ^ 14)::bigint - 2);

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE UNIQUE INDEX ON public.seqvalues(id);
$DDL$);

-- This should wrap around and fail. Since we're always running on the same
-- node with the same nodeid, and starting at the same initial sequence value,
-- it'll do so at the same value too.
INSERT INTO seqvalues(id)
SELECT bdr._bdr_snowflake_id_nextval_private('dummy_seq2'::regclass, '530605914245317'::bigint);

-- So we'll see the same stop-point
SELECT last_value FROM dummy_seq2;

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

\c postgres

-- We should be able to insert the same number of values on the other node
-- before wrapping, even if we're using the same timestamp-part.

SELECT node_seq_id FROM bdr.bdr_nodes
WHERE (node_sysid, node_timeline, node_dboid) = bdr.bdr_get_local_nodeid();

SELECT last_value FROM dummy_seq2;

INSERT INTO seqvalues(id)
SELECT bdr._bdr_snowflake_id_nextval_private('dummy_seq2'::regclass, '530605914245317'::bigint)
FROM generate_series(0, (2 ^ 14)::bigint - 2);

SELECT last_value FROM dummy_seq2;

SELECT count(id) FROM seqvalues;

SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL,NULL);

\c regression

SELECT count(id) FROM seqvalues;
