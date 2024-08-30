/* -------------------------------------------------------------------------
 *
 * pgactive_elog.h
 *		pgactive error reporting facility
 *
 * Copyright (C) 2024, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_elog.h
 *
 * -------------------------------------------------------------------------
 */
#ifndef pgactive_ELOG_H
#define pgactive_ELOG_H

#include "datatype/timestamp.h"

/*
 * Define pgactive error codes for which last error info needs to be tracked.
 * When adding new error code, remember to add corresponding entry in
 * pgactiveErrorMessages.
 *
 * NB: If ever changing start and end limits for one worker, adjust other
 * workers' start and end limits.
 */

#define PGACTIVE_TRACKED_ERRORS_CHUNK 64
typedef enum pgactiveTrackedErrorCodes
{
	PGACTIVE_ERRCODE_NONE = 0,

	/* perdb worker error codes start --> */
	PGACTIVE_PERDB_WORKER_ERROR_CODE_START = 1,
	PGACTIVE_ERROR_CODE_MAX_NODES_PARAM_MISMATCH = 2,

	/* add new perdb worker error codes here */

	PGACTIVE_PERDB_WORKER_ERROR_CODE_END = PGACTIVE_TRACKED_ERRORS_CHUNK,
	/* <-- perdb worker error codes end */

	/* apply worker error codes start --> */
	PGACTIVE_APPLY_WORKER_ERROR_CODE_START = PGACTIVE_TRACKED_ERRORS_CHUNK + 1,
	PGACTIVE_ERROR_CODE_APPLY_FAILURE,

	/* add new apply worker error codes here */

	PGACTIVE_APPLY_WORKER_ERROR_CODE_END = 2 * PGACTIVE_TRACKED_ERRORS_CHUNK,
	/* <-- apply worker error codes end */
}			pgactiveTrackedErrorCodes;

extern PGDLLIMPORT const char *const pgactiveErrorMessages[];

typedef struct pgactiveLastErrorInfo
{
	pgactiveTrackedErrorCodes errcode;
	TimestampTz errtime;
}			pgactiveLastErrorInfo;

#define GET_FIRST_ARG(arg1, ...) arg1
#define GET_REST_ARGS(arg1, ...) __VA_ARGS__

/*
 * Log the pgactive error message. Either call with pgactiveTrackedErrorCodes:
 *
 * ereport_pgactive(ERROR,
 *                  PGACTIVE_ERRCODE_XXX,
 *                  errmsg("...."));
 *
 * or just call:
 *
 * ereport_pgactive(ERROR,
 *                  errmsg("...."));
 */
#define ereport_pgactive(elevel, ...) \
do { \
	pgactive_set_worker_last_error_info(pgactive_worker_slot, \
										GET_FIRST_ARG(__VA_ARGS__, 0)); \
	ereport(elevel, GET_REST_ARGS(__VA_ARGS__)); \
} while(0)

/* Forward declaration */
struct pgactiveWorker;

extern void pgactive_set_worker_last_error_info(struct pgactiveWorker *w,
												pgactiveTrackedErrorCodes errcode);
extern void pgactive_reset_worker_last_error_info(struct pgactiveWorker *w);

#endif							/* pgactive_ELOG_H */
