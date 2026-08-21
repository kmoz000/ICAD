# Model Context Protocol bridge

The native MCP stdio server exposes compiler, language, material, validation,
measurement, inspection, and artifact-build tools. It uses the repository's
strict JSON implementation and reusable project builder. Current stateless MCP
discovery and legacy initialization are both supported. The write-capable tool
is confined to the workspace supplied when the process starts.

Durable project tools add exact revision reads, compiler-validated atomic
commits, single or batch parameter edits, immutable history, restore, and revision-pinned
builds. Conflicts and compiler diagnostics are structured tool errors suitable
for autonomous repair loops.

The embedded `icad.agent.bootstrap`, `icad.agent.review`, and
`icad.agent.create` tools provide the shortest path from a prompt to editable
source and artifacts. The one-call create tool still uses revision checks,
compiler validation, the owned geometry engine, and workspace confinement.
Its brief exposes assumptions and named edit handles; its review includes the
compact spatial/mechanism design map needed for the next model decision.

`icad.topology` returns the compiler-owned analytic entity graph with stable
semantic IDs. Staged builds include the same graph as `.topology.json`, so an
agent can inspect and reference geometry without relying on mesh indices.
