/* -------------------------------------------------------------------------
 *
 * pgactive_compat.c
 *		Checks for cross version/arch/settings compatibility
 *
 * Copyright (C) 2014-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_compat.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pgactive.h"

bool
pgactive_get_float4byval(void)
{
#ifdef USE_FLOAT4_BYVAL
	return true;
#else
	return false;
#endif
}

bool
pgactive_get_float8byval(void)
{
#ifdef USE_FLOAT8_BYVAL
	return true;
#else
	return false;
#endif
}

bool
pgactive_get_integer_timestamps(void)
{
#ifdef USE_INTEGER_DATETIMES
	return true;
#else
	return false;
#endif
}

bool
pgactive_get_bigendian(void)
{
#ifdef WORDS_BIGENDIAN
	return true;
#else
	return false;
#endif
}
