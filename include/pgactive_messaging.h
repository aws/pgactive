#ifndef pgactive_NODE_MESSAGING_H
#define pgactive_NODE_MESSAGING_H

#include "lib/stringinfo.h"

typedef enum pgactiveMessageType
{
	pgactive_MESSAGE_START,		/* pgactive started */
	/* DDL locking */
	pgactive_MESSAGE_ACQUIRE_LOCK,
	pgactive_MESSAGE_RELEASE_LOCK,
	pgactive_MESSAGE_CONFIRM_LOCK,
	pgactive_MESSAGE_DECLINE_LOCK,
	/* Replay confirmations */
	pgactive_MESSAGE_REQUEST_REPLAY_CONFIRM,
	pgactive_MESSAGE_REPLAY_CONFIRM
	/* Node detach/join */

}			pgactiveMessageType;

extern void pgactive_process_remote_message(StringInfo s);
extern void pgactive_prepare_message(StringInfo s, pgactiveMessageType message_type);
extern void pgactive_send_message(StringInfo s, bool transactional);

extern char *pgactive_message_type_str(pgactiveMessageType message_type);

#endif
