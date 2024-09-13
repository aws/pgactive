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
	[PGACTIVE_ERROR_CODE_APPLY_FAILURE] = "pgactive_apply_failure",
};

void
pgactive_set_worker_last_error_info(struct pgactiveWorker *w,
									pgactiveTrackedErrorCodes errcode)
{
	if (w == NULL)
		return;

	Assert(w->worker_type == pgactive_WORKER_PERDB ||
		   w->worker_type == pgactive_WORKER_APPLY);

	w->last_error_info.errcode = errcode;
	w->last_error_info.errtime = GetCurrentTimestamp();
}

void
pgactive_reset_worker_last_error_info(struct pgactiveWorker *w)
{
	int			errcode;

	if (w == NULL)
		return;

	Assert(w->worker_type == pgactive_WORKER_PERDB ||
		   w->worker_type == pgactive_WORKER_APPLY);

	errcode = w->last_error_info.errcode;

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

	w->last_error_info.errcode = PGACTIVE_ERRCODE_NONE;
	w->last_error_info.errtime = 0;
}
