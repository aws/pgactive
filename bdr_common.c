/* -------------------------------------------------------------------------
 *
 * bdr_common.c
 *		Utility functions
 *
 * Functions which can be shared between extension and cli
 * (i.e. don't require server side libraries).
 *
 * Copyright (c) 2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		bdr_common.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "access/xlogdefs.h"
#include "nodes/pg_list.h"

#include "bdr_internal.h"

/*
 * Functions taken from src/common/exec.c
 */

/* validate_exec() is provided by core from PG14 */
#if PG_VERSION_NUM < 140000
static int	validate_exec(const char *path);
#endif

/* pipe_read_line() is provided by core from PG13 */
#if PG_VERSION_NUM < 130000
static char *pipe_read_line(char *cmd, char *line, int maxsize);
#endif

/*
 * Format slot name string from node identifiers.
 */
void
bdr_slot_name(Name slot_name, const BDRNodeId * const remote_node, Oid local_dboid)
{
	snprintf(NameStr(*slot_name), NAMEDATALEN, BDR_SLOT_NAME_FORMAT,
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

#if PG_VERSION_NUM < 130000
/*
 * The runtime library's popen() on win32 does not work when being
 * called from a service when running on windows <= 2000, because
 * there is no stdin/stdout/stderr.
 *
 * Executing a command in a pipe and reading the first line from it
 * is all we need.
 */
static char *
pipe_read_line(char *cmd, char *line, int maxsize)
{
#ifndef WIN32
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
#else							/* WIN32 */

	SECURITY_ATTRIBUTES sattr;
	HANDLE		childstdoutrd,
				childstdoutwr,
				childstdoutrddup;
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	char	   *retval = NULL;

	sattr.nLength = sizeof(SECURITY_ATTRIBUTES);
	sattr.bInheritHandle = TRUE;
	sattr.lpSecurityDescriptor = NULL;

	if (!CreatePipe(&childstdoutrd, &childstdoutwr, &sattr, 0))
		return NULL;

	if (!DuplicateHandle(GetCurrentProcess(),
						 childstdoutrd,
						 GetCurrentProcess(),
						 &childstdoutrddup,
						 0,
						 FALSE,
						 DUPLICATE_SAME_ACCESS))
	{
		CloseHandle(childstdoutrd);
		CloseHandle(childstdoutwr);
		return NULL;
	}

	CloseHandle(childstdoutrd);

	ZeroMemory(&pi, sizeof(pi));
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdError = childstdoutwr;
	si.hStdOutput = childstdoutwr;
	si.hStdInput = INVALID_HANDLE_VALUE;

	if (CreateProcess(NULL,
					  cmd,
					  NULL,
					  NULL,
					  TRUE,
					  0,
					  NULL,
					  NULL,
					  &si,
					  &pi))
	{
		/* Successfully started the process */
		char	   *lineptr;

		ZeroMemory(line, maxsize);

		/* Try to read at least one line from the pipe */
		/* This may require more than one wait/read attempt */
		for (lineptr = line; lineptr < line + maxsize - 1;)
		{
			DWORD		bytesread = 0;

			/* Let's see if we can read */
			if (WaitForSingleObject(childstdoutrddup, 10000) != WAIT_OBJECT_0)
				break;			/* Timeout, but perhaps we got a line already */

			if (!ReadFile(childstdoutrddup, lineptr, maxsize - (lineptr - line),
						  &bytesread, NULL))
				break;			/* Error, but perhaps we got a line already */

			lineptr += strlen(lineptr);

			if (!bytesread)
				break;			/* EOF */

			if (strchr(line, '\n'))
				break;			/* One or more lines read */
		}

		if (lineptr != line)
		{
			/* OK, we read some data */
			int			len;

			/* If we got more than one line, cut off after the first \n */
			lineptr = strchr(line, '\n');
			if (lineptr)
				*(lineptr + 1) = '\0';

			len = strlen(line);

			/*
			 * If EOL is \r\n, convert to just \n. Because stdout is a
			 * text-mode stream, the \n output by the child process is
			 * received as \r\n, so we convert it to \n.  The server main.c
			 * sets setvbuf(stdout, NULL, _IONBF, 0) which has the effect of
			 * disabling \n to \r\n expansion for stdout.
			 */
			if (len >= 2 && line[len - 2] == '\r' && line[len - 1] == '\n')
			{
				line[len - 2] = '\n';
				line[len - 1] = '\0';
				len--;
			}

			/*
			 * We emulate fgets() behaviour. So if there is no newline at the
			 * end, we add one...
			 */
			if (len == 0 || line[len - 1] != '\n')
				strcat(line, "\n");

			retval = line;
		}

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}

	CloseHandle(childstdoutwr);
	CloseHandle(childstdoutrddup);

	return retval;
#endif							/* WIN32 */
}
#endif

/*
 * Find another program in our binary's directory,
 * then make sure it is the proper version.
 *
 * BDR modified version of core's find_other_exec() - returns computed major
 * version number.
 */
int
bdr_find_other_exec(const char *argv0, const char *target,
					uint32 *version, char *retpath)
{
	char		cmd[MAXPGPATH];
	char		line[100];
	int			pre_dot,
				post_dot;

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

	if (!pipe_read_line(cmd, line, sizeof(line)))
		return -1;

	if (sscanf(line, "%*s %*s %d.%d", &pre_dot, &post_dot) != 2)
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
