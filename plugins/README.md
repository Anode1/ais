# AIS plugins

An AIS plugin is just an **executable** that extends `ais`. It does its work by
calling the `ais` command line (or reading the plain-text index directly). The
GUI wrappers in `../gui` are plugins in spirit; anything that speaks `ais` qualifies.

## Layout (one directory per plugin)

    plugins/<name>/
      manifest.json     name, version, the entry executable, a one-line summary
      ais-<verb>        the entry executable (any language; must be chmod +x)
      README.txt        what it does, how to run it
      sandbox/          isolated dev harness -- never touches real data
        run.sh          builds a throwaway index, seeds fixtures, runs the plugin
        fixtures/       sample inputs

Copy `hello_world/` to start your own: rename the exec to `ais-<yourverb>`,
update `manifest.json`, edit the exec, and test it in `sandbox/`.

## The contract (what a plugin may and may not do)

AIS is **append-only by policy** -- users and plugins are meant to *add*.

- A plugin **owns** its own behavior, output, language, and dependencies.
- **AIS owns the index**: the plain-text store, the single-writer lock, and
  idempotent/monotonic `put`. Therefore a plugin:
  - MUST mutate the index only through the `ais` CLI -- never write `store`,
    `idx/`, or `tomb` directly (that would break locking, ids, idempotency);
  - MAY read the plain-text store/idx directly (documented format) for speed;
  - inherits AIS's safety: any `del` / `del-key` / `compact` it triggers asks
    for confirmation unless `-y` is passed. Adding is free; removing is guarded.

## Installing a plugin

Drop its directory under `plugins/` (shipped with AIS) or under
`$XDG_DATA_HOME/ais/plugins/` (per-user; discovery prefers this one).

## Invocation (planned)

Because a bare `ais WORD` already means "get records under WORD", plugins are
run through a reserved dispatch verb rather than guessed from the name:

    ais x <name> [args...]        # (planned) run plugin <name>

AIS exports the resolved index location in `$AIS_INDEX` before exec, so the
plugin's own `ais` calls hit the same index. Until the dispatch verb is wired,
run a plugin directly:

    AIS_INDEX=/path/to/index plugins/<name>/ais-<verb> [args...]
