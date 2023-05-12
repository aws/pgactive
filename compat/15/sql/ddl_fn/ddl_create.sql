SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_tbl_simple_create(val int); $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_simple_create
\c postgres
\d+ test_tbl_simple_create

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_tbl_simple_create; $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_simple_create
\c regression
\d+ test_tbl_simple_create

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE UNLOGGED TABLE public.test_tbl_unlogged_create(val int); $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_unlogged_create
\c postgres
\d+ test_tbl_unlogged_create

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_tbl_unlogged_create; $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_unlogged_create
\c regression
\d+ test_tbl_unlogged_create

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_tbl_simple_pk(val int PRIMARY KEY); $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_simple_pk
\c postgres
\d+ test_tbl_simple_pk

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_tbl_simple_pk; $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_simple_pk
\c regression
\d+ test_tbl_simple_pk

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_tbl_combined_pk(val int, val1 int, PRIMARY KEY (val, val1)); $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_combined_pk
\c postgres
\d+ test_tbl_combined_pk

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_tbl_combined_pk; $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_combined_pk
\c regression
\d+ test_tbl_combined_pk

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_tbl_serial(val SERIAL); $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_serial
\c postgres
\d+ test_tbl_serial

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_tbl_serial; $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_serial
\c regression
\d+ test_tbl_serial

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_tbl_serial(val SERIAL); $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_serial
\c postgres
\d+ test_tbl_serial

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_tbl_serial_pk(val SERIAL PRIMARY KEY); $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_serial_pk
\c postgres
\d+ test_tbl_serial_pk

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_tbl_serial_pk; $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_serial_pk
\c regression
\d+ test_tbl_serial_pk

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_tbl_serial_combined_pk(val SERIAL, val1 INTEGER, PRIMARY KEY (val, val1)); $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_serial_combined_pk
\c postgres
\d+ test_tbl_serial_combined_pk

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_tbl_create_index (val int, val2 int); $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE UNIQUE INDEX test1_idx ON public.test_tbl_create_index(val); $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE INDEX test2_idx ON public.test_tbl_create_index (lower(val2::text)); $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_create_index
\c regression
\d+ test_tbl_create_index

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP INDEX public.test1_idx; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP INDEX public.test2_idx; $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_create_index
\c postgres
\d+ test_tbl_create_index

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE INDEX test1_idx ON public.test_tbl_create_index(val, val2); $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE INDEX test2_idx ON public.test_tbl_create_index USING gist (val, UPPER(val2::text)); $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_create_index
\c regression
\d+ test_tbl_create_index

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP INDEX public.test1_idx; $DDL$);
-- Not supported via bdr.bdr_replicate_ddl_command, see //github.com/2ndQuadrant/bdr-private/issues/124
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP INDEX CONCURRENTLY public.test2_idx; $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_create_index
\c postgres
\d+ test_tbl_create_index

-- Not supported via bdr.bdr_replicate_ddl_command, see //github.com/2ndQuadrant/bdr-private/issues/124
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE INDEX CONCURRENTLY test1_idx ON public.test_tbl_create_index(val, val2); $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE UNIQUE INDEX CONCURRENTLY test2_idx ON public.test_tbl_create_index (lower(val2::text)); $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_create_index
\c regression
\d+ test_tbl_create_index

-- Not supported via bdr.bdr_replicate_ddl_command, see //github.com/2ndQuadrant/bdr-private/issues/124
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP INDEX CONCURRENTLY public.test1_idx; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP INDEX CONCURRENTLY public.test2_idx; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_tbl_create_index; $DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_simple_create_with_arrays_tbl(val int[], val1 text[]); $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_simple_create_with_arrays_tbl
\c postgres
\d+ test_simple_create_with_arrays_tbl

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_simple_create_with_arrays_tbl; $DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TYPE public.test_t AS ENUM('a','b','c'); $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_simple_create_with_enums_tbl(val public.test_t, val1 public.test_t); $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_simple_create_with_enums_tbl
\c regression
\d+ test_simple_create_with_enums_tbl

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_simple_create_with_enums_tbl; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TYPE public.test_t; $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_simple_create_with_enums_tbl

\dT test_t
\c postgres
\d+ test_simple_create_with_enums_tbl

\dT test_t

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TYPE public.test_t AS (f1 text, f2 float, f3 integer); $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_simple_create_with_composites_tbl(val public.test_t, val1 public.test_t); $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_simple_create_with_composites_tbl
\c regression
\d+ test_simple_create_with_composites_tbl

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_simple_create_with_composites_tbl; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TYPE public.test_t; $DDL$);
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_simple_create_with_composites_tbl

\dT test_t
\c postgres
\d+ test_simple_create_with_composites_tbl

\dT test_t

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_tbl_serial; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_tbl_serial_combined_pk; $DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_tbl_inh_parent(f1 text, f2 date DEFAULT '2014-01-02'); $DDL$);
\set VERBOSITY terse
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_tbl_inh_chld1(f1 text, f2 date DEFAULT '2014-01-02') INHERITS (public.test_tbl_inh_parent); $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_tbl_inh_chld2(f1 text, f2 date) INHERITS (public.test_tbl_inh_parent); $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.test_tbl_inh_chld3(f1 text) INHERITS (public.test_tbl_inh_parent, public.test_tbl_inh_chld1); $DDL$);
\set VERBOSITY default

SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_inh_*
\c regression
\d+ test_tbl_inh_*

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE RULE test_tbl_inh_parent_rule_ins_1 AS ON INSERT TO public.test_tbl_inh_parent
          WHERE (f1 LIKE '%1%') DO INSTEAD
          INSERT INTO public.test_tbl_inh_chld1 VALUES (NEW.*);
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE RULE test_tbl_inh_parent_rule_ins_2 AS ON INSERT TO public.test_tbl_inh_parent
          WHERE (f1 LIKE '%2%') DO INSTEAD
          INSERT INTO public.test_tbl_inh_chld2 VALUES (NEW.*);
$DDL$);

SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_inh_parent
\c postgres
\d+ test_tbl_inh_parent

SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_tbl_inh_chld1; $DDL$);
\set VERBOSITY terse
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.test_tbl_inh_parent CASCADE; $DDL$);
\set VERBOSITY default

SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_inh_*
\c regression
\d+ test_tbl_inh_*

CREATE TABLE test_tbl_exclude(val int PRIMARY KEY,EXCLUDE USING gist(id with =));
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\d+ test_tbl_exclude
\c postgres
\d+ test_tbl_exclude
\c regression

-- ensure tables WITH OIDs can't be created
SHOW default_with_oids;
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.tbl_with_oids() WITH oids; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.tbl_without_oids() WITHOUT oids; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.tbl_without_oids; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.tbl_without_oids(); $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.tbl_without_oids; $DDL$);
SET default_with_oids = true;
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.tbl_with_oids(); $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.tbl_with_oids() WITH OIDS; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.tbl_without_oids() WITHOUT oids; $DDL$);
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE public.tbl_without_oids; $DDL$);
SET default_with_oids = false;

-- ensure storage attributes in SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.are replicated properly $DDL$);
\c postgres
SELECT bdr.bdr_replicate_ddl_command($DDL$ CREATE TABLE public.tbl_showfillfactor (name char(500), unique (name) with (fillfactor=65)) with (fillfactor=75); $DDL$);
\d+ tbl_showfillfactor
SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\c regression
\d+ tbl_showfillfactor
\set VERBOSITY terse
SELECT bdr.bdr_replicate_ddl_command($DDL$ DROP TABLE tbl_showfillfactor;$DDL$);
\set VERBOSITY default

--- AGGREGATE ---
\c postgres
SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE AGGREGATE public.test_avg (
   sfunc = int4_avg_accum, basetype = int4, stype = _int8,
   finalfunc = int8_avg,
   initcond1 = '{0,0}',
   sortop = =
);
$DDL$);

-- without finalfunc; test obsolete spellings 'sfunc1' etc
SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE AGGREGATE public.test_sum (
   sfunc1 = int4pl, basetype = int4, stype1 = int4,
   initcond1 = '0'
);
$DDL$);

-- zero-argument aggregate
SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE AGGREGATE public.test_cnt (*) (
   sfunc = int8inc, stype = int8,
   initcond = '0'
);
$DDL$);

SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\dfa test_*
\c regression
\dfa test_*


SELECT bdr.bdr_replicate_ddl_command($DDL$
DROP AGGREGATE public.test_avg(int4);
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
DROP AGGREGATE public.test_sum(int4);
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
DROP AGGREGATE public.test_cnt(*);
$DDL$);

SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\dfa test_*
\c postgres
\dfa test_*

SELECT bdr.bdr_replicate_ddl_command($DDL$
create type public.aggtype as (a integer, b integer, c text);
$DDL$);


SELECT bdr.bdr_replicate_ddl_command($DDL$
create function public.aggf_trans(public.aggtype[],integer,integer,text) returns public.aggtype[]
as 'select array_append($1,ROW($2,$3,$4)::public.aggtype)'
language sql strict immutable;
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
create function public.aggfns_trans(public.aggtype[],integer,integer,text) returns public.aggtype[]
as 'select array_append($1,ROW($2,$3,$4)::public.aggtype)'
language sql immutable;
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
create aggregate public.test_aggfstr(integer,integer,text) (
   sfunc = public.aggf_trans, stype = public.aggtype[],
   initcond = '{}'
);
$DDL$);

SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\dfa test_*
\c regression
\dfa test_*

SELECT bdr.bdr_replicate_ddl_command($DDL$
DROP AGGREGATE public.test_aggfstr(integer,integer,text);
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
DROP FUNCTION public.aggf_trans(public.aggtype[],integer,integer,text);
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
DROP FUNCTION public.aggfns_trans(public.aggtype[],integer,integer,text);
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
DROP TYPE public.aggtype;
$DDL$);

SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\dfa test_*
\c postgres
\dfa test_*

--- OPERATOR ---
\c postgres

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE OPERATOR public.## (
   leftarg = path,
   rightarg = path,
   procedure = path_inter,
   commutator = OPERATOR(public.##)
);
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE OPERATOR public.@#@ (
   rightarg = int8,		-- left unary
   procedure = factorial
);
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
CREATE OPERATOR public.#@# (
   leftarg = int8,		-- right unary
   procedure = factorial
);
$DDL$);

SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\do public.##
\do public.@#@
\do public.#@#
\c regression
\do public.##
\do public.@#@
\do public.#@#

SELECT bdr.bdr_replicate_ddl_command($DDL$
DROP OPERATOR public.##(path, path);
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
DROP OPERATOR public.@#@(none,int8);
$DDL$);

SELECT bdr.bdr_replicate_ddl_command($DDL$
DROP OPERATOR public.#@#(int8,none);
$DDL$);

\do public.##
\do public.@#@
\do public.#@#

SELECT bdr.wait_slot_confirm_lsn(NULL,NULL);
\c postgres
\do public.##
\do public.@#@
\do public.#@#
