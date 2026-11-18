/* -------------------------------------------------------------------------
 *
 * pgactive_elog.c
 *		pgactive error reporting facility
 *
 * Copyright (C) 2024, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_elog.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pgactive.h"
#include "pgactive_elog.h"

/*
 * Lookup table for pgactive error messages.
 */
const char *const pgactiveErrorMessages[] = {
	[PGACTIVE_ERRCODE_NONE] = "none",
	[PGACTIVE_ERROR_CODE_MAX_NODES_PARAM_MISMATCH] = "pgactive_max_nodes_parameter_mismatch",
	[PGACTIVE_ERROR_CODE_INIT_DUMP_FAILURE] = "pgactive_init_dump_failure",
	[PGACTIVE_ERROR_CODE_INIT_RESTORE_FAILURE] = "pgactive_init_restore_failure",
	[PGACTIVE_ERROR_CODE_PREVIOUS_INIT_FAILURE] = "pgactive_previous_init_failure",
	[PGACTIVE_ERROR_CODE_APPLY_FAILURE] = "pgactive_apply_failure",
};

void
pgactive_set_worker_last_error_info(struct pgactiveWorker *w,
									pgactiveTrackedErrorCodes errcode)
{
	TimestampTz errtime;

	if (w == NULL)
		return;

	Assert(w->worker_type == pgactive_WORKER_PERDB ||
		   w->worker_type == pgactive_WORKER_APPLY);

	errtime = GetCurrentTimestamp();

	SpinLockAcquire(&w->last_error_info.mutex);
	w->last_error_info.errcode = errcode;
	w->last_error_info.errtime = errtime;
	SpinLockRelease(&w->last_error_info.mutex);
}

void
pgactive_reset_worker_last_error_info(struct pgactiveWorker *w)
{
	pgactiveTrackedErrorCodes errcode;

	if (w == NULL)
		return;

	Assert(w->worker_type == pgactive_WORKER_PERDB ||
		   w->worker_type == pgactive_WORKER_APPLY);

	pgactive_get_worker_last_error_info(w, &errcode, NULL);

	if (w->worker_type == pgactive_WORKER_PERDB)
	{
		if (!(errcode >= PGACTIVE_PERDB_WORKER_ERROR_CODE_START &&
			  errcode <= PGACTIVE_PERDB_WORKER_ERROR_CODE_END))
			return;
	}
	else if (w->worker_type == pgactive_WORKER_APPLY)
	{
		if (!(errcode >= PGACTIVE_APPLY_WORKER_ERROR_CODE_START &&
			  errcode <= PGACTIVE_APPLY_WORKER_ERROR_CODE_END))
			return;
	}

	SpinLockAcquire(&w->last_error_info.mutex);
	w->last_error_info.errcode = PGACTIVE_ERRCODE_NONE;
	w->last_error_info.errtime = 0;
	SpinLockRelease(&w->last_error_info.mutex);
}

/*
 * Copy a worker's last error, the code and its time together. Either output
 * pointer can be NULL.
 */
void
pgactive_get_worker_last_error_info(struct pgactiveWorker *w,
									pgactiveTrackedErrorCodes *errcode,
									TimestampTz *errtime)
{
	pgactiveTrackedErrorCodes code;
	TimestampTz time;

	SpinLockAcquire(&w->last_error_info.mutex);
	code = w->last_error_info.errcode;
	time = w->last_error_info.errtime;
	SpinLockRelease(&w->last_error_info.mutex);

	if (errcode != NULL)
		*errcode = code;
	if (errtime != NULL)
		*errtime = time;
}
