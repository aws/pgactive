/* -------------------------------------------------------------------------
 *
 * bdr_common.c
 *		Utility functions
 *
 * Functions which can be shared between extension and cli
 * (i.e. don't require server side libraries).
 *
 * Copyright (c) 2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		bdr_common.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/xlogdefs.h"
#include "nodes/pg_list.h"

#include "bdr_internal.h"

/*
 * Format slot name string from node identifiers.
 */
void
bdr_slot_name(Name slot_name, const BDRNodeId * const remote_node, Oid local_dboid)
{
	snprintf(NameStr(*slot_name), NAMEDATALEN, BDR_SLOT_NAME_FORMAT,
			 local_dboid, remote_node->sysid, remote_node->timeline, remote_node->dboid,
			 EMPTY_REPLICATION_NAME);
	NameStr(*slot_name)[NAMEDATALEN - 1] = '\0';
}
