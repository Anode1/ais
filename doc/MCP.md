# Give an agent your index

Your index is yours: things you filed under your own words, on your own disk, in
plain text. `ais --mcp` lets an agent read that index as a tool, which is the
difference between an agent recalling what you saved and an agent searching for
it again.

The saving is measured: 2,900 tokens a question against 24,500, and 40 of 40
answers exact against 31 of 40, with the harness in
[`experiment/`](../experiment/) and a deposited run that reproduces with no API
key. The reason is simpler than the numbers: a key lookup returns the matching
rows, and a wrong key returns nothing rather than something plausible.

## Install and wire it up

```sh
curl -fsSL https://raw.githubusercontent.com/Anode1/ais/main/scripts/install.sh | sh
claude mcp add ais -- ais --mcp
```

Any client that reads the usual JSON works the same way:

```json
{ "mcpServers": { "ais": { "command": "ais", "args": ["--mcp"] } } }
```

That is the shape Claude Desktop, Cursor, Zed and Windsurf all take. There is
nothing to run in the background, no account, and no port: the agent starts `ais`
itself and talks to it over a pipe.

## Which index it opens

Your home index `~/.ais`, the current named index, or whatever `-f` names. The
`.ais/` found by walking up from the working directory is the one index `--mcp`
will not serve: a repository you cloned can ship one, and it would become the
agent's memory with its records reaching the model as tool output. Naming the
index is the permission, so a project index is served by naming it:

    claude mcp add ais -- ais -f /abs/path/of/project/.ais --mcp

`ais --init` in a repository gives that project its own index. What an agent
files there sits beside the code as plain text, readable in a diff, and separate
from your personal index. The server tells the model which of the two it has
opened, so a project's notes are never reported back to you as your own memory.

## The tools

| Tool | Arguments | Answers |
| --- | --- | --- |
| `recall` | `keys`, `match` (`all`, the default, or `any`), `limit` | `id\|value` per line |
| `find` | `text`, `limit` | `id\|value` per line, values containing TEXT (any case) |
| `tags` | `limit` | `count\|key` per line, busiest first |
| `timeline` | `count` | `id\|timestamp\|keys\|value` per line, newest first |
| `save` | `value`, `keys`, both required | `saved as record N under KEYS`; present only with `rw` |

Rows are the shapes the CLI prints. Nothing matched is the words `no match`, and
a reply that hit its `limit` says so, because a model reads an empty block as a
broken tool and reports a page as the whole index.

## The keys stay yours

Nobody types a tool call. People say "save this", "add it to my memory", "what
did I file about venice", and the model decides what those mean. So the server
tells it, in the `instructions` it hands every client on connect. The first line
is the whole position:

> ais is this person's own associative index: things they filed under their own
> words, on their own disk, in plain text.

The rule it spends most words on: when you have not said what to file something
under, the model asks, and offers the keys your index already uses. It does not
invent a vocabulary for you. `save` refuses a value with no keys at all, because
a record filed under nothing cannot be recalled by any key, ever.

This is the part no automatic tagger can copy without giving up its own premise.
A model that files your things under the words it would have chosen has handed
you the average of everyone's words, which is the thing this index exists not to
be. See [`about.txt`](about.txt) for the argument in full.

## What it will not do

Writes are off unless you start it as `ais --mcp rw`. An agent gets recall by
default and has to be handed the write bit on purpose.

There is no delete, no re-tag, no edit, at any setting. Those are the operations
whose damage you cannot see happening, and the CLI is two keystrokes away.

An encrypted value stays the opaque `aisc:` marker in every reply. Decryption
asks a person for a passphrase at a terminal, so there is no unlocked vault
behind this server and nothing for an agent to drain.

A document's value is its file path inside the index. The agent can read that
file if it should; the index points at documents rather than holding them.

## What it is not

It is not a vector store and not RAG: recall is an exact lookup on the keys you
chose, with no embedding, no index to rebuild and no similarity threshold.

It is not a model deciding what to remember on your behalf. You name the keys,
or the agent asks you for them.

It is not a server: no daemon, no port, no account, nothing leaves the machine.

Implementation notes, the wire format and the test recipe are in
[`dev/MCP.md`](dev/MCP.md).
