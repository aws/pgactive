SELECT
  attnum, attname, attisdropped
FROM pg_catalog.pg_attribute
WHERE attrelid = 'bdr.bdr_nodes'::regclass
ORDER BY attnum;

SELECT
  attnum, attname, attisdropped
FROM pg_catalog.pg_attribute
WHERE attrelid = 'bdr.bdr_connections'::regclass
ORDER BY attnum;
