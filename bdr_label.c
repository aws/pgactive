/* -------------------------------------------------------------------------
 *
 * bdr_label.c
 *		BDR security label implementation
 *
 * Provide object metadata for bdr using the security label
 * infrastructure.
 *
 * Copyright (c) 2014-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		bdr_label.c
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include "bdr.h"
#include "bdr_label.h"

#include "catalog/pg_class.h"
#include "catalog/pg_database.h"
#include "commands/dbcommands.h"
#include "commands/seclabel.h"
#include "miscadmin.h"
#include "utils/acl.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"

static void bdr_object_relabel(const ObjectAddress *object, const char *seclabel);

/*
 * Needs to call at postmaster init (or backend init for EXEC_BACKEND).
 */
void
bdr_label_init(void)
{
	/* Security label provider hook */
	register_label_provider(BDR_SECLABEL_PROVIDER, bdr_object_relabel);
}

static void
bdr_object_relabel(const ObjectAddress *object, const char *seclabel)
{
	switch (object->classId)
	{
		case RelationRelationId:

			if (!pg_class_ownercheck(object->objectId, GetUserId()))
				aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_TABLE,
							   get_rel_name(object->objectId));

			/* ensure bdr_relcache.c is coherent */
			CacheInvalidateRelcacheByRelid(object->objectId);

			bdr_parse_relation_options(seclabel, NULL);
			break;
		case DatabaseRelationId:

			if (!pg_database_ownercheck(object->objectId, GetUserId()))
				aclcheck_error(ACLCHECK_NOT_OWNER, ACL_ALL_RIGHTS_DATABASE,
							   get_database_name(object->objectId));

			/* ensure bdr_dbcache.c is coherent */
			CacheInvalidateCatalog(DatabaseRelationId);

			bdr_parse_database_options(seclabel, NULL);
			break;
		default:
			elog(ERROR, "unsupported object type: %s",
				 getObjectDescription(object));
			break;
	}
}
