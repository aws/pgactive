/*
 * bdr_locks.h
 *
 * BiDirectionalReplication
 *
 * Copyright (c) 2014-2015, PostgreSQL Global Development Group
 *
 * bdr_locks.h
 */
#ifndef BDR_LOCKS_H
#define BDR_LOCKS_H

typedef enum BDRLockType
{
	BDR_LOCK_NOLOCK,			/* no lock (not used) */
	BDR_LOCK_DDL,				/* lock against DDL */
	BDR_LOCK_WRITE				/* lock against any write */
}			BDRLockType;

void		bdr_locks_startup(void);
void		bdr_locks_set_nnodes(int nnodes);
void		bdr_acquire_ddl_lock(BDRLockType lock_type);
void		bdr_process_acquire_ddl_lock(const BDRNodeId * const node,
										 BDRLockType lock_type);
void		bdr_process_release_ddl_lock(const BDRNodeId * const origin, const BDRNodeId * const lock);
void		bdr_process_confirm_ddl_lock(const BDRNodeId * const origin, const BDRNodeId * const lock,
										 BDRLockType lock_type);
void		bdr_process_decline_ddl_lock(const BDRNodeId * const origin, const BDRNodeId * const lock,
										 BDRLockType lock_type);
void		bdr_process_request_replay_confirm(const BDRNodeId * const node, XLogRecPtr lsn);
void		bdr_process_replay_confirm(const BDRNodeId * const node, XLogRecPtr lsn);
void		bdr_locks_process_remote_startup(const BDRNodeId * const node);

extern bool bdr_locks_process_message(int msg_type, bool transactional,
									  XLogRecPtr lsn, const BDRNodeId * const origin,
									  StringInfo message);

extern char *bdr_lock_type_to_name(BDRLockType lock_type);
extern BDRLockType bdr_lock_name_to_type(const char *lock_type);

extern void bdr_locks_node_detached(BDRNodeId * node);

extern bool IsBDRLocksShmemLockHeldByMe(void);

#endif
