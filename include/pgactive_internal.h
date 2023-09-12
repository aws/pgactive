/*
 * pgactive_internal.h
 *
 * Active-active Replication
 *
 * Copyright (c) 2012-2015, PostgreSQL Global Development Group
 *
 * pgactive_internal.h must be #include-able from FRONTEND code, so it may not
 * reference elog, List, etc.
 */
#ifndef pgactive_INTERNAL_H
#define pgactive_INTERNAL_H

#include <signal.h>
#include "access/xlogdefs.h"

#define EMPTY_REPLICATION_NAME ""

/*
 * The format used for slot names
 *
 * params: local_dboid, remote_sysid, remote_timeline, remote_dboid, replname
 */
#define pgactive_SLOT_NAME_FORMAT "pgactive_%u_"UINT64_FORMAT"_%u_%u__%s"

/*
 * The format used for replication identifiers (riident, replident)
 *
 * params: remote_sysid, remote_timeline, remote_dboid, local_dboid, replname
 */
#define pgactive_REPORIGIN_ID_FORMAT "pgactive_"UINT64_FORMAT"_%u_%u_%u_%s"

typedef enum pgactiveNodeStatus
{
	pgactive_NODE_STATUS_NONE = '\0',
	pgactive_NODE_STATUS_BEGINNING_INIT = 'b',
	pgactive_NODE_STATUS_COPYING_INITIAL_DATA = 'i',
	pgactive_NODE_STATUS_CATCHUP = 'c',
	pgactive_NODE_STATUS_CREATING_OUTBOUND_SLOTS = 'o',
	pgactive_NODE_STATUS_READY = 'r',
	pgactive_NODE_STATUS_KILLED = 'k'
} pgactiveNodeStatus;

/*
 * Because C doesn't let us do literal string concatentation
 * with "char", provide versions as SQL literals too.
 */
#define pgactive_NODE_STATUS_BEGINNING_INIT_S "'b'"
#define pgactive_NODE_STATUS_COPYING_INITIAL_DATA_S "'i'"
#define pgactive_NODE_STATUS_CATCHUP_S "'c'"
#define pgactive_NODE_STATUS_CREATING_OUTBOUND_SLOTS_S "'o'"
#define pgactive_NODE_STATUS_READY_S "'r'"
#define pgactive_NODE_STATUS_KILLED_S "'k'"

#define pgactive_NID_GETTER_FUNC_NAME "_pgactive_node_identifier_getter_private"

#define pgactiveThisTimeLineID 0

/* Structure representing pgactive_nodes record */
typedef struct pgactiveNodeId
{
	uint64		sysid;
	TimeLineID	timeline;
	Oid			dboid;
}			pgactiveNodeId;

/* A configured pgactive connection from pgactive_connections */
typedef struct pgactiveConnectionConfig
{
	pgactiveNodeId remote_node;

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
	 * pgactive_nodes.node_name, palloc'd in same memory context as this
	 * struct. Could be NULL if we're talking to an old pgactive.
	 */
	char	   *node_name;

	int			apply_delay;

	/* Quoted identifier-list of replication sets */
	char	   *replication_sets;
}			pgactiveConnectionConfig;

extern pgactiveConnectionConfig * pgactive_get_connection_config(const pgactiveNodeId * nodeid,
																 bool missing_ok);
extern pgactiveConnectionConfig * pgactive_get_my_connection_config(bool missing_ok);

extern void pgactive_free_connection_config(pgactiveConnectionConfig * cfg);

extern void pgactive_slot_name(Name out_name, const pgactiveNodeId * const remote, Oid local_dboid);

extern char *pgactive_replident_name(const pgactiveNodeId * const remote, Oid local_dboid);

extern void pgactive_parse_slot_name(const char *name, pgactiveNodeId * remote, Oid *local_dboid);

extern void pgactive_parse_replident_name(const char *name, pgactiveNodeId * remote, Oid *local_dboid);

extern int	pgactive_find_other_exec(const char *argv0, const char *target,
									 uint32 *version, char *retpath);

extern uint64 GenerateNodeIdentifier(void);

extern char *get_connect_string(const char *servername);
#endif							/* pgactive_INTERNAL_H */
