#!/usr/bin/env bash

# bdr_pgbench.sh is a sample script illustrating how pgbench can be run with
# BDR. It is intended for development and testing purposes, not for production
# uses. Idea is to help developers to automate pgbench + BDR for testing
# features or measuring performance etc. Note that bdr_pgbench.sh needs both
# postgres and BDR source code trees to be present. It also has many other nuts
# and bolts to tune. Therefore, it is highly recommended to closely look at the
# script, understand it and change it to taste.

# Usage:
# sh bdr_pgbench.sh PGSRC=/path/to/postgres/source/code BDRSRC=/path/to/bdr/source/code RESULTS=/path/to/bdr_pgbench.sh/results/

set -e -u

for arg in "$@"; do
eval "$arg"
done

# Check if the variables are set and print their values
if [ -z "$PGSRC" ]; then
	echo "PGSRC is not set"
	exit 1
else
	echo "Value of PGSRC is $PGSRC"
fi

if [ -z "$BDRSRC" ]; then
	echo "BDRSRC is not set"
	exit 1
else
	echo "Value of BDRSRC is $BDRSRC"
fi

if [ -z "$RESULTS" ]; then
	echo "RESULTS is not set"
	exit 1
else
	echo "Value of RESULTS is $RESULTS"
	rm -rf $RESULTS
	mkdir -p $RESULTS
fi

PGBIN=$PGSRC/inst/bin

# Clean any remains of previous runs
rm -rf $PGSRC/inst

# First node in BDR group
WHALE=whale
WHALE_H=localhost
WHALE_P=7432
WHALE_DB=$WHALE
WHALE_SC=$WHALE

# Second node in BDR group
PANDA=panda
PANDA_H=localhost
PANDA_P=7432
PANDA_DB=$PANDA
PANDA_SC=$PANDA

# pgbench configuration
SCALE=1
CLIENTS=10
RUNMODE=parallel
RUNTIME=1

# Build pg source code
echo "Building pg source code at $PGSRC"
cd $PGSRC
sh configure --prefix=$PWD/inst/ CFLAGS="-O2" > $RESULTS/install.log && make -j8 install > $RESULTS/install.log 2>&1

# Install contrib modules required for BDR
make -C contrib/btree_gist install
make -C contrib/cube install
make -C contrib/hstore install
make -C contrib/pg_trgm install

# Initialize pg instance
$PGBIN/initdb -D $PGBIN/data

# Build BDR source code
echo "Building BDR source code at $BDRSRC"
cd $BDRSRC
PATH=$PGBIN:$PATH ./configure
make -j8 install
cd $PGSRC

# BDR-cize pg instance
cat << EOF >> $PGBIN/data/postgresql.conf
shared_preload_libraries = 'bdr'
track_commit_timestamp = on
wal_level = 'logical'
port=7432
bdr.skip_ddl_replication = false
EOF

# Start pg instance
$PGBIN/pg_ctl -D $PGBIN/data -l $RESULTS/server.log start

# BDR-cize nodes
echo "BDR-cizing node $WHALE"
$PGBIN/psql -h $WHALE_H -p $WHALE_P postgres -c "CREATE DATABASE $WHALE_DB" >> $RESULTS/check.log 2>&1
$PGBIN/psql -h $WHALE_H -p $WHALE_P $WHALE_DB -c "CREATE EXTENSION bdr CASCADE" >> $RESULTS/check.log 2>&1
$PGBIN/psql -h $WHALE_H -p $WHALE_P $WHALE_DB -c "SELECT bdr.bdr_create_group(local_node_name := '$WHALE', node_external_dsn := 'dbname=$WHALE_DB host=$WHALE_H port=$WHALE_P')" >> $RESULTS/check.log 2>&1
$PGBIN/psql -h $WHALE_H -p $WHALE_P $WHALE_DB -c "SELECT bdr.bdr_wait_for_node_ready()" >> $RESULTS/check.log 2>&1

echo "BDR-cizing node $PANDA"
$PGBIN/psql -h $PANDA_H -p $PANDA_P postgres -c "CREATE DATABASE $PANDA_DB" >> $RESULTS/check.log 2>&1
$PGBIN/psql -h $PANDA_H -p $PANDA_P $PANDA_DB -c "CREATE EXTENSION bdr CASCADE" >> $RESULTS/check.log 2>&1
$PGBIN/psql -h $PANDA_H -p $PANDA_P $PANDA_DB -c "SELECT bdr.bdr_join_group(local_node_name := '$PANDA', node_external_dsn := 'dbname=$PANDA_DB host=$PANDA_H port=$PANDA_P', join_using_dsn := 'dbname=$WHALE_DB host=$WHALE_H port=$WHALE_P')" >> $RESULTS/check.log 2>&1
$PGBIN/psql -h $PANDA_H -p $PANDA_P $PANDA_DB -c "SELECT bdr.bdr_wait_for_node_ready()" >> $RESULTS/check.log 2>&1

# Initialize pgbench
echo "Setting up pgbench on node $WHALE"
$PGBIN/psql -h $WHALE_H -p $WHALE_P $WHALE_DB -c "CREATE SCHEMA $WHALE_SC; ALTER DATABASE $WHALE_DB SET search_path=$WHALE_SC,pg_catalog;" >> $RESULTS/check.log 2>&1
$PGBIN/pgbench  -q -i -s $SCALE -h $WHALE_H -p $WHALE_P $WHALE_DB  >> $RESULTS/check.log 2>&1
$PGBIN/psql -h $WHALE_H -p $WHALE_P $WHALE_DB -c "SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL, NULL);" >> $RESULTS/check.log 2>&1
if [ "$RUNMODE" = "parallel" ]; then
    echo "Setting up pgbench on node $PANDA"
	$PGBIN/psql -h $PANDA_H -p $PANDA_P $PANDA_DB -c "CREATE SCHEMA $PANDA_SC; ALTER DATABASE $PANDA_DB SET search_path=$PANDA_SC,pg_catalog;" >> $RESULTS/check.log 2>&1
	$PGBIN/pgbench -q -i -s $SCALE -h $PANDA_H -p $PANDA_P $PANDA_DB >> $RESULTS/check.log 2>&1
	$PGBIN/psql -h $PANDA_H -p $PANDA_P $PANDA_DB -c "SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL, NULL);" >> $RESULTS/check.log 2>&1
fi

# Run pgbench
echo "Running pgbench for duration $(date -u -d @$RUNTIME +%H:%M:%S) (HH:MM:SS)"
if [ "$RUNMODE" = "zigzag" ]; then
	NUM_RUNS=10
	RUNTIME=$(($RUNTIME/$NUM_RUNS))
else
	NUM_RUNS=1
fi

for i in `seq 1 $NUM_RUNS`; do
	if [ $(($i%2)) -eq 1 ]; then
		$PGBIN/pgbench -n -T $RUNTIME -j $CLIENTS -c $CLIENTS -h $WHALE_H -p $WHALE_P $WHALE_DB >> $RESULTS/check.log 2>&1 &
		WHALE_B_PID=$!
	fi
	if [ $(($i%2)) -eq 0 ] || [ "$RUNMODE" = "parallel" ]; then
		$PGBIN/pgbench -n -T $RUNTIME -j $CLIENTS -c $CLIENTS -h $PANDA_H -p $PANDA_P $PANDA_DB >> $RESULTS/check.log 2>&1 &
		PANDA_B_PID=$!
	fi

	# Wait for pgbench instance(s) to finish
	while kill -0 $WHALE_B_PID 2>>/dev/null || ( [ -n "$PANDA_B_PID" ] && kill -0 $PANDA_B_PID 2>>/dev/null ) ; do
		sleep 1
	done
done

$PGBIN/psql -h $WHALE_H -p $WHALE_P $WHALE_DB -c "SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL, NULL);" >> $RESULTS/check.log 2>&1
$PGBIN/psql -h $PANDA_H -p $PANDA_P $PANDA_DB -c "SELECT bdr.bdr_wait_for_slots_confirmed_flush_lsn(NULL, NULL);" >> $RESULTS/check.log 2>&1

# Part a node away from BDR group, to hit data differ error.
#$PGBIN/psql -h $PANDA_H -p $PANDA_P $PANDA_DB -c "SELECT bdr.bdr_detach_nodes(ARRAY['$WHALE']);" >> $RESULTS/check.log 2>&1
#$PGBIN/psql -h $WHALE_H -p $WHALE_P $WHALE_DB -c "CREATE TABLE foo (elefanto int);" >> $RESULTS/check.log 2>&1
#$PGBIN/psql -h $WHALE_H -p $WHALE_P $WHALE_DB -c "INSERT INTO foo SELECT * FROM generate_series(1, 100);" >> $RESULTS/check.log 2>&1

SQL=$(cat <<EOF
SET search_path=pg_catalog;
DO \$\$
DECLARE
    relid oid;
    cnt bigint;
    hsh bigint;
BEGIN
	FOR relid IN SELECT t.relid FROM pg_stat_user_tables t WHERE schemaname NOT IN ('bdr') ORDER BY schemaname, relname
    LOOP
        EXECUTE 'SELECT count(*), sum(hashtext((t.*)::text)) FROM ' || relid::regclass::text || ' t' INTO cnt, hsh;
        RAISE NOTICE '%: %, %', relid::regclass::text, cnt, hsh;
    END LOOP;
END;\$\$;
EOF
)

$PGBIN/psql -h $WHALE_H -p $WHALE_P $WHALE_DB -c "$SQL" > $RESULTS/whale.chksum 2>&1
$PGBIN/psql -h $PANDA_H -p $PANDA_P $PANDA_DB -c "$SQL" > $RESULTS/panda.chksum 2>&1

echo "pgbench finished, cleaning up"

# Stop pg instance
$PGBIN/pg_ctl -D $PGBIN/data -l $RESULTS/server.log stop

echo "Comparing data on node $WHALE and node $PANDA"
diff -c $RESULTS/whale.chksum $RESULTS/panda.chksum > $RESULTS/chksum.diff
status=$?
if [ $status -eq 0 ]; then
    echo "Data on node $WHALE and node $PANDA is same"
elif [ $status -eq 1 ]; then
	echo "Data on node $WHALE and node $PANDA is not same, check $RESULTS/chksum.diff"
    exit 1
elif [ $status -eq 2 ]; then
	echo "Error in executing diff command"
	exit 1
fi

echo "Check $RESULTS for more details"

# You may want the results to stay for analysis.

# Get back to BDR source directory
cd $BDRSRC
