#ifndef PG_BDR_COMPAT_H
#define PG_BDR_COMPAT_H

#include "utils/syscache.h"

#define ExecInitExtraTupleSlotBdr(estate, a) \
	ExecInitExtraTupleSlot(estate, a)

#define pg_analyze_and_rewrite(parsetree, query_string, paramTypes, numParams) \
	pg_analyze_and_rewrite(parsetree, query_string, paramTypes, numParams, NULL)

#define IsKnownTag(tag) (tag != NULL)

#define GetCommandTagName(tag) tag

#define transformAlterTableStmtBdr(relid, astmt, queryString) \
	transformAlterTableStmt(relid, astmt, queryString)

#define TTS_TUP(slot) (slot->tts_tuple)

#define BdrGetSysCacheOid1(cacheId, oidcol, key1) \
	GetSysCacheOid1(cacheId, key1)

#define BdrGetSysCacheOid2(cacheId, oidcol, key1, key2) \
	GetSysCacheOid2(cacheId, key1, key2)

/* GetSysCacheOid2 equivalent that errors out if nothing is found */
static inline  Oid
GetSysCacheOid2Error(int cacheId, Datum key1, Datum key2)
{
	Oid			result;

	result = GetSysCacheOid2(cacheId, key1, key2);

	if (result == InvalidOid)
		elog(ERROR, "cache lookup failure in cache %d", cacheId);

	return result;
}

#define BdrGetSysCacheOid2Error(cacheId, oidcol, key1, key2) \
	GetSysCacheOid2Error(cacheId, key1, key2)

/* deprecated in PG12, removed in PG13 */
#define table_open(r, l)        heap_open(r, l)
#define table_openrv(r, l)      heap_openrv(r, l)
#define table_openrv_extended(r, l, m)  heap_openrv_extended(r, l, m)
#define table_close(r, l)       heap_close(r, l)

/* 29c94e03c7 */
#define ExecStoreHeapTuple(tuple, slot, shouldFree) ExecStoreTuple(tuple, slot, InvalidBuffer, shouldFree)

/* 2f9661311b83 */
#define CommandTag const char *

/* 6aba63ef3e60 */
#define pg_plan_queries(querytrees, query_string, cursorOptions, boundParams) \
	pg_plan_queries(querytrees, cursorOptions, boundParams)

/* 2d7d946cd323 */
#define IsCatalogNamespace(ns) IsSystemNamespace(ns)

/* 763f2edd9209 */
#define ExecFetchSlotHeapTupleDatum(slot) ExecFetchSlotTupleDatum(slot)

/* Postgres commit 6f6f284c7ee4 introduced this macro in version 14. */
/*
 * Handy macro for printing XLogRecPtr in conventional format, e.g.,
 *
 * printf("%X/%X", LSN_FORMAT_ARGS(lsn));
 */
#define LSN_FORMAT_ARGS(lsn) (AssertVariableIsOfTypeMacro((lsn), XLogRecPtr), (uint32) ((lsn) >> 32)), ((uint32) (lsn))
#endif
