# The `--mcp` tool server

`ais --mcp` serves the Model Context Protocol on stdin and stdout, so an agent
reaches the index the way it already reaches every other local tool. It is a
front end like the web GUI and the phone app: same engine, same index, no state
of its own.

What it buys is the difference between recall and search. An agent that greps a
tree to find something the user already filed pays that cost on every question;
a key lookup returns the matching rows and nothing else. The measurement is in
the README: 24,500 tokens against 2,900, and 40 of 40 answers exact.

## Wiring it into a client

Claude Code:

    claude mcp add ais -- ais --mcp

Anything else that reads the usual JSON (Claude Desktop, Cursor, Zed, Windsurf):

    { "mcpServers": { "ais": { "command": "ais", "args": ["--mcp"] } } }

The index is resolved exactly as it is for every other command: the nearest
`.ais/` at or above the working directory, then the current named index, then
`~/.ais`. To pin one, pass it: `"args": ["-f", "/home/you/.ais", "--mcp"]`. A
per-repository index (`ais --init`) gives an agent a memory that lives with the
code, in plain text a reviewer can read.

## The tools

| Tool | Arguments | Answers |
| --- | --- | --- |
| `recall` | `keys` (array or one space-separated string), `match` (`all`, the default, or `any`), `limit` | `id\|value` per line |
| `find` | `text`, `limit` | `id\|value` per line, values containing TEXT (any case) |
| `tags` | `limit` | `count\|key` per line, busiest first |
| `timeline` | `count` | `id\|timestamp\|keys\|value` per line, newest first |
| `save` | `value`, `keys` | `saved as record N`; present only with `rw` |

Rows are the shapes the CLI prints, because that is the contract every other
front end follows. No match is the words `no match`, not an empty block: a model
reads emptiness as a broken tool and calls again.

## In conversation

Nobody types a tool call. People say "save this", "add it to my memory", "what
did I file under venice", and the model decides what that means. MCP has a place
for exactly this, the `instructions` string in the initialize reply, and the
server fills it: when to call save rather than answer from its own memory, that
recall comes before find, and that an empty answer is an answer.

The rule it spends most words on: the keys are the user's. If they did not name
any ("save this"), the model asks which keys to file it under, several separated
by spaces, and suggests what the index already uses rather than inventing a
vocabulary. A model that files someone's things under the average of everyone's
words has built the thing this index exists not to be.

The instructions live in `INSTRUCTIONS[]` at the top of `c/mcp.c`, next to the
tool descriptions, because together they are the whole interface a model sees.

## What it will not do

Writes are off unless the operand says `rw`. An agent gets recall by default and
has to be handed the write bit deliberately.

There is no delete, no re-tag, no edit, at any setting. Those are the operations
whose damage a person cannot see happening, and the CLI is two keystrokes away.

An encrypted value stays the opaque `aisc:` marker in every reply. Decryption
prompts for a passphrase at a terminal, so there is no unlocked vault behind this
server and nothing for an agent to drain. A document's value is its `blobs/`
path: a real file on the same disk, which the agent can read if it should.

## How it is built

`c/mcp.c` calls the engine's public API and nothing else in `c/`, so adding or
removing it changes only main.c's dispatch. It carries its own small JSON reader
(objects, arrays, strings with `\u` escapes and surrogate pairs, numbers) that
unescapes in place inside the request buffer, so a request costs one fixed node
table and no allocation.

Replies stream. A record is escaped into the reply as the engine hands it over,
so recalling the whole index costs the same memory as recalling one record. The
price is that a reply cannot be retracted once it has started, which is why
everything that can fail is settled before the first byte goes out.

The transport is the MCP stdio one: one JSON-RPC 2.0 message per line. A request
longer than `AIS_LINE_MAX` is refused with `-32600` and the rest of the line is
dropped, rather than being parsed in halves. stdout carries protocol and nothing
else; diagnostics go to stderr.

Two builds exclude the file by name, because a phone has no stdin to serve:
`ais_engine.podspec` (iOS) and `app/flutter/src/CMakeLists.txt` (Android and the
Linux desktop runner). Both already exclude `main.c` and `tests.c`; keep the
three lists in step when a front end file is added.

## Testing it

The server is a pipe, so a test is a pipe. `tests/cli.sh` drives a real
handshake and asserts on the replies; by hand:

    printf '%s\n' \
      '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}' \
      '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"tags","arguments":{}}}' \
      | ais --mcp
