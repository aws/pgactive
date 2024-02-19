/* -------------------------------------------------------------------------
 *
 * pgactive_common.c
 *		Utility functions
 *
 * Functions which can be shared between extension and cli
 * (i.e. don't require server side libraries).
 *
 * Copyright (c) 2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_common.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "access/xlogdefs.h"
#include "nodes/pg_list.h"

#include "pgactive_internal.h"

/*
 * Functions taken from src/common/exec.c
 */

/* validate_exec() is provided by core from PG14 */
#if PG_VERSION_NUM < 140000
static int	validate_exec(const char *path);
#endif

/*
 * Format slot name string from node identifiers.
 */
void
pgactive_slot_name(Name slot_name, const pgactiveNodeId * const remote_node, Oid local_dboid)
{
	snprintf(NameStr(*slot_name), NAMEDATALEN, pgactive_SLOT_NAME_FORMAT,
			 local_dboid, remote_node->sysid, remote_node->timeline, remote_node->dboid,
			 EMPTY_REPLICATION_NAME);
}

#if PG_VERSION_NUM < 140000
/*
 * validate_exec -- validate "path" as an executable file
 *
 * returns 0 if the file is found and no error is encountered.
 *		  -1 if the regular file "path" does not exist or cannot be executed.
 *		  -2 if the file is otherwise valid but cannot be read.
 */
static int
validate_exec(const char *path)
{
	struct stat buf;
	int			is_r;
	int			is_x;

#ifdef WIN32
	char		path_exe[MAXPGPATH + sizeof(".exe") - 1];

	/* Win32 requires a .exe suffix for stat() */
	if (strlen(path) >= strlen(".exe") &&
		pg_strcasecmp(path + strlen(path) - strlen(".exe"), ".exe") != 0)
	{
		strlcpy(path_exe, path, sizeof(path_exe) - 4);
		strcat(path_exe, ".exe");
		path = path_exe;
	}
#endif

	/*
	 * Ensure that the file exists and is a regular file.
	 *
	 * XXX if you have a broken system where stat() looks at the symlink
	 * instead of the underlying file, you lose.
	 */
	if (stat(path, &buf) < 0)
		return -1;

	if (!S_ISREG(buf.st_mode))
		return -1;

	/*
	 * Ensure that the file is both executable and readable (required for
	 * dynamic loading).
	 */
#ifndef WIN32
	is_r = (access(path, R_OK) == 0);
	is_x = (access(path, X_OK) == 0);
#else
	is_r = buf.st_mode & S_IRUSR;
	is_x = buf.st_mode & S_IXUSR;
#endif
	return is_x ? (is_r ? 0 : -2) : -1;
}
#endif

/*
 * Although pipe_read_line() is provided by core for external modules starting
 * from PG13, the way input parameters are sent to it has changed since PG17.
 * Instead of dealing with all of these at call sites which makes code
 * unreadable, let's maintain our own version of the function.
 */
static char *
pipe_read_line_v2(char *cmd, char *line, int maxsize)
{
	FILE	   *pgver;

	/* flush output buffers in case popen does not... */
	fflush(stdout);
	fflush(stderr);

	errno = 0;
	if ((pgver = popen(cmd, "r")) == NULL)
	{
		perror("popen failure");
		return NULL;
	}

	errno = 0;
	if (fgets(line, maxsize, pgver) == NULL)
	{
		if (feof(pgver))
			fprintf(stderr, "no data was returned by command \"%s\"\n", cmd);
		else
			perror("fgets failure");
		pclose(pgver);			/* no error checking */
		return NULL;
	}

	if (pclose_check(pgver))
		return NULL;

	return line;
}

/*
 * Find another program in our binary's directory,
 * then make sure it is the proper version.
 *
 * pgactive modified version of core's find_other_exec() - returns computed major
 * version number.
 */
int
pgactive_find_other_exec(const char *argv0, const char *target,
						 uint32 *version, char *retpath)
{
	char		cmd[MAXPGPATH];
	char		line[100];
	int			pre_dot;

	if (find_my_exec(argv0, retpath) < 0)
		return -1;

	/* Trim off program name and keep just directory */
	*last_dir_separator(retpath) = '\0';
	canonicalize_path(retpath);

	/* Now append the other program's name */
	snprintf(retpath + strlen(retpath), MAXPGPATH - strlen(retpath),
			 "/%s%s", target, EXE);

	if (validate_exec(retpath) != 0)
		return -1;

	snprintf(cmd, sizeof(cmd), "\"%s\" -V", retpath);

	if (!pipe_read_line_v2(cmd, line, sizeof(line)))
		return -1;

	if (sscanf(line, "%*s %*s %d", &pre_dot) != 1)
		return -2;

	*version = pre_dot * 10000;

	return 0;
}

/*
 * Create a new unique node identifier.
 *
 * See notes in xlog.c about the algorithm.
 */
uint64
GenerateNodeIdentifier(void)
{
	uint64		nid;
	struct timeval tv;

	gettimeofday(&tv, NULL);
	nid = ((uint64) tv.tv_sec) << 32;
	nid |= ((uint64) tv.tv_usec) << 12;
	nid |= getpid() & 0xFFF;

	return nid;
}
