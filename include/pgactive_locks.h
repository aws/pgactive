/*
 * pgactive_locks.h
 *
 * BiDirectionalReplication
 *
 * Copyright (c) 2014-2015, PostgreSQL Global Development Group
 *
 * pgactive_locks.h
 */
#ifndef pgactive_LOCKS_H
#define pgactive_LOCKS_H

typedef enum pgactiveLockType
{
	pgactive_LOCK_NOLOCK,		/* no lock (not used) */
	pgactive_LOCK_DDL,			/* lock against DDL */
	pgactive_LOCK_WRITE			/* lock against any write */
}			pgactiveLockType;

void		pgactive_locks_startup(void);
void		pgactive_locks_set_nnodes(int nnodes);
void		pgactive_acquire_ddl_lock(pgactiveLockType lock_type);
void		pgactive_process_acquire_ddl_lock(const pgactiveNodeId * const node,
											  pgactiveLockType lock_type);
void		pgactive_process_release_ddl_lock(const pgactiveNodeId * const origin, const pgactiveNodeId * const lock);
void		pgactive_process_confirm_ddl_lock(const pgactiveNodeId * const origin, const pgactiveNodeId * const lock,
											  pgactiveLockType lock_type);
void		pgactive_process_decline_ddl_lock(const pgactiveNodeId * const origin, const pgactiveNodeId * const lock,
											  pgactiveLockType lock_type);
void		pgactive_process_request_replay_confirm(const pgactiveNodeId * const node, XLogRecPtr lsn);
void		pgactive_process_replay_confirm(const pgactiveNodeId * const node, XLogRecPtr lsn);
void		pgactive_locks_process_remote_startup(const pgactiveNodeId * const node);

extern bool pgactive_locks_process_message(int msg_type, bool transactional,
										   XLogRecPtr lsn, const pgactiveNodeId * const origin,
										   StringInfo message);

extern char *pgactive_lock_type_to_name(pgactiveLockType lock_type);
extern pgactiveLockType pgactive_lock_name_to_type(const char *lock_type);

extern void pgactive_locks_node_detached(pgactiveNodeId * node);

extern bool IspgactiveLocksShmemLockHeldByMe(void);

#endif
