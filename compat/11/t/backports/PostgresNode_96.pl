# We're injecting methods into PostgresNode that we want from PostgreSQL 10
#
# Hopefully a backport of these will occur to 9.6, but in the mean time,
# we hack around it.
#
package PostgresNode;

use Carp;

=pod

=item $node->lsn(mode)

Look up WAL positions on the server:

 * insert position (master only, error on replica)
 * write position (master only, error on replica)
 * flush position (master only, error on replica)
 * receive position (always undef on master)
 * replay position (always undef on master)

mode must be specified.

=cut

sub lsn
{
	my ($self, $mode) = @_;
	my %modes = ('insert' => 'pg_current_xlog_insert_location()',
				 'flush' => 'pg_current_xlog_flush_location()',
				 'write' => 'pg_current_xlog_location()',
				 'receive' => 'pg_last_xlog_receive_location()',
				 'replay' => 'pg_last_xlog_replay_location()');

	$mode = '<undef>' if !defined($mode);
	die "unknown mode for 'lsn': '$mode', valid modes are " . join(', ', keys %modes)
		if !defined($modes{$mode});

	my $result = $self->safe_psql('postgres', "SELECT $modes{$mode}");
	chomp($result);

	if ($result eq '')
	{
		return;
	}
	else
	{
		return $result;
	}
}

=pod

=item $node->wait_for_catchup(standby_name, mode, target_lsn)

Wait for the node with application_name standby_name (usually from node->name)
until its replication position in pg_stat_replication equals or passes the
upstream's WAL insert point at the time this function is called. By default
the replay_location is waited for, but 'mode' may be specified to wait for any
of sent|write|flush|replay.

If there is no active replication connection from this peer, waits until
poll_query_until timeout.

Requires that the 'postgres' db exists and is accessible.

target_lsn may be any arbitrary lsn, but is typically $master_node->lsn('insert').

This is not a test. It die()s on failure.

=cut

sub wait_for_catchup
{
	my ($self, $standby_name, $mode, $target_lsn) = @_;
	$mode = defined($mode) ? $mode : 'replay';
	my %valid_modes = ( 'sent' => 1, 'write' => 1, 'flush' => 1, 'replay' => 1 );
	die "unknown mode $mode for 'wait_for_catchup', valid modes are " . join(', ', keys(%valid_modes)) unless exists($valid_modes{$mode});
	# Allow passing of a PostgresNode instance as shorthand
	if ( blessed( $standby_name ) && $standby_name->isa("PostgresNode") )
	{
		$standby_name = $standby_name->name;
	}
	die 'target_lsn must be specified' unless defined($target_lsn);
	print "Waiting for replication conn " . $standby_name . "'s " . $mode . "_location to pass " . $target_lsn . " on " . $self->name . "\n";
	my $query = qq[SELECT '$target_lsn' <= ${mode}_location FROM pg_catalog.pg_stat_replication WHERE application_name = '$standby_name';];
	$self->poll_query_until('postgres', $query)
		or die "timed out waiting for catchup, current position is " . ($self->safe_psql('postgres', $query) || '(unknown)');
	print "done\n";
}

=pod

=item $node->wait_for_slot_catchup(slot_name, mode, target_lsn)

Wait for the named replication slot to equal or pass the supplied target_lsn.
The position used is the restart_lsn unless mode is given, in which case it may
be 'restart' or 'confirmed_flush'.

Requires that the 'postgres' db exists and is accessible.

This is not a test. It die()s on failure.

If the slot is not active, will time out after poll_query_until's timeout.

target_lsn may be any arbitrary lsn, but is typically $master_node->lsn('insert').

Note that for logical slots, restart_lsn is held down by the oldest in-progress tx.

=cut

sub wait_for_slot_catchup
{
	my ($self, $slot_name, $mode, $target_lsn) = @_;
	$mode = defined($mode) ? $mode : 'restart';
	if (!($mode eq 'restart' || $mode eq 'confirmed_flush'))
	{
		die "valid modes are restart, confirmed_flush";
	}
	die 'target lsn must be specified' unless defined($target_lsn);
	print "Waiting for replication slot " . $slot_name . "'s " . $mode . "_lsn to pass " . $target_lsn . " on " . $self->name . "\n";
	my $query = qq[SELECT '$target_lsn' <= ${mode}_lsn FROM pg_catalog.pg_replication_slots WHERE slot_name = '$slot_name';];
	$self->poll_query_until('postgres', $query)
		or die "timed out waiting for catchup, current position is " . ($self->safe_psql('postgres', $query) || '(unknown)');
	print "done\n";
}

1;
