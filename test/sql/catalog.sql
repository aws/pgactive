SELECT
  attnum, attname, attisdropped
FROM pg_catalog.pg_attribute
WHERE attrelid = 'pgactive.pgactive_nodes'::regclass
ORDER BY attnum;

SELECT
  attnum, attname, attisdropped
FROM pg_catalog.pg_attribute
WHERE attrelid = 'pgactive.pgactive_connections'::regclass
ORDER BY attnum;
