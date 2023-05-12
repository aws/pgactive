#ifndef PG_BDR_COMPAT_H
#define PG_BDR_COMPAT_H

#include "access/xlogrecovery.h"
#include "access/heapam.h"
#include "access/genam.h"
#include "common/hashfn.h"
#include "access/htup_details.h"

static inline TimeLineID
GetTimeLineID (void)
{
	TimeLineID current_timeline;

	if (RecoveryInProgress())
		(void) GetXLogReplayRecPtr(&current_timeline);
		else
			current_timeline = GetWALInsertionTimeLine();

	return current_timeline;
}

#define BDR_LOCALID_FORMAT_ARGS \
	GetSystemIdentifier(), GetTimeLineID(), MyDatabaseId, EMPTY_REPLICATION_NAME

//XXX should that be table_slot_create for >= 12 instead?
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

#define BdrGetSysCacheOid GetSysCacheOid

#define BdrGetSysCacheOid1 GetSysCacheOid1

#define BdrGetSysCacheOid2 GetSysCacheOid2

#define GetSysCacheOidError2(cacheId, oidcol, key1, key2) \
	GetSysCacheOidError(cacheId, oidcol, key1, key2, 0, 0)

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
