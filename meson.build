project(
  'pgactive',
  'c',
  version: '2.1.7',
  license: 'Apache-2.0',
  meson_version: '>= 0.57.0',
  default_options: [
    'c_std=gnu99',
    'warning_level=2',
  ],
)

# PostgreSQL server headers require POSIX/GNU extensions
add_project_arguments('-D_GNU_SOURCE', language: 'c')

# ---- PostgreSQL detection ----
pg_config = find_program(
  get_option('pg_config'),
  required: true,
)

pg_bindir             = run_command(pg_config, '--bindir',            check: true).stdout().strip()
pg_includedir         = run_command(pg_config, '--includedir',        check: true).stdout().strip()
pg_includedir_server  = run_command(pg_config, '--includedir-server', check: true).stdout().strip()
pg_pkglibdir          = run_command(pg_config, '--pkglibdir',         check: true).stdout().strip()
pg_sharedir    = run_command(pg_config, '--sharedir',          check: true).stdout().strip()
pg_libdir      = run_command(pg_config, '--libdir',            check: true).stdout().strip()
pg_pgxs        = run_command(pg_config, '--pgxs',              check: true).stdout().strip()
pg_version     = run_command(pg_config, '--version',           check: true).stdout().strip()

# Extract major version number (e.g. "PostgreSQL 18.0" → "18", "PostgreSQL 19devel" → "19",
# "PostgreSQL 19beta1" → "19"). Matches configure.in logic: strip non-digits, take first 2.
pg_major = run_command('sh', '-c',
  'echo "$1" | sed "s/[^0-9]//g" | cut -c 1-2',
  'sh', pg_version.split(' ')[1],
  check: true).stdout().strip()
message('Building against PostgreSQL @0@'.format(pg_major))

pg_inc = include_directories(pg_includedir, pg_includedir_server)
pg_inc_internal = include_directories(pg_includedir / 'postgresql' / 'internal')

# pgxs directory (for test perl modules and pg_regress)
pgxs_dir = run_command('dirname', pg_pgxs, check: true).stdout().strip()
pgxs_basedir = run_command('dirname', pgxs_dir, check: true).stdout().strip()

# libpq
libpq = dependency('libpq', required: false)
if not libpq.found()
  libpq = declare_dependency(
    include_directories: include_directories(
      run_command(pg_config, '--includedir', check: true).stdout().strip(),
    ),
    dependencies: [
      meson.get_compiler('c').find_library('pq',
        dirs: [pg_libdir],
      ),
    ],
  )
endif

# pgfeutils (needed by pgactive_dump)
cc = meson.get_compiler('c')
libpgfeutils = cc.find_library('pgfeutils', dirs: [pg_libdir], required: false)
libpgcommon  = cc.find_library('pgcommon',  dirs: [pg_libdir], required: false)
libpgport    = cc.find_library('pgport',    dirs: [pg_libdir], required: false)

# ---- Version header generation ----
git = find_program('git', required: false)
if git.found()
  git_hash = run_command(
    git, 'rev-parse', '--short', 'HEAD',
    check: false,
  ).stdout().strip()
else
  git_hash = 'unknown'
endif

version_date = run_command('date', '+%Y-%m-%d', check: true).stdout().strip()

version_h = configure_file(
  input: 'include/pgactive_version.h.in',
  output: 'pgactive_version.h',
  configuration: {
    'pgactive_VERSION_DATE':    '"@0@"'.format(version_date),
    'pgactive_VERSION_GITHASH': '"@0@"'.format(git_hash),
  },
)

# ---- Config header generation (pgactive_config_generated.h) ----
cdata = configuration_data()

cdata.set('HAVE_ACCESS_COMMITTS_H',
  cc.has_header('access/committs.h', include_directories: pg_inc) ? 1 : false)
cdata.set('HAVE_ACCESS_COMMIT_TS_H',
  cc.has_header('access/commit_ts.h', include_directories: pg_inc) ? 1 : false)
cdata.set('HAVE_REPLICATION_ORIGIN_H',
  cc.has_header('replication/origin.h', include_directories: pg_inc) ? 1 : false)
cdata.set('HAVE_REPLICATION_REPLICATION_IDENTIFIER_H',
  cc.has_header('replication/replication_identifier.h', include_directories: pg_inc) ? 1 : false)

cdata.set_quoted('PACKAGE_BUGREPORT', '')
cdata.set_quoted('PACKAGE_NAME', 'pgactive')
cdata.set_quoted('PACKAGE_STRING', 'pgactive ' + meson.project_version())
cdata.set_quoted('PACKAGE_TARNAME', 'pgactive')
cdata.set_quoted('PACKAGE_URL', '')
cdata.set_quoted('PACKAGE_VERSION', meson.project_version())

configure_file(
  input: 'include/pgactive_config_generated.h.in',
  output: 'pgactive_config_generated.h',
  configuration: cdata,
)

# ---- Include directories ----
# '.' adds both source root and build root (where generated headers land)
pgactive_inc = include_directories(
  '.',
  'include',
  'src/compat/@0@'.format(pg_major),
)

# ---- Shared library: pgactive.so ----
pgactive_sources = files(
  'src/pgactive.c',
  'src/pgactive_apply.c',
  'src/pgactive_catalogs.c',
  'src/pgactive_commandfilter.c',
  'src/pgactive_common.c',
  'src/pgactive_conflict_handlers.c',
  'src/pgactive_conflict_logging.c',
  'src/pgactive_count.c',
  'src/pgactive_dbcache.c',
  'src/pgactive_ddlrep.c',
  'src/pgactive_ddlrep_truncate.c',
  'src/pgactive_elog.c',
  'src/pgactive_executor.c',
  'src/pgactive_init_replica.c',
  'src/pgactive_locks.c',
  'src/pgactive_messaging.c',
  'src/pgactive_monitoring.c',
  'src/pgactive_node_identifier.c',
  'src/pgactive_nodecache.c',
  'src/pgactive_output.c',
  'src/pgactive_perdb.c',
  'src/pgactive_protocol.c',
  'src/pgactive_relcache.c',
  'src/pgactive_remotecalls.c',
  'src/pgactive_seq.c',
  'src/pgactive_shmem.c',
  'src/pgactive_supervisor.c',
  'src/pgactive_user_mapping.c',
)

# OpenSSL and compression libs (needed by server headers and pgactive_dump)
libssl = dependency('openssl', required: false)
libz = dependency('zlib', required: false)
liblz4 = dependency('liblz4', required: false)
libzstd = dependency('libzstd', required: false)

# On macOS, shared modules loaded into postgres need -bundle_loader
pg_backend = pg_bindir / 'postgres'
pgactive_link_args = []
if host_machine.system() == 'darwin'
  pgactive_link_args += ['-bundle_loader', pg_backend]
endif

pgactive_lib = shared_module(
  'pgactive',
  pgactive_sources,
  include_directories: [pg_inc, pgactive_inc],
  dependencies: [libpq, libssl],
  c_args: ['-DMODULE_PATHNAME="$libdir/pgactive"'],
  link_args: pgactive_link_args,
  name_prefix: '',
  install: true,
  install_dir: pg_pkglibdir,
)

# ---- pgactive_init_copy binary ----
pgactive_init_copy = executable(
  'pgactive_init_copy',
  files(
    'src/pgactive_init_copy.c',
    'src/pgactive_common.c',
  ),
  include_directories: [pg_inc, pg_inc_internal, pgactive_inc],
  dependencies: [libpq, libpgport, libpgcommon],
  install: true,
  install_dir: pg_bindir,
)

# ---- pgactive_dump binary ----
dump_dir = 'src/compat/@0@/pg_dump'.format(pg_major)

pgactive_dump_sources = files(
  dump_dir / 'pg_dump.c',
  dump_dir / 'common.c',
  dump_dir / 'pg_dump_sort.c',
  dump_dir / 'pg_backup_archiver.c',
  dump_dir / 'pg_backup_db.c',
  dump_dir / 'pg_backup_custom.c',
  dump_dir / 'pg_backup_null.c',
  dump_dir / 'pg_backup_tar.c',
  dump_dir / 'pg_backup_directory.c',
  dump_dir / 'pg_backup_utils.c',
  dump_dir / 'parallel.c',
  dump_dir / 'dumputils.c',
  dump_dir / 'compress_io.c',
  dump_dir / 'string_utils.c',
)

# Conditional sources (PG >= 16)
foreach f : ['compress_lz4.c', 'compress_gzip.c', 'compress_none.c', 'compress_zstd.c', 'filter.c']
  p = dump_dir / f
  if run_command('test', '-f', p, check: false).returncode() == 0
    pgactive_dump_sources += files(p)
  endif
endforeach

# connectdb.c (PG >= 18)
connectdb_path = dump_dir / 'connectdb.c'
if run_command('test', '-f', connectdb_path, check: false).returncode() == 0
  pgactive_dump_sources += files(connectdb_path)
endif

pgactive_dump_inc = include_directories(dump_dir)


pgactive_dump = executable(
  'pgactive_dump',
  pgactive_dump_sources,
  include_directories: [pg_inc, pg_inc_internal, pgactive_inc, pgactive_dump_inc],
  dependencies: [libpq, libpgfeutils, libpgport, libpgcommon, libssl, libz, liblz4, libzstd],
  install: true,
  install_dir: pg_bindir,
)

# ---- Extension data files ----
install_data(
  'pgactive.control',
  install_dir: pg_sharedir / 'extension',
)

sql_files = run_command(
  'find', '.', '-maxdepth', '1', '-name', 'pgactive--*.sql',
  check: true,
).stdout().strip().split('\n')

foreach f : sql_files
  install_data(f, install_dir: pg_sharedir / 'extension')
endforeach


# =====================================================================
# Tests
# =====================================================================
if get_option('tests')

  pg_regress_bin = pg_pgxs.replace('/pgxs/src/makefiles/pgxs.mk',
                                   '/pgxs/src/test/regress/pg_regress')

  prove = find_program('prove', required: false)

  # GNU getopt is required by run_tests script (macOS ships BSD getopt)
  gnu_getopt = find_program('getopt', dirs: ['/opt/homebrew/opt/gnu-getopt/bin', '/usr/local/opt/gnu-getopt/bin'], required: false)
  getopt_dir = ''
  if gnu_getopt.found()
    getopt_dir = run_command('dirname', gnu_getopt.full_path(), check: true).stdout().strip() + ':'
  endif

  # Common env for both test targets
  test_env_base = {
    'PATH':       getopt_dir + pg_bindir + ':' + run_command('sh', '-c', 'echo $PATH', check: true).stdout().strip(),
    'PG_CONFIG':  pg_config.full_path(),
    'PG_REGRESS': pg_regress_bin,
  }

  # ------------------------------------------------------------------
  # regress_check  —  pg_regress based SQL regression tests
  #
  # Mirrors:  make regress_check
  #   ./test/run_tests --config test/pgactive_regress.conf \
  #       --testbinary src/test/regress/pg_regress <TESTS>
  # ------------------------------------------------------------------

  # DDL tests (transparent DDL replication)
  ddl_tests = [
    'ddl_enable_ddl',
    'ddl_create',
    'ddl_alter_table',
    'ddl_extension',
    'ddl_function',
    'ddl_grant',
    'ddl_mixed',
    'ddl_namespace',
    'ddl_sequence',
    'ddl_view',
    'ddl_disable_ddl',
  ]

  # DDL tests via pgactive_replicate_ddl_command wrapper
  ddl_fn_tests = []
  foreach t : ddl_tests
    ddl_fn_tests += 'ddl_fn/' + t
  endforeach

  regress_tests = [
    'init',
    'init_pgactive',
    'guc',
    'schema_desync',
    'seq',
    'ddl_lock',
  ] + ddl_tests + ddl_fn_tests + [
    'dml_basic',
    'dml_contrib',
    'dml_delete_pk',
    'dml_extended',
    'dml_replicate_ddl_command',
    'dml_missing_pk',
    'dml_toasted',
    'detach_pgactive',
  ]

  # Build the run_tests wrapper from run_tests.in
  run_tests_script = configure_file(
    input: 'test/run_tests.in',
    output: 'run_tests',
    configuration: {
      'PG_CONFIG': pg_config.full_path(),
      'srcdir':    meson.current_source_dir(),
      'abs_srcdir': meson.current_source_dir(),
      'pgactive_PG_MAJORVERSION': pg_major,
    },
  )

  # regress_check: uses the run_tests wrapper just like the Makefile
  test(
    'regress_check',
    find_program('bash'),
    args: [
      run_tests_script,
      '--config', meson.current_source_dir() / 'test' / 'pgactive_regress.conf',
      '--testbinary', 'src/test/regress/pg_regress',
    ] + regress_tests,
    env: test_env_base,
    depends: [pgactive_lib, pgactive_init_copy, pgactive_dump],
    timeout: 1200,
    is_parallel: false,
    suite: 'regress',
  )

  # ------------------------------------------------------------------
  # prove_check  —  Perl TAP tests
  #
  # Mirrors:  make prove_check
  #   cd $(srcdir) && prove test/t/*.pl
  # ------------------------------------------------------------------
  if prove.found()
    # Determine the pgxs test perl dir for PostgreSQL::Test::Cluster etc.
    pgxs_test_perl = pgxs_basedir / 'src' / 'test' / 'perl'

    prove_env = test_env_base
    prove_env += {
      'TESTDATADIR':  meson.current_build_dir() / 'tmp_check',
      'TESTLOGDIR':   meson.current_build_dir() / 'tmp_check' / 'log',
      'PGPORT':       '65432',
      'top_builddir': meson.current_build_dir(),
      'srcdir':       meson.current_source_dir(),
      # Ensure perl can find PostgreSQL::Test::Cluster
      'PERL5LIB':     pgxs_test_perl,
    }

    test(
      'prove_check',
      prove,
      args: [
        '--verbose',
        '--timer',
        meson.current_source_dir() / 'test' / 't',
      ],
      env: prove_env,
      depends: [pgactive_lib, pgactive_init_copy, pgactive_dump],
      timeout: 1800,
      is_parallel: false,
      suite: 'tap',
    )
  endif

endif
