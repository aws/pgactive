/*
 * bdr_internal.h
 *
 * BiDirectionalReplication
 *
 * Copyright (c) 2012-2015, PostgreSQL Global Development Group
 *
 * bdr_internal.h must be #include-able from FRONTEND code, so it may not
 * reference elog, List, etc.
 */
#ifndef BDR_INTERNAL_H
#define BDR_INTERNAL_H

#include <signal.h>
#include "access/xlogdefs.h"

#define EMPTY_REPLICATION_NAME ""

/*
 * The format used for slot names
 *
 * params: local_dboid, remote_sysid, remote_timeline, remote_dboid, replname
 */
#define BDR_SLOT_NAME_FORMAT "bdr_%u_"UINT64_FORMAT"_%u_%u__%s"

/*
 * The format used for replication identifiers (riident, replident)
 *
 * params: remote_sysid, remote_timeline, remote_dboid, local_dboid, replname
 */
#define BDR_REPORIGIN_ID_FORMAT "bdr_"UINT64_FORMAT"_%u_%u_%u_%s"

typedef enum BdrNodeStatus
{
	BDR_NODE_STATUS_NONE = '\0',
	BDR_NODE_STATUS_BEGINNING_INIT = 'b',
	BDR_NODE_STATUS_COPYING_INITIAL_DATA = 'i',
	BDR_NODE_STATUS_CATCHUP = 'c',
	BDR_NODE_STATUS_CREATING_OUTBOUND_SLOTS = 'o',
	BDR_NODE_STATUS_READY = 'r',
	BDR_NODE_STATUS_KILLED = 'k'
} BdrNodeStatus;

/*
 * Because C doesn't let us do literal string concatentation
 * with "char", provide versions as SQL literals too.
 */
#define BDR_NODE_STATUS_BEGINNING_INIT_S "'b'"
#define BDR_NODE_STATUS_COPYING_INITIAL_DATA_S "'i'"
#define BDR_NODE_STATUS_CATCHUP_S "'c'"
#define BDR_NODE_STATUS_CREATING_OUTBOUND_SLOTS_S "'o'"
#define BDR_NODE_STATUS_READY_S "'r'"
#define BDR_NODE_STATUS_KILLED_S "'k'"

#define BDR_CONTROL_FILE	"bdr_control"

/* Magic number identifying the bdr_control file format */
static const uint32 BDR_CONTROL_FILE_MAGIC_NUMBER = 0x26062023;

/* Structure representing bdr_nodes record */
typedef struct BDRNodeId
{
	uint64		sysid;
	TimeLineID	timeline;
	Oid			dboid;
}			BDRNodeId;

/* A configured BDR connection from bdr_connections */
typedef struct BdrConnectionConfig
{
	BDRNodeId	remote_node;

	/*
	 * If the origin_ id fields are set then they must refer to our node,
	 * otherwise we wouldn't load the configuration entry. So if origin_is_set
	 * is false the origin was zero, and if true the origin is the local node
	 * id.
	 */
	bool		origin_is_my_id;

	/* connstring, palloc'd in same memory context as this struct */
	char	   *dsn;

	/*
	 * bdr_nodes.node_name, palloc'd in same memory context as this struct.
	 * Could be NULL if we're talking to an old BDR.
	 */
	char	   *node_name;

	int			apply_delay;

	/* Quoted identifier-list of replication sets */
	char	   *replication_sets;
}			BdrConnectionConfig;

extern volatile sig_atomic_t got_SIGTERM;
extern volatile sig_atomic_t got_SIGHUP;

extern void bdr_error_nodeids_must_differ(const BDRNodeId * const other_nodeid);
extern BdrConnectionConfig * bdr_get_connection_config(const BDRNodeId * nodeid,
													   bool missing_ok);
extern BdrConnectionConfig * bdr_get_my_connection_config(bool missing_ok);

extern void bdr_free_connection_config(BdrConnectionConfig * cfg);

extern void bdr_slot_name(Name out_name, const BDRNodeId * const remote, Oid local_dboid);

extern char *bdr_replident_name(const BDRNodeId * const remote, Oid local_dboid);

extern void bdr_parse_slot_name(const char *name, BDRNodeId * remote, Oid *local_dboid);

extern void bdr_parse_replident_name(const char *name, BDRNodeId * remote, Oid *local_dboid);

extern int	bdr_find_other_exec(const char *argv0, const char *target,
								uint32 *version, char *retpath);

extern uint64 GenerateNodeIdentifier(void);

#endif							/* BDR_INTERNAL_H */
