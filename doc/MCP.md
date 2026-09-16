# ais --mcp: the agent tool

`ais --mcp` hands your index to a coding agent as a tool it can call. MCP is the
protocol agents use to reach tools on your machine, and there is nothing to
understand beyond one line of config: the agent starts `ais` itself and talks to
it over a pipe.

A key lookup returns the matching rows, and a wrong key returns nothing rather
than something plausible, so only what matched enters the context window. That
saving is measured in the README, [with the run and how to reproduce
it](../README.md#give-an-agent-your-index).

## Wire it up

```sh
claude mcp add ais -- ais --mcp
```

Any client that reads the usual JSON starts it the same way, in its own config
file:

```json
{ "mcpServers": { "ais": { "command": "ais", "args": ["--mcp"] } } }
```

Claude Code is the client the tests exercise. Nothing runs in the background,
there is no account and no port.

## Which index it opens

Your home index `~/.ais`, the current named index, or whatever `-f` names. The
`.ais/` found by walking up from the working directory is the one index `--mcp`
will not serve: a repository you cloned can ship one, and it would become the
agent's memory with its records reaching the model as tool output. On that one it
prints a line naming `-f` and exits 2 before serving anything. A project index is
therefore served by naming it, and that line in the client's own configuration is
the permission:

    claude mcp add ais -- ais -f /abs/path/of/project/.ais --mcp

`ais --serve` still opens a walked-up `.ais/`, because the web GUI is yours and
you opened it in that directory on purpose.

`ais --init` in a repository gives that project its own index. What an agent
files there sits beside the code as plain text, readable in a diff, and separate
from your personal index. The server names the index it opened, in the
instructions it hands the client on connect, because a repo-local `.ais/` and
your personal `~/.ais` speak the same protocol and otherwise look alike.

## The tools

| Tool | Arguments | Rows by default | Answers |
| --- | --- | --- | --- |
| `recall` | `keys`, `match` (`all`, the default, or `any`), `limit` | 200 | `id\|value` per line |
| `find` | `text`, `limit` | 50 | `id\|value` per line, values holding TEXT (case folded over ASCII) |
| `tags` | `limit` | 200 | `count\|key` per line, busiest first |
| `timeline` | `count`, or `limit` for the same thing | 20 | `id\|timestamp\|keys\|value` per line, newest first |
| `save` | `value`, `keys`, both required | | `saved as record N under KEYS`; present only with `rw` |

A row budget is a JSON integer from 1 to 1000. A string, a fraction, a zero or a
number past 1000 is a tool error naming the range: nothing is clamped, because a
model cannot tell a rewritten limit from the one it asked for. An argument no
tool has is refused naming the ones it takes, since an invented `since` on
`timeline` would otherwise be answered with the whole index and read as filtered.
`match` is `all` or `any` and nothing else.

## What comes back

Rows are the shapes the CLI prints. An empty result is words rather than an empty
block, because a model reads an empty block as a broken tool: `no match` from
`recall` and `find`, `the index has no keys yet` from `tags`, `the index is
empty` from `timeline`, which are three different facts. A reply that spent its
budget ends with `(stopped at the limit; there may be more)`, and every tool
marks it, and only when one more row exists, so a page is never read as the whole
index.

## The keys stay yours

Nobody types a tool call. People say "save this", "add it to my memory", "what
did I file about venice", and the model decides what those mean. So the server
tells it, in the `instructions` it hands every client on connect. The first line
is the whole position:

> ais is this person's own associative index: things they filed under their own
> words, on their own disk, in plain text.

The rule it spends most words on: when you have not said what to file something
under, the model asks, and offers the keys your index already uses. It does not
invent a vocabulary for you.

This is the part no automatic tagger can copy without giving up its own premise.
A model that files your things under the words it would have chosen has handed
you the average of everyone's words, which is the thing this index exists not to
be. See [`about.txt`](about.txt) for the argument in full.

## What it refuses

Writes are off unless you start it as `ais --mcp rw`. An agent gets recall by
default and has to be handed the write bit on purpose. Read-only is not a save
that fails: the instructions then carry no save paragraph at all, and say that
restarting as `ais --mcp rw` turns one on.

There is no delete, no re-tag, no edit, at any setting. Those are the operations
whose damage you cannot see happening, and the CLI is two keystrokes away.

`save` itself refuses five things:

| Refused | Because |
| --- | --- |
| no keys | a record filed under nothing cannot be recalled by any key, ever |
| a blank value | a lone space or newline would file an empty record |
| a value starting with `aisc:` or `blobs/` | both prefixes are the index writing to itself, a secret marker and a document path, and a model that could plant either could make a value the front ends read as a file of ais's own making |
| a key holding `\|`, a tab, a newline or any other control byte | the store is line-oriented and the key column is one line of it |
| more than 64 keys in one call | the engine's width for one record |

Two saves succeed in a shape worth knowing. A value the index already holds is
not stored twice: the keys are added to the record that holds it, and the reply
says `added KEYS to existing record N, now under ALLKEYS`. A value with a newline
in it becomes a document file, and the reply names it, `as document blobs/NAME
(find does not search inside documents)`.

## What a model never sees

An encrypted value is never handed over. Every tool emits it as `aisc:
(encrypted, hidden)`, `find` included, which still matches on the stored
ciphertext and still hides it. Cleartext appears only at a terminal and never on
a pipe, and this server is a pipe, so there is no unlocked vault behind it for an
agent to drain.

A value that names a file is a path, and the agent can read that file if it
should. A `--doc` body sits at `blobs/...` under the index directory the
instructions named; anything else you indexed is the path you gave. A `blobs/`
value that does not resolve inside the index is a path that escapes it, and comes
back as `[document path withheld: escapes the index]`.

## What it is not

It is not a vector store and not RAG; the reason is in the README under
[Why not embeddings or a vector database](../README.md#questions).

It is not a model deciding what to remember on your behalf. You name the keys,
or the agent asks you for them.

It is not a server: no daemon, no port, no account, nothing leaves the machine.

Implementation notes, the wire format and the test recipe are in
[`dev/MCP.md`](dev/MCP.md).
