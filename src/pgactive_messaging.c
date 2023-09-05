/* -------------------------------------------------------------------------
 *
 * bdr_messaging.c
 *		Replication!!!
 *
 * Replication???
 *
 * Copyright (C) 2012-2016, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		bdr_messaging.c
 *
 * BDR needs to do cluster-wide operations with varying degrees of synchronous
 * behaviour in order to perform DDL, detach/join nodes, etc. Operations may need
 * to communicate with a quorum of nodes or all known nodes. The logic to
 * handle WAL message sending/receiving and dispatch, quorum counting, etc is
 * centralized here.
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "bdr.h"
#include "bdr_locks.h"
#include "bdr_messaging.h"

#include "libpq/pqformat.h"

#include "replication/message.h"
#include "replication/origin.h"

#include "utils/memutils.h"

#include "miscadmin.h"

/*
 * Receive and decode a logical WAL message
 */
void
bdr_process_remote_message(StringInfo s)
{
	StringInfoData message;
	bool		transactional;
	int			msg_type;
	XLogRecPtr	lsn;
	BDRNodeId	origin_node;

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
	bdr_getmsg_nodeid(&message, &origin_node, true);

	elog(DEBUG1, "received message type %s from " BDR_NODEID_FORMAT_WITHNAME " at %X/%X",
		 bdr_message_type_str(msg_type),
		 BDR_NODEID_FORMAT_WITHNAME_ARGS(origin_node), LSN_FORMAT_ARGS(lsn));

	if (bdr_locks_process_message(msg_type, transactional, lsn, &origin_node, &message))
		goto done;

	elog(WARNING, "unhandled BDR message of type %s", bdr_message_type_str(msg_type));

	resetStringInfo(&message);

done:
	if (!transactional)
		replorigin_session_advance(lsn, InvalidXLogRecPtr);

	Assert(CurrentMemoryContext == MessageContext);
}

/*
 * Prepare a StringInfo with a BDR WAL-message header. The caller
 * should then append message-specific payload to the StringInfo
 * with the pq_send functions, then call bdr_send_message(...)
 * to dispatch it.
 *
 * The StringInfo must be initialized.
 */
void
bdr_prepare_message(StringInfo s, BdrMessageType message_type)
{
	BDRNodeId	myid;

	bdr_make_my_nodeid(&myid);

	elog(DEBUG2, "preparing message type %s in %p from " BDR_NODEID_FORMAT_WITHNAME,
		 bdr_message_type_str(message_type),
		 (void *) s,
		 BDR_NODEID_FORMAT_WITHNAME_ARGS(myid));

	/* message type */
	pq_sendint(s, message_type, 4);
	/* node identifier */
	bdr_send_nodeid(s, &myid, true);

	/* caller's data will follow */
}

/*
 * Send a WAL message previously prepared with bdr_prepare_message,
 * after using pq_send functions to add message-specific payload.
 *
 * The StringInfo is reset automatically and may be re-used
 * for another message.
 */
void
bdr_send_message(StringInfo s, bool transactional)
{
	XLogRecPtr	lsn;

	lsn = LogLogicalMessage(BDR_LOGICAL_MSG_PREFIX, s->data, s->len, transactional);
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
bdr_message_type_str(BdrMessageType message_type)
{
	switch (message_type)
	{
		case BDR_MESSAGE_START:
			return "BDR_MESSAGE_START";
		case BDR_MESSAGE_ACQUIRE_LOCK:
			return "BDR_MESSAGE_ACQUIRE_LOCK";
		case BDR_MESSAGE_RELEASE_LOCK:
			return "BDR_MESSAGE_RELEASE_LOCK";
		case BDR_MESSAGE_CONFIRM_LOCK:
			return "BDR_MESSAGE_CONFIRM_LOCK";
		case BDR_MESSAGE_DECLINE_LOCK:
			return "BDR_MESSAGE_DECLINE_LOCK";
		case BDR_MESSAGE_REQUEST_REPLAY_CONFIRM:
			return "BDR_MESSAGE_REQUEST_REPLAY_CONFIRM";
		case BDR_MESSAGE_REPLAY_CONFIRM:
			return "BDR_MESSAGE_REPLAY_CONFIRM";
	}
	elog(ERROR, "unhandled BdrMessageType %d", message_type);
}
