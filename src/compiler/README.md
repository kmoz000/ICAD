# Compiler frontend

The C++ compiler owns source locations, diagnostics, lexical analysis, parsing,
AST, units, types, resolution, semantic checks, and canonical IR lowering.
Phases depend only toward lower-level compiler modules and never on geometry or
viewer code. The implemented native parser follows `grammar/icad.ebnf` and
requires no parser-generator runtime.

Canonical IR includes typed scalar/angle values, parameter-driven 3D points,
normalized and axis-angle-rotated vectors, retained spatial expression
dependencies, world-space body poses, and an acyclic mechanism joint graph in
addition to profiles, features, materials, constraints, and scenes. Mixed
point/vector expressions are dependency-resolved and diagnosed for cycles.

Canonical IR also produces a stable project dependency DAG for agent
inspection. The public `IncrementalCompiler` performs frontend lowering, hashes
each body's resolved geometric dependency slice, reuses unchanged validated
topology, and recomputes dirty bodies in source order. It never writes a second
build tree or delegates caching to an exporter.
