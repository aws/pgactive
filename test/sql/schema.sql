--
-- pgactive tables' attributes must never change with schema changes.
-- Only new attributes can be appended and only if nullable.
--

select attrelid::regclass::text, attnum, attname, attisdropped, atttypid::regtype, attnotnull
from pg_attribute
where attrelid = ANY (ARRAY['pgactive.pgactive_nodes', 'pgactive.pgactive_connections', 'pgactive.pgactive_queued_drops', 'pgactive.pgactive_queued_commands', 'pgactive.pgactive_global_locks']::regclass[])
  and attnum >= 1
order by attrelid, attnum;
