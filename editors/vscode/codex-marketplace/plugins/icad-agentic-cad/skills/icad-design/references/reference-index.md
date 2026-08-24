# ICAD design reference index

Use these references together, with a strict authority order:

1. The running compiler's `icad.language` response defines accepted syntax.
2. `icad-grammar.ebnf` is the packaged, offline grammar snapshot.
3. `modeling-contract.md` defines the sketch-to-assembly modeling discipline.
4. `blueprint-concept-pass.md` is the token-efficient operational extraction.
5. `blueprint-reading-complete.pdf` is the page-preserving source reference.

For ordinary ICAD generation, read the Markdown concept pass and modeling
contract, then consult the EBNF for exact productions. Do not inject the full
PDF into every prompt. Consult the PDF only for drawing conventions, view
selection, title blocks, dimensions, tolerances, sections, auxiliary views, or
other page-level evidence not represented in the compressed guide.

For generated source, begin with `REQUIRES ICAD 1.0` and add only capability
names returned by the running compiler. Requirement headers are a compatibility
contract, not permission to emit proposal-only syntax.

The production expression layer is limited to parameters, angles, and feature
properties. Query for `PARAMETER_EXPRESSIONS_V1` and
`QUALIFIED_VALUE_REFERENCES_V1` before emitting formulas or project-qualified
scalar names; broader scoped topology references remain proposal syntax.

When `MULTI_SHAPE_SKETCH_V1` is advertised, the production sketch subset
includes named open/closed shapes, four explicit region roles, point/line/arc/
circle entities, qualified point/entity constraints, full-solve enforcement,
and explicit `sketch.shape` PAD/POCKET inputs. When advertised,
`SKETCH_REGION_ARRANGEMENT_V1` adds explicit outer/hole arrangements and
`ADVANCED_SKETCH_CONSTRAINTS_V1` adds dimensional, line, circular, midpoint,
and symmetry equations. `SKETCH_LINE_ARC_TANGENCY_V1` adds explicit shared
endpoint line/arc tangency. `SEMANTIC_EDGE_LOOP_SELECTION_V1` adds
`TOP|BOTTOM INNER|OUTER` circular rim selection for FILLET/CHAMFER and exposes
applicable operations. `TOPOLOGY_QUERY_V1` adds a named circular annular
edge-loop query with convexity and adjacent-cap predicates, stable matched ID,
match evidence, and explicit operation rejection reasons. Richer curves, role
selectors, broader tangency, arbitrary chains, and remapped general topology
selectors remain proposal syntax.

When `PERSISTENT_FACE_REFERENCES_V1` is advertised, the production persistent
subset consists of body-local `feature.face.top|bottom` paths, optional `FACE`
aliases, strict earlier-feature ordering, and canonical `supportTopologyId`
feedback. Do not emit side-face, edge, plural, or geometric fallback selectors.

Blueprint knowledge controls how design intent is interpreted. ICAD grammar
controls how that intent is expressed. If the packaged EBNF and the running
compiler disagree, use `icad.language`, report the mismatch, and never invent
syntax.

Packaged source metadata:

- File: `blueprint-reading-complete.pdf`
- Pages: 72
- SHA-256: `049fd7bf224fe7de9278196f416d0b14e2042dab08e14d25b1f13ebfd2b878df`
- Grammar source: repository `grammar/icad.ebnf`
