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
not. An id of object, array or boolean type is `-32600`, while `+5` and `007`
are `-32700`, because JSON has no such numbers and the reader stops at the
parse. A top-level array is `-32600` naming the one-object-per-line rule, since
well-formed JSON in the wrong shape is not a parse fault. A missing or wrong
`jsonrpc` is `-32600` and does carry the id, which was read before the check.

`initialize` echoes the protocol version the client asked for when it is one of
the three published revisions (`2024-11-05`, `2025-03-26`, `2025-06-18`), whose
tool surface is the same here, and otherwise answers with `MCP_PROTOCOL`. A
string that merely looks like a date is not a revision: echoing it would agree
to a protocol nobody has written.

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

The `INSTR_*` constants and the tool descriptions near the end of `c/mcp.c` are
the whole interface a model sees, so they are written for a reader. The
user-facing half of that is in [`../MCP.md`](../MCP.md); the mechanics belong
here.

The tool descriptions are printed unescaped, straight from the constant, so a
literal `"` inside one breaks the entire `tools/list` reply while `initialize`
still parses: a client then connects, reports healthy, and has no tools. Quote
examples with single quotes, as the file does for `'id|value'` and `'blobs/'`.
The instructions escape properly, because they go out through `jout()`.

The instructions are assembled from three constants at connect time, and the
middle one is a choice, not an addition: `INSTR_SAVE` under `rw`, `INSTR_NOSAVE`
otherwise. Appending the read-only notice instead left the model told to call
save and then told there is no save, and a model reading both either claims it
saved something or reaches for a file of its own. The path of the index actually
opened is then appended, for the reason the code comment gives.

Which index gets served is decided before any of this, in main.c: `ais_locate_how`
reports which precedence step chose the path, and step 2, a `.ais` found by
walking up, is refused there (LAYOUT.md, "--mcp and the index nobody named").
`ais_mcp` is handed an open index and a write bit and knows nothing about it.

Three guards live in the tool layer rather than in the engine, because each one
exists to keep a model from being misled by its own reply: `args_ok` refuses an
argument no tool has, `rows_arg` refuses a limit outside 1 to 1000 instead of
clamping it, and `out_value` replaces a secret's ciphertext and a `blobs/` path
that fails `ais_blob_rel_ok` with fixed markers. The store keeps the real value
in every case, which is why `find` still matches text a reply will not show.

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
      | ais -f /tmp/scratch/.ais --mcp

Name the index with `-f`: in a directory at or under a `.ais/`, the server exits
2 instead of serving it.
