#!/usr/bin/env perl
#
# Perform both physical and logical joins under
# an ongoing load from pgbench. The idea here
# is to make sure that we 
#
use strict;
use warnings;
use lib "t/";
use Cwd;
use Config;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use utils::nodemanagement;
use utils::concurrent;

my $pgbench_scale = 1;

# Create an upstream node and bring up bdr
my $node_a = PostgreSQL::Test::Cluster->new('node_a');
initandstart_node($node_a);
# We must init pgbench before we bring up BDR at the moment,
# since we don't support transparent DDL replication yet...
pgbench_init($node_a, $pgbench_scale);
create_bdr_group($node_a);

TODO: {
    # seems to hang during init, likely due to snapbuild bugs
    # 2ndQuadrant/bdr-private#67
    todo_skip 'logical join under write load hangs due to probable BDR bug', 8;
    note "Logical join node under write load\n";
    join_under_write_load('logical',$node_a, PostgreSQL::Test::Cluster->new('node_b'), $pgbench_scale);
}

TODO: {
    todo_skip 'should compare node contents after join under load', 1;
}

note "Physical join node under write load\n";
join_under_write_load('physical',$node_a, PostgreSQL::Test::Cluster->new('node_c'), $pgbench_scale);

TODO: {
    todo_skip 'should compare node contents after join under load', 1;
}

done_testing();
