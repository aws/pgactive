-- test for RT-#37869

CREATE TABLE add_column (
    id serial primary key,
    data text
);

INSERT INTO add_column (data) SELECT generate_series(1,100,10);

ALTER TABLE add_column ADD COLUMN other varchar(100);

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c postgres
SELECT id, data, other FROM add_column ORDER BY id;

UPDATE add_column SET other = 'foobar';

SELECT pgactive.pgactive_wait_for_slots_confirmed_flush_lsn(NULL,NULL);
\c regression
SELECT id, data, other FROM add_column ORDER BY id;

DROP TABLE add_column;

-- We allow pgactive nodes sending out changes for postgres logical replication
-- subscribers.
CREATE PUBLICATION mypub FOR ALL TABLES;

-- We do not allow pgactive nodes receiving changes from postgres logical
-- replication publishers.
CREATE SUBSCRIPTION mysub CONNECTION '' PUBLICATION mypub;
ALTER SUBSCRIPTION mysub REFRESH PUBLICATION;

DROP PUBLICATION mypub;

-- We do not allow external logical replication extensions to be created when
-- pgactive is active.
CREATE EXTENSION pglogical;
