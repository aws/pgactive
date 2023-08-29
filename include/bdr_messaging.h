#ifndef BDR_NODE_MESSAGING_H
#define BDR_NODE_MESSAGING_H

#include "lib/stringinfo.h"

typedef enum BdrMessageType
{
	BDR_MESSAGE_START,			/* bdr started */
	/* DDL locking */
	BDR_MESSAGE_ACQUIRE_LOCK,
	BDR_MESSAGE_RELEASE_LOCK,
	BDR_MESSAGE_CONFIRM_LOCK,
	BDR_MESSAGE_DECLINE_LOCK,
	/* Replay confirmations */
	BDR_MESSAGE_REQUEST_REPLAY_CONFIRM,
	BDR_MESSAGE_REPLAY_CONFIRM
	/* Node detach/join */

}			BdrMessageType;

extern void bdr_process_remote_message(StringInfo s);
extern void bdr_prepare_message(StringInfo s, BdrMessageType message_type);
extern void bdr_send_message(StringInfo s, bool transactional);

extern char *bdr_message_type_str(BdrMessageType message_type);

#endif
