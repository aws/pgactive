/* -------------------------------------------------------------------------
 *
 * pgactive_messaging.c
 *		Replication!!!
 *
 * Replication???
 *
 * Copyright (C) 2012-2016, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_messaging.c
 *
 * pgactive needs to do cluster-wide operations with varying degrees of synchronous
 * behaviour in order to perform DDL, detach/join nodes, etc. Operations may need
 * to communicate with a quorum of nodes or all known nodes. The logic to
 * handle WAL message sending/receiving and dispatch, quorum counting, etc is
 * centralized here.
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pgactive.h"
#include "pgactive_locks.h"
#include "pgactive_messaging.h"

#include "libpq/pqformat.h"

#include "replication/message.h"
#include "replication/origin.h"

#include "utils/memutils.h"

#include "miscadmin.h"

/*
 * Receive and decode a logical WAL message
 */
void
pgactive_process_remote_message(StringInfo s)
{
	StringInfoData message;
	bool		transactional;
	int			msg_type;
	XLogRecPtr	lsn;
	pgactiveNodeId	origin_node;

	transactional = pq_getmsgbyte(s);
	lsn = pq_getmsgint64(s);

	/*
	 * Logical WAL messages are (for some reason) encapsulated in their own
	 * header with its own length, even though the outer CopyData knows its
	 * length. Unwrap it.
	 */
	initStringInfo(&message);
	message.len = pq_getmsgint(s, 4);
	message.data = (char *) pq_getmsgbytes(s, message.len);
	msg_type = pq_getmsgint(&message, 4);
	pgactive_getmsg_nodeid(&message, &origin_node, true);

	elog(DEBUG1, "received message type %s from " pgactive_NODEID_FORMAT_WITHNAME " at %X/%X",
		 pgactive_message_type_str(msg_type),
		 pgactive_NODEID_FORMAT_WITHNAME_ARGS(origin_node), LSN_FORMAT_ARGS(lsn));

	if (pgactive_locks_process_message(msg_type, transactional, lsn, &origin_node, &message))
		goto done;

	elog(WARNING, "unhandled pgactive message of type %s", pgactive_message_type_str(msg_type));

	resetStringInfo(&message);

done:
	if (!transactional)
		replorigin_session_advance(lsn, InvalidXLogRecPtr);

	Assert(CurrentMemoryContext == MessageContext);
}

/*
 * Prepare a StringInfo with a pgactive WAL-message header. The caller
 * should then append message-specific payload to the StringInfo
 * with the pq_send functions, then call pgactive_send_message(...)
 * to dispatch it.
 *
 * The StringInfo must be initialized.
 */
void
pgactive_prepare_message(StringInfo s, pgactiveMessageType message_type)
{
	pgactiveNodeId	myid;

	pgactive_make_my_nodeid(&myid);

	elog(DEBUG2, "preparing message type %s in %p from " pgactive_NODEID_FORMAT_WITHNAME,
		 pgactive_message_type_str(message_type),
		 (void *) s,
		 pgactive_NODEID_FORMAT_WITHNAME_ARGS(myid));

	/* message type */
	pq_sendint(s, message_type, 4);
	/* node identifier */
	pgactive_send_nodeid(s, &myid, true);

	/* caller's data will follow */
}

/*
 * Send a WAL message previously prepared with pgactive_prepare_message,
 * after using pq_send functions to add message-specific payload.
 *
 * The StringInfo is reset automatically and may be re-used
 * for another message.
 */
void
pgactive_send_message(StringInfo s, bool transactional)
{
	XLogRecPtr	lsn;

	lsn = LogLogicalMessage(pgactive_LOGICAL_MSG_PREFIX, s->data, s->len, transactional);
	XLogFlush(lsn);

	elog(DEBUG3, "sending prepared message %p",
		 (void *) s);

	resetStringInfo(s);
}

/*
 * Get the text name for a message type. The caller must
 * NOT free the result.
 */
char *
pgactive_message_type_str(pgactiveMessageType message_type)
{
	switch (message_type)
	{
		case pgactive_MESSAGE_START:
			return "pgactive_MESSAGE_START";
		case pgactive_MESSAGE_ACQUIRE_LOCK:
			return "pgactive_MESSAGE_ACQUIRE_LOCK";
		case pgactive_MESSAGE_RELEASE_LOCK:
			return "pgactive_MESSAGE_RELEASE_LOCK";
		case pgactive_MESSAGE_CONFIRM_LOCK:
			return "pgactive_MESSAGE_CONFIRM_LOCK";
		case pgactive_MESSAGE_DECLINE_LOCK:
			return "pgactive_MESSAGE_DECLINE_LOCK";
		case pgactive_MESSAGE_REQUEST_REPLAY_CONFIRM:
			return "pgactive_MESSAGE_REQUEST_REPLAY_CONFIRM";
		case pgactive_MESSAGE_REPLAY_CONFIRM:
			return "pgactive_MESSAGE_REPLAY_CONFIRM";
	}
	elog(ERROR, "unhandled pgactiveMessageType %d", message_type);
}
