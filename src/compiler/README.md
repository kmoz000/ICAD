# Compiler frontend

The C++ compiler owns source locations, diagnostics, lexical analysis, parsing,
AST, units, types, resolution, semantic checks, and canonical IR lowering.
Phases depend only toward lower-level compiler modules and never on geometry or
viewer code. The implemented native parser follows `grammar/icad.ebnf` and
requires no parser-generator runtime.

The post-v0.21 lexer foundation retains comments, recognizes reserved v2
punctuation and JSON-style string literals, accepts decimal exponent spellings,
normalizes CRLF/CR/LF into newline tokens, and records exact byte ranges plus
exclusive end locations. The current parser filters retained comments and
continues to consume the production grammar unchanged. Signed literals such as
`-12.5` deliberately remains one number token for source compatibility and is
handled by the typed-expression parser. Recognizing a proposed token does not
enable any unadvertised v2 declaration or capability.

Language requirements are preflighted before normal parsing. `REQUIRES ICAD
MAJOR.MINOR` is checked against production language version `1.0`, while
`REQUIRES CAPABILITY NAME` is checked against the centralized registry in
`compiler/language.cpp`. Supported declarations are retained in the AST.
Unsupported requirements stop compilation with one `ICAD-C` diagnostic before
future syntax is examined. CLI `icad language` and MCP `icad.language` consume
the same registry.

`compiler/expression.cpp` owns the production scalar-expression parser and
typed evaluator. The v1 gate covers parameter, angle, and feature-property
expressions plus project-qualified value references. It evaluates to canonical
units, retains source/dependency provenance, supports forward references, and
rejects unknown names, cycles, incompatible dimensions, unsupported derived
dimensions, and division by zero.

Canonical IR includes typed scalar/angle values, parameter-driven 3D points,
normalized and axis-angle-rotated vectors, retained spatial expression
dependencies, world-space body poses, and an acyclic mechanism joint graph in
addition to profiles, features, materials, constraints, and scenes. Mixed
point/vector expressions are dependency-resolved and diagnosed for cycles.

The preferred modeling surface is an ordered body-local feature history. A
`SKETCH ... ON PLANE` creates the datum profile, `PAD ... NEW` creates the first
solid, and `SKETCH ... ON FACE` selects a named earlier feature face for a
following `PAD ... ADD` or `POCKET`. AST and canonical IR retain the sketch
plane, support feature, face selector, operation, and source order. The native
geometry layer alone resolves those references into oriented solids; the
frontend never depends on geometry code. Low-level `FEATURE` remains available
for advanced operations and compatibility, but is not the primary authoring
path.

Body-local sketches always declare `ON PLANE` or `ON FACE`, must be consumed by
a later operation, and resolve to `body::sketch` in canonical IR. This permits
clear local names without collisions between parts. `PAD` and `POCKET` remain
distinct source commands through AST, IR, CLI inspection, dependency analysis,
incremental fingerprints, and agent-facing feature history rather than being
flattened into anonymous extrusion records.

Canonical IR also produces a stable project dependency DAG for agent
inspection. The public `IncrementalCompiler` performs frontend lowering, hashes
each body's resolved geometric dependency slice, reuses unchanged validated
topology, and recomputes dirty bodies in source order. It never writes a second
build tree or delegates caching to an exporter.
