#ifndef PG_BDR_COMPAT_H
#define PG_BDR_COMPAT_H

#include "access/heapam.h"
#include "access/genam.h"
#include "common/hashfn.h"
#include "access/htup_details.h"
#include "utils/syscache.h"

/*
 * XXX Should it be table_slot_create for >= 12 instead of
 * ExecInitExtraTupleSlot?
 */
#define ExecInitExtraTupleSlotBdr(estate, a) \
	ExecInitExtraTupleSlot(estate, a, &TTSOpsHeapTuple)

#define pg_analyze_and_rewrite(parsetree, query_string, paramTypes, numParams) \
	pg_analyze_and_rewrite_fixedparams(parsetree, query_string, paramTypes, numParams, NULL)

#define IsKnownTag(tag) (tag != CMDTAG_UNKNOWN)

#define HeapTupleHeaderGetOid(tup) \
( \
	((tup)->t_infomask & HEAP_HASOID_OLD) ? \
		*((Oid *) ((char *)(tup) + (tup)->t_hoff - sizeof(Oid))) \
	: \
		InvalidOid \
)

#define transformAlterTableStmtBdr(relid, astmt, queryString) \
	transformAlterTableStmt(relid, astmt, queryString, &beforeStmts, &afterStmts)

#define TTS_TUP(slot) (((HeapTupleTableSlot *)slot)->tuple)

#define BdrGetSysCacheOid1 GetSysCacheOid1

#define BdrGetSysCacheOid2 GetSysCacheOid2

/* GetSysCacheOid2 equivalent that errors out if nothing is found */
static inline Oid
GetSysCacheOid2Error(int cacheId, AttrNumber oidcol,
					 Datum key1, Datum key2)
{
	Oid			result;

	result = GetSysCacheOid2(cacheId, oidcol, key1, key2);

	if (result == InvalidOid)
		elog(ERROR, "cache lookup failure in cache %d", cacheId);

	return result;
}

#define BdrGetSysCacheOid2Error(cacheId, oidcol, key1, key2) \
	GetSysCacheOid2Error(cacheId, oidcol, key1, key2)

/* 2a10fdc4307a667883f7a3369cb93a721ade9680 */
#define getObjectDescription(object) getObjectDescription(object, false)

/* e997a0c642860a96df0151cbeccfecbdf0450d08 */
#define GetFlushRecPtr() GetFlushRecPtr(NULL)

/* 1a0586de3657cd35581f0639c87d5050c6197bb7 */
#define MakeSingleTupleTableSlot(tupdesc) \
	MakeSingleTupleTableSlot(tupdesc, &TTSOpsHeapTuple)

/* 1281a5c907b41e992a66deb13c3aa61888a62268 */
#define AT_ProcessedConstraint AT_AddConstraint

#endif
