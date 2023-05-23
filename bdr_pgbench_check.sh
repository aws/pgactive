#!/usr/bin/env bash

#CONFIG
DATADIR=./tmp_check
SCALE=4
CLIENTS=4

RUNMODE="$BDR_PGBENCH_RUNMODE"
if [ ! -n "$RUNMODE" ]; then
    RUNMODE="parallel"
fi

RUNTIME="$BDR_PGBENCH_RUNTIME"
if [ ! -n "$RUNTIME" ]; then
    RUNTIME=100
fi

#INTERNAL
TOPBUILDDIR=@top_srcdir@
BINDIR=@bindir@
LIBDIR=@libdir@
MAKE=@MAKE@
PGCONF=bdr_pgbench.conf
HBACONF=pg_hba.conf
PRIMARY_HOST=localhost
PRIMARY_PORT=7432
PRIMARY_DB=bdr_pgbench
SECONDARY_HOST=localhost
SECONDARY_DB=bdr_pgbench2
SECONDARY_PORT=7432
SCRIPTDIR="$( cd "$(dirname "$0")" ; pwd -P )"

convertsecs () {
	((h=${1}/3600))
	((m=(${1}%3600)/60))
	((s=${1}%60))
	printf "%02d:%02d:%02d\n" $h $m $s
}

# get full paths
mkdir -p $DATADIR
rm -r $DATADIR/*
cd $DATADIR
DATADIR=`pwd -P`

BINDIR=$DATADIR/install/$BINDIR
LIBDIR=$DATADIR/install/$LIBDIR

cd $SCRIPTDIR

cd $TOPBUILDDIR
TOPBUILDDIR=`pwd -P`

echo >$SCRIPTDIR/bdr_pgbench_check.log 2>&1
on_exit() {
	$BINDIR/pg_ctl -D $DATADIR/data stop -w -mfast >>$SCRIPTDIR/bdr_pgbench_check.log 2>&1
	echo "Error occured, check $SCRIPTDIR/bdr_pgbench_check.log for more info"
	exit 1
}
trap 'on_exit' ERR


# install pg and contrib
echo "Installing Postgres"
cd $TOPBUILDDIR
$MAKE DESTDIR="$DATADIR/install" install >>$SCRIPTDIR/bdr_pgbench_check.log 2>&1
echo "Installing Postgres contrib modules"
cd contrib
$MAKE DESTDIR="$DATADIR/install" install >>$SCRIPTDIR/bdr_pgbench_check.log 2>&1

# setup environment
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$LIBDIR
DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH:$LIBDIR
LIBPATH=$LIBPATH:$LIBDIR

# create and start pg instance
echo "Initializing Postgres instance"
cd $SCRIPTDIR
$BINDIR/initdb -D $DATADIR/data >>$SCRIPTDIR/bdr_pgbench_check.log 2>&1
cp $PGCONF $DATADIR/data/postgresql.conf
cp $HBACONF $DATADIR/data/pg_hba.conf

$BINDIR/pg_ctl -D $DATADIR/data start -w -l $DATADIR/bdr_pgbench_check_pg.log >>$SCRIPTDIR/bdr_pgbench_check.log 2>&1

# create databases
echo "Creating databases"
$BINDIR/psql -h $PRIMARY_HOST -p $PRIMARY_PORT postgres -c "CREATE DATABASE $PRIMARY_DB" >>$SCRIPTDIR/bdr_pgbench_check.log 2>&1
$BINDIR/psql -h $SECONDARY_HOST -p $SECONDARY_PORT postgres -c "CREATE DATABASE $SECONDARY_DB" >>$SCRIPTDIR/bdr_pgbench_check.log 2>&1
$BINDIR/psql -h $PRIMARY_HOST -p $PRIMARY_PORT $PRIMARY_DB > /dev/null << SQL
DO \$\$
BEGIN
FOR i IN 1..10 LOOP
	PERFORM * FROM pg_replication_slots;
	IF FOUND THEN
		RETURN;
	END IF;
	PERFORM pg_sleep(1);
END LOOP;
PERFORM bdr.wait_slot_confirm_lsn(NULL,NULL);
END;\$\$;
SQL

# initialize pgbench
echo "Setting up pgbench schema on primary db"
$BINDIR/pgbench  -q -i -s $SCALE -h $PRIMARY_HOST -p $PRIMARY_PORT $PRIMARY_DB  >>$SCRIPTDIR/bdr_pgbench_check.log 2>&1
$BINDIR/psql -h $PRIMARY_HOST -p $PRIMARY_PORT $PRIMARY_DB -c "SELECT bdr.wait_slot_confirm_lsn(NULL, NULL);" > /dev/null
if [ "$RUNMODE" = "parallel" ]; then
	echo "Setting up pgbench schema on secondary db"
	$BINDIR/psql -h $SECONDARY_HOST -p $SECONDARY_PORT $SECONDARY_DB -c "CREATE SCHEMA pgbench2; ALTER DATABASE $SECONDARY_DB SET search_path=pgbench2,pg_catalog;" >>$SCRIPTDIR/bdr_pgbench_check.log 2>&1
	$BINDIR/psql -h $SECONDARY_HOST -p $SECONDARY_PORT $SECONDARY_DB -c "SELECT bdr.wait_slot_confirm_lsn(NULL, NULL);" > /dev/null
	$BINDIR/pgbench -q -i -s $SCALE -h $SECONDARY_HOST -p $SECONDARY_PORT $SECONDARY_DB >>$SCRIPTDIR/bdr_pgbench_check.log 2>&1
	$BINDIR/psql -h $SECONDARY_HOST -p $SECONDARY_PORT $SECONDARY_DB -c "SELECT bdr.wait_slot_confirm_lsn(NULL, NULL);" > /dev/null
fi

# run pgbench
echo "Running pgbench (for $(convertsecs RUNTIME)) ..."
if [ "$RUNMODE" = "zigzag" ]; then
	NUM_RUNS=10
	RUNTIME=$(($RUNTIME/$NUM_RUNS))
else
	NUM_RUNS=1
fi

for i in `seq 1 $NUM_RUNS`; do

	if [ $(($i%2)) -eq 1 ]; then
		$BINDIR/pgbench -n -T $RUNTIME -j $CLIENTS -c $CLIENTS -h $PRIMARY_HOST -p $PRIMARY_PORT $PRIMARY_DB >>$SCRIPTDIR/bdr_pgbench_check.log 2>&1 &
		PRIMARY_BENCHPID=$!
	fi
	if [ $(($i%2)) -eq 0 ] || [ "$RUNMODE" = "parallel" ]; then
		$BINDIR/pgbench -n -T $RUNTIME -j $CLIENTS -c $CLIENTS -h $SECONDARY_HOST -p $SECONDARY_PORT $SECONDARY_DB >>$SCRIPTDIR/bdr_pgbench_check.log 2>&1 &
		SECONDARY_BENCHPID=$!
	fi

	# wait for pgbench instance(s) to finish
	while kill -0 $PRIMARY_BENCHPID 2>>/dev/null || ( [ -n "$SECONDARY_BENCHPID" ] && kill -0 $SECONDARY_BENCHPID 2>>/dev/null ) ; do
		sleep 1
	done

done

$BINDIR/psql -h $PRIMARY_HOST -p $PRIMARY_PORT $PRIMARY_DB -c "SELECT bdr.wait_slot_confirm_lsn(NULL, NULL);" > /dev/null
$BINDIR/psql -h $SECONDARY_HOST -p $SECONDARY_PORT $SECONDARY_DB -c "SELECT bdr.wait_slot_confirm_lsn(NULL, NULL);" > /dev/null

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

$BINDIR/psql -h $PRIMARY_HOST -p $PRIMARY_PORT $PRIMARY_DB -c "$SQL" >$DATADIR/primary.chksum 2>&1
$BINDIR/psql -h $SECONDARY_HOST -p $SECONDARY_PORT $SECONDARY_DB -c "$SQL" >$DATADIR/secondary.chksum 2>&1

echo "Pgbench finished, cleaning up"
# stop pg
$BINDIR/pg_ctl -D $DATADIR/data stop -w -mfast >>$SCRIPTDIR/bdr_pgbench_check.log 2>&1

cd $SCRIPTDIR

diff -u $DATADIR/primary.chksum $DATADIR/secondary.chksum > $DATADIR/chksum.diff
if [ ! -n "$?" ]; then
	echo "ERROR: data in databases differ, check $DATADIR/chksum.diff"
	exit 1
fi
