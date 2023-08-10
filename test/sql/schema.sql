--
-- BDR tables' attributes must never change with schema changes.
-- Only new attributes can be appended and only if nullable.
--

select attrelid::regclass::text, attnum, attname, attisdropped, atttypid::regtype, attnotnull
from pg_attribute
where attrelid = ANY (ARRAY['bdr.bdr_nodes', 'bdr.bdr_connections', 'bdr.bdr_queued_drops', 'bdr.bdr_queued_commands', 'bdr.bdr_global_locks']::regclass[])
  and attnum >= 1
order by attrelid, attnum;
