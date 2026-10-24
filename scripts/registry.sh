#!/bin/sh
# registry.sh -- write server.json for the official MCP Registry from a
# published release's .mcpb, then publish it:
#   sh scripts/registry.sh v0.3.29 > server.json
#   sh scripts/registry.sh v0.3.29 dist/ais-v0.3.29.mcpb.sha256 > server.json   # CI: local sidecar
#   mcp-publisher login github      # once, as Anode1
#   mcp-publisher publish
# The hash comes from the release's own .sha256 sidecar, so the entry names
# exactly the file a client downloads and checks.
set -e
[ $# -ge 1 ] || { echo "usage: registry.sh vX.Y.Z [SHA256FILE] > server.json" >&2; exit 2; }
tag=$1; version=${tag#v}
url="https://github.com/Anode1/ais/releases/download/$tag/ais-$tag.mcpb"
if [ -n "$2" ]; then sha=$(cut -d' ' -f1 < "$2"); else sha=$(curl -fsSL "$url.sha256" | cut -d' ' -f1); fi
[ ${#sha} -eq 64 ] || { echo "registry: no sha256 in ${2:-$url.sha256}" >&2; exit 1; }
cat <<EOF
{
  "\$schema": "https://static.modelcontextprotocol.io/schemas/2025-12-11/server.schema.json",
  "name": "io.github.Anode1/ais",
  "title": "ais",
  "description": "Memory for agents and people: exact recall from a plain-text index, by your own keys.",
  "repository": { "url": "https://github.com/Anode1/ais", "source": "github" },
  "version": "$version",
  "packages": [
    {
      "registryType": "mcpb",
      "identifier": "$url",
      "fileSha256": "$sha",
      "transport": { "type": "stdio" }
    }
  ]
}
EOF
