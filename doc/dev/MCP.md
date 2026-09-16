# `--mcp`: the implementation

What the server is for, how to wire it into a client, and what it refuses to do
are in [`../MCP.md`](../MCP.md), which is the page a user lands on. This file is
the inside: the wire, the reader, and the three lists that have to stay in step.

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

## The text a model reads

`INSTRUCTIONS[]` and the tool descriptions at the top of `c/mcp.c` are the whole
interface a model sees, so they are written for a reader. The user-facing half of
that is in [`../MCP.md`](../MCP.md); three mechanics belong here.

The tool descriptions are printed unescaped, straight from the constant, so a
literal `"` inside one breaks the entire `tools/list` reply while `initialize`
still parses: a client then connects, reports healthy, and has no tools. Quote
examples with single quotes, as the file does for `'id|value'` and `'blobs/'`.
`INSTRUCTIONS[]` is safe from this because it goes out through `jout()`.

Two things are appended to the instructions at connect time. In a read-only
session, that there is no save tool and that `ais --mcp rw` turns one on, since a
model told to call a tool it cannot see will claim it saved something. And the
path of the index actually opened, because a repo-local `.ais/` and the personal
`~/.ais` speak the same protocol, and an agent that cannot tell them apart
reports a project's notes as the user's own memory.

Which index gets served is decided before any of this, in main.c: `ais_locate_how`
reports which precedence step chose the path, and step 2, a `.ais` found by
walking up, is refused there (LAYOUT.md, "--mcp and the index nobody named").
`ais_mcp` is handed an open index and a write bit and knows nothing about it.

`save` requires keys in its schema as well as in its prose. The schema is what a
model generates against, so prose alone loses: with `"required":["value"]` a
keyless save succeeded and made a record that no key can ever recall.

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
