#ifndef PG_pgactive_COMPAT_H
#define PG_pgactive_COMPAT_H

#include "access/heapam.h"
#include "access/genam.h"
#include "catalog/pg_namespace.h"
#include "common/hashfn.h"
#include "access/htup_details.h"
#include "utils/syscache.h"

/*
 * XXX Should it be table_slot_create for >= 12 instead of
 * ExecInitExtraTupleSlot?
 */
#define ExecInitExtraTupleSlotpgactive(estate, a) \
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

#define transformAlterTableStmtpgactive(relid, astmt, queryString) \
	transformAlterTableStmt(relid, astmt, queryString, &beforeStmts, &afterStmts)

#define TTS_TUP(slot) (((HeapTupleTableSlot *)slot)->tuple)

#define pgactiveGetSysCacheOid1 GetSysCacheOid1

#define pgactiveGetSysCacheOid2 GetSysCacheOid2

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

#define pgactiveGetSysCacheOid2Error(cacheId, oidcol, key1, key2) \
	GetSysCacheOid2Error(cacheId, oidcol, key1, key2)

/* 2a10fdc4307a667883f7a3369cb93a721ade9680 */
#define getObjectDescription(object) getObjectDescription(object, false)

/* e997a0c642860a96df0151cbeccfecbdf0450d08 */
#define GetFlushRecPtr() GetFlushRecPtr(NULL)

/* 1a0586de3657cd35581f0639c87d5050c6197bb7 */
#define MakeSingleTupleTableSlot(tupdesc) \
	MakeSingleTupleTableSlot(tupdesc, &TTSOpsHeapTuple)

#define pg_class_ownercheck(objectid, roleid) \
	object_ownercheck(RelationRelationId, objectid, roleid)

#define pg_database_ownercheck(objectid, roleid) \
	object_ownercheck(DatabaseRelationId, objectid, roleid)

#define pg_namespace_ownercheck(objectid, roleid) \
	object_ownercheck(NamespaceRelationId, objectid, roleid)

#define pg_foreign_server_aclcheck(objectid, roleid, mode) \
	object_aclcheck(ForeignServerRelationId, objectid, roleid, mode)

#endif
