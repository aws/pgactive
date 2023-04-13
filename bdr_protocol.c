#include "postgres.h"

#include "libpq/pqformat.h"
#include "utils/elog.h"

#include "bdr.h"
#include "bdr_internal.h"

/*
 * Extract a BDR node ID from a wire protocol message.
 *
 * The boolean flag for node name is required because, for historical reasons,
 * some BDR messages send an empty node-name field in node IDs and others do
 * not.
 */
void
bdr_getmsg_nodeid(StringInfo message, BDRNodeId * const nodeid, bool expect_empty_nodename)
{
	nodeid->sysid = pq_getmsgint64(message);
	nodeid->timeline = pq_getmsgint(message, 4);
	nodeid->dboid = pq_getmsgint(message, 4);
	if (expect_empty_nodename)
	{
		int			namelen = pq_getmsgint(message, 4);

		if (namelen != 0)
			ereport(ERROR,
					(errcode(ERRCODE_PROTOCOL_VIOLATION),
					 errmsg("expected zero-length node name, got %d", namelen)));
	}
}

void
bdr_send_nodeid(StringInfo s, const BDRNodeId * const nodeid, bool include_empty_nodename)
{
	pq_sendint64(s, nodeid->sysid);
	pq_sendint(s, nodeid->timeline, 4);
	pq_sendint(s, nodeid->dboid, 4);
	if (include_empty_nodename)
		pq_sendint(s, 0, 4);
}

/*
 * Converts an int64 to network byte order.
 */
void
bdr_sendint64(int64 i, char *buf)
{
	uint32		n32;

	/* High order half first, since we're doing MSB-first */
	n32 = (uint32) (i >> 32);
	n32 = htonl(n32);
	memcpy(&buf[0], &n32, 4);

	/* Now the low order half */
	n32 = (uint32) i;
	n32 = htonl(n32);
	memcpy(&buf[4], &n32, 4);
}
