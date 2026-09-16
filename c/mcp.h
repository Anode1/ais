/* mcp.h -- the Model Context Protocol front end: ais as a tool server for an
 * agent, over stdin/stdout.
 *
 * MCP is what an agent client speaks to a local tool (Claude Code is the one
 * the tests exercise), so this is the same engine the CLI drives, reached the
 * way an agent already knows how to reach things. Recall by key is a lookup: an agent
 * that asks for what the user filed pays one round trip instead of grepping a
 * tree into its context window.
 */
#ifndef AIS_MCP_H
#define AIS_MCP_H

#include "ais.h"

/* Serve MCP on stdin/stdout until the client closes the stream: newline-framed
 * JSON-RPC 2.0, one message per line, which is the MCP stdio transport.
 * ALLOW_WRITE adds the save tool; without it the session cannot change the
 * index at all. Returns 0 on a clean close. */
int ais_mcp(ais *a, int allow_write);

#endif /* AIS_MCP_H */
