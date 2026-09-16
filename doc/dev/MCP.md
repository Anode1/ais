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
| `recall` | `keys`, `match` (`all`, the default, or `any`), `limit` | `id\|value` per line |
| `find` | `text`, `limit` | `id\|value` per line, values containing TEXT (any case) |
| `tags` | `limit` | `count\|key` per line, busiest first |
| `timeline` | `count` (`limit` is taken too) | `id\|timestamp\|keys\|value` per line, newest first |
| `save` | `value`, `keys`, both required | `saved as record N under KEYS`; present only with `rw` |

`keys` is declared in the schema as an array of strings, which is what a model
generates. One space-separated string is accepted too, for anything hand-rolled.

Rows are the shapes the CLI prints, because that is the contract every other
front end follows. Two answers are words rather than rows, because a model reads
an empty block as a broken tool and calls again: nothing matched is `no match`,
and a reply that hit its `limit` ends with `(stopped at the limit; there may be
more)` so a page is not reported as the whole index. `tags` on an empty index
says `the index has no keys yet`.

`save` refuses a value with no keys. A record filed under nothing cannot be
recalled by any key, ever, so the refusal names what to do instead: ask the user
which keys, several separated by spaces. The reply names the keys back, which is
the only place a misheard key shows before the record is lost to it.

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
Two things are appended at connect time: in a read-only session, that there is no
save tool and how to turn one on, since a model told to call a tool it cannot see
will claim it saved something; and the path of the index actually opened, because
a repo-local `.ais/` and the personal `~/.ais` speak the same protocol and an
agent that cannot tell them apart reports a project's notes as the user's own
memory.

The tool descriptions are written out as JSON and printed unescaped, so a
literal `"` inside one would break the whole `tools/list` reply. Quote examples
with single quotes, as the file already does for `'id|value'` and `'blobs/'`.

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
longer than `AIS_LINE_MAX`, or one holding a NUL byte, is refused with `-32600`
and dropped whole rather than parsed in halves. Both answer with `"id": null`,
since the id was never read, so a client cannot correlate the failure with the
request it lost. The line is read byte by byte rather than with `fgets`, which
reports only a pointer: a NUL would then end the C string early and put the
framing off by one message.

An id goes back as the bytes that arrived, never re-rendered from a `long`, so
`1.5` and an id past `LONG_MAX` come back as themselves. `"id": null` is a
request under JSON-RPC and is answered; an absent id is a notification and is
not.

Values reach the reply as the store holds them, and the store holds whatever the
CLI, `--import` or sync were given, which need not be UTF-8. JSON must be, and a
client decodes the whole line before parsing it, so a byte that is not part of a
well-formed sequence goes out as `\ufffd` and costs one character instead of the
whole reply.

stdout carries protocol and nothing else; diagnostics go to stderr.

Two builds exclude the file by name, because a phone has no stdin to serve:
`ais_engine.podspec` (iOS) and `app/flutter/src/CMakeLists.txt` (Android and the
Linux desktop runner). Both already exclude `main.c` and `tests.c`; keep the
three lists in step when a front end file is added.

## Testing it

The server is a pipe, so a test is a pipe. `tests/cli.sh` drives a real
handshake and asserts on the replies. Every reply it collects also goes through
a JSON parser (`jsonok`), which is not decoration: a substring grep cannot see a
stray quote inside a description, and an unparseable `tools/list` once passed
thirty green assertions. By hand:

    printf '%s\n' \
      '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}' \
      '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"tags","arguments":{}}}' \
      | ais --mcp
