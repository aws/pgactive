/*-------------------------------------------------------------------------
 *
 * bdr_pgutils.c
 *		This files is used as common place for function that we took
 *		verbatim from PostgreSQL because they are declared as static and
 *		we can't use them as API.
 *		Also the internal API function that use those static functions
 *		be defined here and should start with bdr_ prefix.
 *
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  bdr_pgutils.c
 *
 *-------------------------------------------------------------------------
 */

#ifndef FRONTEND
#include "postgres.h"
#else
#include "postgres_fe.h"
#endif

#include <sys/stat.h>
#include <unistd.h>

#include "access/xlogdefs.h"
#include "nodes/pg_list.h"

#include "bdr_internal.h"

#ifndef FRONTEND
#define log_error4(str, param, arg1)	elog(LOG, str, param, arg1)
#else
#define log_error4(str, param, arg1)	(fprintf(stderr, str, param, arg1), fputc('\n', stderr))
#endif

/*
 * Start function taken from src/common/exec.c
 */

static int	validate_exec(const char *path);
static char *pipe_read_line(char *cmd, char *line, int maxsize);

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


/*
 * Find another program in our binary's directory,
 * then make sure it is the proper version.
 *
 * BDR modified version - returns computed major version number
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

	if (pre_dot < 10)
	{
		/* old style, e.g. 9.6.1 */
		*version = pre_dot * 10000 + post_dot * 100;
	}
	else
	{
		/* new style, e.g. 10.1 */
		*version = pre_dot * 10000;
	}

	return 0;
}


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
#endif   /* WIN32 */
}

/*
 * End function taken from src/common/exec.c
 */
