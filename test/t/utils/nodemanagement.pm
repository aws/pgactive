#!/usr/bin/env perl
#
# Shared test code for simple pgactive node management.
#
package utils::nodemanagement;

use strict;
use warnings;
use Exporter;
use Cwd;
use Config;
use Carp qw(cluck);
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use IPC::Run;
use Time::HiRes qw(usleep);
use vars qw($pgactive_test_dbname);

use Carp 'verbose';
$SIG{__DIE__} = \&Carp::confess;

use vars qw(@ISA @EXPORT @EXPORT_OK);
@ISA         = qw(Exporter);
@EXPORT      = qw(
    $pgactive_test_dbname
    make_pgactive_group
    initandstart_node
    initandstart_pgactive_group
    initandstart_logicaljoin_node
    pgactive_logical_join
    create_pgactive_group
    initandstart_physicaljoin_node
    check_join_status
    pgactive_detach_nodes
    check_detach_status
    stop_nodes
    detach_and_check_nodes
    exec_ddl
    node_isready
    wait_for_pg_isready
    create_table
    dump_nodes_statuses
    check_joinfail_status
    initandstart_join_node
    wait_for_apply
    start_acquire_ddl_lock
    wait_acquire_ddl_lock
    cancel_ddl_lock
    release_ddl_lock
    generate_pgactive_logical_join_query
    pgactive_update_postgresql_conf
    get_log_size
    find_in_log
    create_pgactive_group_with_db
    join_pgactive_group_with_db
    create_and_check_data
    );

# For use by other modules, but need not appear in the default namespace of
# tests.
@EXPORT_OK   = qw(
    copy_transform_postgresqlconf
    start_pgactive_init_copy
    wait_detach_completion
);

BEGIN {
    $pgactive_test_dbname = 'pgactive_test';
}

my $tempdir = PostgreSQL::Test::Utils::tempdir;

# Make a group of pgactive nodes with numbered node names
# and returns a list of the nodes.
sub make_pgactive_group {
    my ($n_nodes, $name_prefix, $mode, $no_dsn) = @_;
    $mode = 'logical' if !defined($mode);
    $name_prefix = 'node_' if !defined($name_prefix);
    $no_dsn = 0 if !defined($no_dsn);

    die "unrecognised join mode $mode"
        if ($mode ne 'logical' && $mode ne 'physical');

    my $node_0 = PostgreSQL::Test::Cluster->new("${name_prefix}0");
    my @nodes;
    push @nodes, $node_0;

    for (my $nodeid = 1; $nodeid < $n_nodes; $nodeid++)
    {
        my $node_n = PostgreSQL::Test::Cluster->new("${name_prefix}${nodeid}");
        push @nodes, $node_n;
    }

    initandstart_pgactive_group($node_0, $no_dsn, \@nodes);

    for (my $nodeid = 1; $nodeid < $n_nodes; $nodeid++)
    {
        my $node_n = $nodes[ $nodeid ];
        if ($mode eq 'logical')
        {
            initandstart_logicaljoin_node($node_n, $node_0, $no_dsn, \@nodes);
        }
        else
        {
            initandstart_physicaljoin_node($node_n, $node_0);
        }
    }

    return \@nodes;
}

# Wrapper around pgactive.pgactive_create_group
#
sub create_pgactive_group {
    my ($node, $no_dsn, $nodes) = @_;
    $no_dsn = 0 if !defined($no_dsn);
    my $pgport = $node->port;
    my $pghost = $node->host;
    my $node_connstr = "port=$pgport host=$pghost dbname=$pgactive_test_dbname";

    if ( $no_dsn eq 1 ) {
        my $n_nodes = scalar(@$nodes);
        my $node_user = $ENV{USERNAME} || $ENV{USERNAME} || $ENV{USER} ;
        for (my $nodeid = 0; $nodeid < $n_nodes; $nodeid++)
        {
            my $t_node = $nodes->[$nodeid];
            my $node_connstr = "server_@{[ $t_node->name ]}";
            my $node_port = $t_node->port;
            my $node_host = $t_node->host;
            $node->safe_psql( $pgactive_test_dbname, qq{CREATE SERVER $node_connstr FOREIGN DATA WRAPPER pgactive_fdw OPTIONS (port '$pgport', dbname '$pgactive_test_dbname', host '$pghost');} );
            $node->safe_psql( $pgactive_test_dbname, qq{CREATE USER MAPPING FOR $node_user  SERVER $node_connstr OPTIONS ( user '$node_user');} );
        }
    }
    $node->safe_psql(
        $pgactive_test_dbname, qq{
            SELECT pgactive.pgactive_create_group(
                    local_node_name := '@{[ $node->name ]}',
                    node_external_dsn := '$node_connstr'
                    );
            }
    );
    $node->safe_psql( $pgactive_test_dbname,
        qq[SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
    $node->safe_psql( $pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()' ) eq 't'
        or BAIL_OUT('!pgactive.pgactive_is_active_in_db() after pgactive_create_group');
}

# Given a newly allocated PostgresNode, bring up a standalone 1-node pgactive
# system using pgactive_create_group.
sub initandstart_pgactive_group {
    my $node      = shift;
    my $no_dsn    = shift;
    my $nodes     = shift;

    initandstart_node($node);
    create_pgactive_group($node, $no_dsn, $nodes);
}

# Init and start node with pgactive, create the test DB and install the pgactive
# extension.
sub initandstart_node {
    my ($node, $db, %kwopts) = @_;

    $node->init( hba_permit_replication => 1, allows_streaming => 1,
				 %{$kwopts{extra_init_opts}//{}} );
    pgactive_update_postgresql_conf( $node );
    $node->start;
    _create_db_and_exts( $node, $db );

}

# Edit postgresql.conf with required parameters for pgactive
sub pgactive_update_postgresql_conf {
    my ($node) = shift;

    # Set timeouts for lock acquire to avoid indefinite wait loops in tests.
    # For example, it was noticed in CI tests that LOCK TABLE on pgactive.pgactive_nodes
    # in pgactive.pgactive_detach_nodes can sporadically cause indefinite wait loop in
    # 042_concurrency_physical.pl.
    my $lock_acquire_timeout =
        $PostgreSQL::Test::Utils::timeout_default .'s';

    # Setting pgactive.debug_trace_replay=on here can be a big help, so added for
    # discoverability.
    $node->append_conf(
        'postgresql.conf', qq(
            wal_level = logical
            track_commit_timestamp = on
            shared_preload_libraries = 'pgactive'
            max_connections = 100
            max_wal_senders = 20
            max_replication_slots = 20
            # Make sure there are enough background worker slots for pgactive to run
            max_worker_processes = 20
            #log_min_messages = debug2
            #pgactive.debug_trace_replay = off
            log_line_prefix = '%m %p %d [%a] %c:%l (%v:%t) '
			pgactive.skip_ddl_replication = false
			pgactive.log_conflicts_to_table = false
            pgactive.max_nodes = 20
            pgactive.ddl_lock_acquire_timeout = $lock_acquire_timeout
            lock_timeout = $lock_acquire_timeout
    ));
}

sub _create_db_and_exts {
    my ($node, $db) = @_;

    $db = $pgactive_test_dbname if !defined($db);

    $node->safe_psql( 'postgres', qq{CREATE DATABASE $db;} );
    $node->safe_psql( $db,    q{CREATE EXTENSION pgactive;} );
}
sub initandstart_join_node {
    my $join_node          = shift;
    my $upstream_node      = shift;
    my $type          = shift;

    if ( $type eq 'logical' ) {
        initandstart_logicaljoin_node( $join_node, $upstream_node );
    }
    elsif ( $type eq 'physical' ) {
        initandstart_physicaljoin_node( $join_node, $upstream_node );
    }
}
# Shortcut for creating a new node, joining to upstream and validating
# the join with some TAP tests.
sub initandstart_logicaljoin_node {
    my $join_node          = shift;
    my $upstream_node      = shift;
    my $join_node_name     = $join_node->name();
    my $upstream_node_name = $upstream_node->name();

    initandstart_node($join_node);
    pgactive_logical_join( $join_node, $upstream_node );
    check_join_status( $join_node,$upstream_node);
}

#
# Generate a query for pgactive.pgactive_join_group
#
# Caller is responsible for quote escaping on extra params.
#
sub generate_pgactive_logical_join_query {
    my ($local_node, $join_node, %params) = @_;

	my $ln_port = $local_node->port;
	my $ln_host = $local_node->host;
    my $ln_connstr = "port=$ln_port host=$ln_host dbname=$pgactive_test_dbname";

	my $jn_port = $join_node->port;
	my $jn_host = $join_node->host;
    my $jn_connstr = "port=$jn_port host=$jn_host dbname=$pgactive_test_dbname";

    my $join_query = qq{
            SELECT pgactive.pgactive_join_group(
                    local_node_name := '@{[$local_node->name]}',
                    node_external_dsn := '$ln_connstr',
                    join_using_dsn := '$jn_connstr'};

    while (my ($k,$v) = each(%params)) {
        $join_query .= ", $k := '$v'";
    }

    $join_query .= ");";

    return $join_query;
}

# pgactive group join with optional extra params passed directly to pgactive.pgactive_join_group
#
# Caller is responsible for quote escaping on extra params.
#
sub pgactive_logical_join {
    my ($local_node, $join_node, %params) = @_;

    my $nowait = delete $params{nowait} // 0;

    my $join_query = generate_pgactive_logical_join_query($local_node, $join_node, %params);
    $local_node->safe_psql($pgactive_test_dbname, $join_query);

    if (!$nowait) {
        $local_node->safe_psql( $pgactive_test_dbname,
            qq[SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
    }
}

# Copy postgresql.conf from an existing node to a temporary
# location, changing the port to match the generated port for
# a new node.
#
sub copy_transform_postgresqlconf {
    my ($join_node, $upstream_node) = @_;
    my $join_node_name = $join_node->name();
    my $outfile_name   = "$tempdir/postgresql.conf.$join_node_name";

    open( my $upstream_conf,
        "<", $upstream_node->data_dir . '/postgresql.conf' )
      or die("can't open node_a conf file for reading: $!");

    open( my $joinnode_conf, ">", $outfile_name )
      or die("can't open node_b conf file for writing: $!");

    while (<$upstream_conf>) {
        if ( $_ =~ "^port" ) {
            print $joinnode_conf "port = " . $join_node->port . "\n";
        }
        else {
            print $joinnode_conf $_;
        }
    }
    close($upstream_conf) or die("failed to close old postgresql.conf: $!");
    close($joinnode_conf) or die("failed to close new postgresql.conf: $!");

    return $outfile_name;
}

#
# Run pgactive_init_copy and return an IPC::Run::Handle for it, which can be waited
# on with $h->finish, result code tested with $h->result (for unix return code
# from process) or $h->full_result(0) (for shell result including signal codes),
# etc.
#
# An optional 3rd argument is an arrayref of extra arguments to IPC::Run::start,
# like
#
#     ['timeout', 30]
#
# If called in array context, handles to stdout and stderr are returned,
# otherwise they're left connected to the system file handles.
#
# IPC::Run exceptions will be thrown to the caller.
#
sub start_pgactive_init_copy {
    my ($join_node, $upstream_node, $new_conf_file, $extra_ipc_run_opts) = @_;
    
    my @ipcrun_opts = (
        [
            'pgactive_init_copy',     '-v',
            '-D',                $join_node->data_dir,
            "-n",                $join_node->name,
            '-d',                $upstream_node->connstr($pgactive_test_dbname),
            '--local-dbname',    $pgactive_test_dbname,
            '--local-port',      $join_node->port,
            '--postgresql-conf', $new_conf_file
        ],
    );

    my $stdout = '';
    my $stderr = '';
    
    
    if (wantarray) {
        push @ipcrun_opts, '>', \$stdout, '2>', \$stderr;
    }

    my $h = IPC::Run::start(@ipcrun_opts,
                            ref $extra_ipc_run_opts ? @{$extra_ipc_run_opts} : undef);

    if (wantarray) {
        return ($h, $stdout, $stderr);
    } else { 
        return $h;
    }
}

# Initialize a node and do a physical join to upstream node using
# pgactive_init_copy.
#
# A new config file is generated by copying the upstream's file and changing
# the port. (If we need to add extra params to the new node's config file we
# can add an extra option to this function containing a string of params
# to append after copy.)
#
# Adds 4 tests.
#
sub initandstart_physicaljoin_node {
    my ($join_node, $upstream_node) = @_;

    my $new_conf_file = copy_transform_postgresqlconf( $join_node, $upstream_node );
    my $timeout = IPC::Run::timeout($PostgreSQL::Test::Utils::timeout_default, exception=>"Timed out");
    my $h = start_pgactive_init_copy($join_node, $upstream_node, $new_conf_file, [$timeout]);
    $h->finish;
    is($h->result(0), 0, 'pgactive_init_copy exited without error');

    # wait for Pg to start
    wait_for_pg_isready($join_node);

    # wait for pgactive to come up
    $upstream_node->safe_psql( $pgactive_test_dbname,
        qq[SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
    $join_node->safe_psql( $pgactive_test_dbname,
        qq[SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);

    $join_node->safe_psql( $pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()' ) eq 't'
        or BAIL_OUT('!pgactive.pgactive_is_active_in_db() after pgactive_create_group');

    # PostgresNode doesn't know we started the node since we didn't
    # use any of its methods, so we'd better tell it to check. Otherwise
    # it'll ignore the node for things like pg_ctl stop.
    $join_node->_update_pid(1);

    check_join_status($join_node, $upstream_node);
}

# 1. Check pgactive is_active status is 't'
# 2. Check node status is ready 'r' on self and upstream node.
# 3. Ensure active replication slots present on both ends
#
sub check_join_status {
    my $join_node          = shift;
    my $upstream_node      = shift;
    my $join_node_name     = $join_node->name();
    my $upstream_node_name = $upstream_node->name();

    is( $join_node->safe_psql( $pgactive_test_dbname, 'SELECT pgactive.pgactive_is_active_in_db()' ),
        't', qq(pgactive is_active status on $join_node_name after join) );

    is(
        $join_node->safe_psql(
            $pgactive_test_dbname,
            "SELECT node_status FROM pgactive.pgactive_nodes WHERE node_name = '$join_node_name'"
        ),
        'r',
        qq($join_node_name status is 'r' on new node)
    );

    is(
        $upstream_node->safe_psql(
            $pgactive_test_dbname,
            "SELECT node_status FROM pgactive.pgactive_nodes WHERE node_name = '$join_node_name'"
        ),
        'r',
        qq($join_node_name status is 'r' on upstream node)
    );

    # The new node's slot on the join target must be created
    is(
        $upstream_node->safe_psql($pgactive_test_dbname, qq[SELECT EXISTS (SELECT 1 FROM pgactive.pgactive_node_slots WHERE node_name = '$join_node_name')]),
        't',
        qq(replication slot for $join_node_name on $upstream_node_name has been created)
    );

    # The join target's slot on the new node must be created
    is(
        $join_node->safe_psql($pgactive_test_dbname, qq[SELECT EXISTS (SELECT 1 FROM pgactive.pgactive_node_slots WHERE node_name = '$upstream_node_name')]),
        't',
        qq(replication slot for $upstream_node_name on $join_node_name has been created)
    );

    # The join target must have an active connection to the new node
    is(
        $join_node->safe_psql($pgactive_test_dbname, qq[SELECT EXISTS (SELECT 1 FROM pg_stat_activity WHERE application_name = '$upstream_node_name:send')]),
        't',
        qq(replication connection for $upstream_node_name on $join_node_name is present)
    );

    # The new node must have an active connection to the join target
    is(
        $upstream_node->safe_psql($pgactive_test_dbname, qq[SELECT EXISTS (SELECT 1 FROM pg_stat_activity WHERE application_name = '$join_node_name:send')]),
        't',
        qq(replication connection for $join_node_name on $upstream_node_name is present)
    );
}

sub wait_detach_completion {
    my ($detach_node, $upstream_node) = @_;

    if (!$upstream_node->poll_query_until($pgactive_test_dbname, qq[SELECT NOT EXISTS (SELECT 1 FROM pgactive.pgactive_node_slots WHERE node_name = '] . $detach_node->name . "')")) {
        cluck("replication slot for node " . $detach_node->name . " on " . $upstream_node->name . " was not removed, trying to continue anyway");
    }
}

# Remove one or mote nodes from cluster using 'pgactive_detach_nodes'.
#
# Does not check detach status.
#
# Thread safe.
sub pgactive_detach_nodes {
    my $pgactive_detach_nodes         = shift;
    my $upstream_node      = shift;
    my $upstream_node_name = $upstream_node->name();

    for my $detach_node (@{$pgactive_detach_nodes}) {
        my $detach_node_name = $detach_node->name();
        $upstream_node->safe_psql($pgactive_test_dbname, qq[SELECT EXISTS (SELECT 1 FROM pgactive.pgactive_node_slots WHERE node_name = '$detach_node_name')])
            or BAIL_OUT("could not find existing slot for $detach_node_name on $upstream_node_name before detaching");
    }

    my $nodelist = "ARRAY['" . join("','", map { $_->name } @{$pgactive_detach_nodes}) . "']";

    $upstream_node->safe_psql( $pgactive_test_dbname,
        "SELECT pgactive.pgactive_detach_nodes($nodelist)" );

    # We can tell a detach has taken effect when the downstream's slot vanishes
    # on the upstream.
    for my $detach_node (@{$pgactive_detach_nodes}) {
        wait_detach_completion($detach_node, $upstream_node);
    }
}

# Stop all nodes passed. Trivial wrapper around PostgresNode::stop
#
# Thread safe.
sub stop_nodes {
    my ($stop_nodes, $mode) = @_;

    for my $stop_node (@{$stop_nodes}) {
        $stop_node->stop($mode, fail_ok => 1);
    }
}

# Check node status is 'k' on self and upstream node
# for each detached node
sub check_detach_status {
    my $pgactive_detach_nodes         = shift;
    my $upstream_node      = shift;
    my $upstream_node_name = $upstream_node->name();

    foreach my $detach_node (@$pgactive_detach_nodes) {
        my $detach_node_name     = $detach_node->name();

        is(
            $upstream_node->safe_psql(
                $pgactive_test_dbname,
                "SELECT node_status FROM pgactive.pgactive_nodes WHERE node_name = '$detach_node_name'"
            ),
            'k',
            qq($detach_node_name status on upstream node after detach is 'k')
        );

        # It is unsafe/incorrect to expect the detached node to know it's detached and
        # have a 'k' state. Sometimes it will, sometimes it won't, it depends on a
        # race between the detaching node terminating its connections and it
        # receiving notification of its own detaching. That's a bit of a wart in pgactive,
        # but won't be fixed in 2.0 and is actually very hard to truly "fix" in a
        # distributed system. So we allow the local node status to be 'k' or 'r'.
        #
        like(
            $detach_node->safe_psql(
                $pgactive_test_dbname,
                "SELECT node_status FROM pgactive.pgactive_nodes WHERE node_name = '$detach_node_name'"
            ),
            qr/^(k|r)$/,
            qq($detach_node_name status on local node after detach is 'k' or 'r')
        );

        # The downstream's slot on the upstream MUST be gone
        is(
            $upstream_node->safe_psql($pgactive_test_dbname, qq[SELECT EXISTS (SELECT 1 FROM pgactive.pgactive_node_slots WHERE node_name = '$detach_node_name')]),
            'f',
            qq(replication slot for $detach_node_name on $upstream_node_name has been removed)
        );

        # The upstream's slot on the downstream MAY be gone, or may be present, so
        # there's no point checking. But the upstream's connection to the downstream
        # MUST be gone, so we can look for the apply worker's connection.
        is(
            $detach_node->safe_psql($pgactive_test_dbname, qq[SELECT EXISTS (SELECT 1 FROM pg_stat_activity WHERE application_name = '$upstream_node_name:send')]),
            'f',
            qq(replication connection for $upstream_node_name on $detach_node_name is gone)
        );
    }
}

# Shorthand for pgactive_detach_nodes(), check_detach_status(), stop_nodes()
sub detach_and_check_nodes {
    my ($pgactive_detach_nodes, $upstream_node) = @_;
    pgactive_detach_nodes($pgactive_detach_nodes, $upstream_node);
    check_detach_status($pgactive_detach_nodes, $upstream_node);
    stop_nodes($pgactive_detach_nodes);
}

# 
# Remove the pgactive.pgactive_nodes entry for a detached node, so that its node name may
# be re-used.  The node must already be marked as detached.
#
sub delete_detached_node_from_catalog {
    my ($detached_node, $upstream_node) = @_;
    my $detach_node_name     = $detached_node->name();
    my $upstream_node_name = $upstream_node->name();

    my $deleted = $upstream_node->safe_psql( $pgactive_test_dbname,
        "DELETE FROM pgactive.pgactive_nodes WHERE node_name = '$detach_node_name' and node_status = 'k' returning 1"
    );

    if ($deleted ne '1') {
        BAIL_OUT("attempt to delete pgactive.pgactive_nodes row for $detach_node_name from $upstream_node_name failed, node not found or status <> k");
    }
}

# Execute the specified DDL string on the pgactive test DB using pgactive.pgactive_replicate_ddl_command
#
# Threadsafe.
sub exec_ddl {
    my ($node, $ddl_string) = @_;

    $node->safe_psql($pgactive_test_dbname, qq{
        SELECT pgactive.pgactive_replicate_ddl_command(\$DDL\$ $ddl_string \$DDL\$);
    });
}

# Invoke pg_isready and return result. 0 is success/ready.
#
# Threadsafe.
sub node_isready {
    my $node = shift;
    IPC::Run::run([
        'pg_isready', '-d', $node->connstr('postgres')
        ]);
    return $?;
}

# Wait until pg_isready says a node is up or timeout (if supplied) exceeded. Returns
# 0 on timeout, 1 on success.
#
# Threadsafe.
sub wait_for_pg_isready {
    my ($node, $maxwait) = @_;
    $maxwait = $PostgreSQL::Test::Utils::timeout_default if !defined($maxwait);

    my $waited = 0;
    my $wait_secs = 0.5;
    while (1) {
        my $ret = node_isready($node);
        last if $ret == 0;
        sleep($wait_secs);
        $waited += $wait_secs;
        if ($maxwait && ($waited > $maxwait))
        {
            diag "gave up waiting for node " . $node->name . " to become ready after $maxwait seconds, last result was $ret";
            return 0;
        }
    };

    return 1;
}

# Print out pgactive.pgactive_nodes status info for a node
#
# Threadsafe(ish)?
sub dump_nodes_statuses {
    my $node = shift;
    note "Nodes table from " . $node->name . " is:\n" . $node->safe_psql($pgactive_test_dbname, q[select node_name, node_status from pgactive.pgactive_nodes]) . "\n";
}

# Create a dummy table on a node, with single field 'id'.
#
# Threadsafe.
sub create_table {
    my ($node, $table_name) = @_;
    exec_ddl($node,qq{ CREATE TABLE public.$table_name( id integer primary key);});
}

# Check that no slots or nodes entries are created for failed join on peer
# nodes.
#
sub check_joinfail_status {
    my ($join_node, $join_node_sysid, $join_node_timeline, @peer_nodes) = @_;
    my $join_node_name = $join_node->name();

#   die "join node sysid and timeline must be passed"
#       unless ($join_node_sysid and $join_node_timeline);
#
#   die "join node sysid and timeline must be scalars"
#       if (ref $join_node_sysid || ref $join_node_timeline);

    foreach my $node (@peer_nodes){
        is($node->safe_psql($pgactive_test_dbname, "SELECT node_status FROM pgactive.pgactive_nodes WHERE node_name = '$join_node_name'"), '', "no nodes entry on ". $node->name() . " from " . $join_node_name . " after failed join" );
    }
    my ($sysid, $timeline, $dboid);
    eval {
         ($sysid, $timeline, $dboid) = split(qr/\|/, $join_node->safe_psql($pgactive_test_dbname, 'SELECT * FROM pgactive.pgactive_get_local_nodeid()'));
    };
    if ($@) {
        die("couldn't query joining node for its sysid and timeline: $@");
    }
    foreach my $node (@peer_nodes) {
        my $slotname = $node->safe_psql($pgactive_test_dbname, qq[SELECT pgactive.pgactive_format_slot_name('$sysid', '$timeline', '$dboid', '');]);
        is($node->slot($slotname)->{'slot_name'}, '', "slot for " . $join_node_name . " not created on peer node " . $node->name)
            or diag "slot name is $slotname";
        
    }
}

# Wait until a peer has caught up
sub wait_for_apply {
    my ($self, $peer) = @_;
    # On node <self>, wait until the send pointer on the replication slot with
    # application_name "<peer>:send" to passes the xlog flush position on node
    # <self> at the time of this call.
    my $lsn = $self->lsn('flush');
    die('no lsn to catch up to') if !defined $lsn;
    $self->wait_for_catchup($peer->name . ":send", 'replay', $lsn);
}

# Acquire a global ddl lock on $node in $mode using a background
# psql session and return the IPC::Run handle for the session
# along with a hash its stdin, stdout and stderr handles.
#
# $timer, if supplied, may be an IPC::Run::Timer or IPC::Run::Timeout
# object to time-limit the acquisition attempt. Timeouts die() on expiry,
# timers must be passed to wait_acquire_ddl_lock.
sub start_acquire_ddl_lock {
    my ($node, $mode, $timer) = @_;
    my ($psql_stdout, $psql_stderr) = ('','');

    my $psql_stdin = qq[
BEGIN;
SELECT pg_backend_pid() || '=pid';
SELECT 'acquired' FROM pgactive.pgactive_acquire_global_lock('$mode');
];

    my $psql = IPC::Run::start(
        ['psql', '-qAtX', '-d', $node->connstr($pgactive_test_dbname), '-f', '-'],
        '<', \$psql_stdin, '>', \$psql_stdout, '2>', \$psql_stderr,
        $timer);

    $psql->pump until $psql_stdout =~ qr/([[:digit:]]+)=pid/;

    my $backend_pid = $1;
    print("pid of backend acquiring ddl lock is $backend_pid\n");

    # Acquire should be in progress or finished
    if ($node->safe_psql($pgactive_test_dbname, qq[SELECT 1 FROM pg_stat_activity WHERE query LIKE '%pgactive.pgactive_acquire_global_lock%' AND pid = $backend_pid;]) ne '1')
    {
        croak("cannot find expected query   SELECT 'acquired' FROM pgactive.pgactive_acquire_global_lock...   in pg_stat_activity\n");
    }

	$node->poll_query_until($pgactive_test_dbname, q[SELECT lock_state <> 'nolock' FROM pgactive.pgactive_global_locks_info]);

    my $status = $node->safe_psql($pgactive_test_dbname, q[SELECT lock_state, lock_mode, owner_is_my_node, owner_is_my_backend FROM pgactive.pgactive_global_locks_info]);
	if (not ($status =~ qr/(?:acquire_acquired|acquire_tally_confirmations)\|$mode\|t\|f/))
	{
		croak("expected lock info (acquire_acquired|acquire_tally_confirmations)|$mode|t|f, got $status");
	}

	print("lock acquire in progress...\n");

    return {
        handle => $psql,
        stdin => \$psql_stdin,
        stdout => \$psql_stdout,
        stderr => \$psql_stderr,
        node => $node,
        backend_pid => $backend_pid,
        mode => $mode
    };
}

# Wait to acquire global ddl lock on handle supplied by start_acquire_ddl_lock.
#
# By default waits forever (or until timeout supplied at start),
# and dies if acquisition fails.
#
sub wait_acquire_ddl_lock {
    my ($psql, $timer, $no_error_die) = @_;
    my $success = 1;

    do {
        $psql->{'handle'}->pump;
        last if defined($timer) && $timer->is_expired;
    }
    until (${$psql->{'stdout'}} =~ 'acquired' or ${$psql->{'stderr'}} =~ 'ERROR' or !$psql->{'handle'}->pumpable);

    print("acquired or failed\n");

    if (${$psql->{stderr}} =~ 'ERROR')
    {
        ${$psql->{stdin}} .= "\\q\n";
        $psql->{handle}->pump;
        $psql->{handle}->kill_kill;
        croak("could not acquire global ddl lock in mode " . $psql->{mode} . " on " . $psql->{node}->name . ": " . ${$psql->{stderr}})
            unless($no_error_die);
    }

	# TODO: double check against pgactive.pgactive_global_locks_info
    return ${$psql->{'stdout'}} =~ 'acquired';
}

sub cancel_ddl_lock {
    my $psql = shift;
    $psql->{node}->safe_psql($pgactive_test_dbname, "SELECT pg_terminate_backend(" . $psql->{backend_pid} . ")");
}

sub release_ddl_lock {
    my $psql = shift;

    ${$psql->{stdin}} .= "ROLLBACK;\n\\echo ROLLBACK\n\\q";
    $psql->{handle}->finish;
}

# Return the size of logfile of $node in bytes
sub get_log_size
{
	my ($node) = @_;

	return (stat $node->logfile)[7];
}

# Find $pat in logfile of $node after $off-th byte
sub find_in_log
{
	my ($node, $pat, $off) = @_;
	#my $max_attempts = $PostgreSQL::Test::Utils::timeout_default * 10;
	my $max_attempts = 60 * 10;
	my $log;

	while ($max_attempts-- >= 0)
	{
		$log = PostgreSQL::Test::Utils::slurp_file($node->logfile, $off);
		last if ($log =~ m/$pat/);
		usleep(100_000);
	}

	return $log =~ m/$pat/;
}

sub create_pgactive_group_with_db {
    my ($node_name, $db) = @_;

    my $node = PostgreSQL::Test::Cluster->new($node_name);
    initandstart_node($node, $db);

    my $port = $node->port;
    my $host = $node->host;
    my $node_connstr = "port=$port host=$host dbname=$db";

    $node->safe_psql($db, qq{
        SELECT pgactive.pgactive_create_group(
            local_node_name := '$node_name',
            node_external_dsn := '$node_connstr');});
    $node->safe_psql($db, qq[
        SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
    $node->safe_psql($db, 'SELECT pgactive.pgactive_is_active_in_db()' ) eq 't'
    or BAIL_OUT('!pgactive.pgactive_is_active_in_db() after pgactive_create_group');

    return $node;
}

sub join_pgactive_group_with_db {
    my ($node_name, $upstream_node, $db) = @_;

    my $node = PostgreSQL::Test::Cluster->new($node_name);
    initandstart_node($node, $db);

    my $port = $node->port;
    my $host = $node->host;
    my $node_connstr = "port=$port host=$host dbname=$db";

    $port = $upstream_node->port;
    $host = $upstream_node->host;
    my $upstream_node_connstr = "port=$port host=$host dbname=$db";

    $node->safe_psql($db, qq{
        SELECT pgactive.pgactive_join_group(
            local_node_name := '$node_name',
            node_external_dsn := '$node_connstr',
            join_using_dsn := '$upstream_node_connstr');});
    $node->safe_psql($db, qq[
        SELECT pgactive.pgactive_wait_for_node_ready($PostgreSQL::Test::Utils::timeout_default)]);
    $node->safe_psql($db, 'SELECT pgactive.pgactive_is_active_in_db()' ) eq 't'
    or BAIL_OUT('!pgactive.pgactive_is_active_in_db() after pgactive_join_group');

    return $node;
}

sub create_and_check_data {
    my ($node1, $node2, $db) = @_;

    $node1->safe_psql($db,
        q[CREATE TABLE fruits(id integer, name varchar);]);
    $node1->safe_psql($db,
        q[INSERT INTO fruits VALUES (1, 'Mango');]);
    wait_for_apply($node1, $node2);

    $node2->safe_psql($db,
        q[INSERT INTO fruits VALUES (2, 'Apple');]);
    wait_for_apply($node2, $node1);

    my $query = qq[SELECT COUNT(*) FROM fruits;];
    my $expected = 2;
    my $res1 = $node1->safe_psql($db, $query);
    my $res2 = $node2->safe_psql($db, $query);

    is($res1, $expected, "pgactive node " . $node1->name() . "has all the data");
    is($res2, $expected, "pgactive node " . $node2->name() . "has all the data");
}

1;
