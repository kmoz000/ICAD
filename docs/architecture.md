# ICAD architecture

ICAD is an agent-first compiler with a single, repository-owned geometry
engine. External CAD kernels, desktop GUI frameworks, image libraries, JSON
libraries, and JavaScript frameworks are outside the core architecture.

This page describes the implemented architecture. The proposed lossless lexer,
v2 compiler passes, direct modeling-kernel layers, persistent topology, and
parallel revision model are specified separately in the
[ICAD v2 compiler and engine architecture](compiler-v2-architecture.md).

```text
.icad source
  -> root-confined IMPORT/INJECT module expansion and cycle detection
  -> source locations and stable diagnostics
  -> native lexer
  -> native parser and AST
  -> units, type schemas, and symbol resolution
  -> semantic validation
  -> canonical product/scene/material/spatial/mechanism/constraint IR
  -> ICAD native geometry model
       -> exact analytic topology and shell validation
       -> measurement and clearance analysis
       -> deterministic AABB broad phase and surface intersections
       -> direct analytic STEP B-Rep and assembly
       -> direct STL, Wavefront OBJ, glTF/GLB, and 3MF
       -> compiled web model
       -> BOM, manufacturing report, projected-edge SVG/DXF
  -> native material texture generator
  -> scene animation compiler
  -> plain JavaScript WebGL2 viewer bundle with Canvas fallback
  -> optional pinned webview/webview native host
       -> coalesced background preview queue
       -> incremental body topology and delivery-mesh cache
       -> independent complete-package export
  -> provider-neutral MCP tools and editor LSP
```

## Dependency direction

```text
diagnostics <- lexer <- parser <- AST
                         |
units <- types ----------+
AST <- resolver ----------+-> semantic -> canonical IR
                                           |
                            +--------------+--------------+
                            |              |              |
                       geometry        materials       scenes
                            |              |              |
                            +-- STEP / STL / OBJ / glTF / 3MF / web --+
                                           |
                              analysis / documents / drawings
```

Lexer, parser, resolver, and semantic-lowering code cannot depend on geometry
or presentation code. The top-level compiler driver runs the topology gate
after semantic lowering.
Exporters cannot create their own shapes: STEP, OBJ, and the web viewer all
consume the same validated native model. The CLI performs file I/O and output
selection only.

## Native geometry contract

Each compiled feature has two synchronized native representations. The exact
model owns stable semantic IDs for vertices, oriented edges, wires, faces,
shells, and solids plus line/circle curves and plane/cylinder/cone/sphere
surfaces. Its validator checks references, normalized frames, curve endpoints,
boundary-on-surface geometry, closed wires, opposite edge incidence, shell
ownership, and Euler characteristic. The
faceted model owns consistently wound triangles for fabrication and viewing.
Transforms are applied deterministically to both representations.
An optional world-space BODY pose composes after feature-local transforms in
both representations. Named points, normalized vectors, typed angles, retained
point/vector expression dependencies, and the validated joint parent graph
remain semantic IR rather than being reverse engineered from geometry.

This owned-kernel layer also triangulates validated polygon, line/arc path, and
circle profiles for extrusion, full revolution, fixed-frame polyline sweep,
profile loft, and twisted multi-section profile morphing. Arc
extrusions retain analytic circular edges and cylindrical side surfaces.
Area and volume analysis consumes analytic feature/profile definitions for all
current primitives, curved extrusions, and line-profile revolutions; curved
revolution measurements, bounds,
spatial candidate searches, and interference analysis consume deterministic
tessellation. Segment/plane, ray/triangle, triangle/triangle, and
point-in-polyhedral-solid tests are native and tolerance-aware. The classifier
separates volume penetration or containment from surface-only contact without
claiming analytic intersection volume.
Analytic toroidal surfaces and advanced healing remain future native layers
and must preserve the same canonical-IR boundary. STEP now maps validated exact
line/circle and plane/cylinder/cone/sphere topology directly; faceted advanced
features retain a valid faceted boundary representation.

The native 2D sketch solver uses deterministic damped Gauss-Newton iteration,
finite-difference Jacobians, pivoted linear solves, and Jacobian-rank DOF
classification. Fixed anchors are excluded from solver variables. Consistent
closed point loops become canonical line-segment profiles before geometry
construction; inconsistent systems stop semantic compilation.

The source-composition layer expands only declarative `.icad` fragments,
resolves paths relative to the importing module, confines canonical paths to a
declared project root, rejects cycles and non-ICAD files, and applies depth and
aggregate-size limits before lexing. No import path executes code.

The feature dependency layer builds a directed acyclic graph from canonical IR
and preserves source-stable evaluation order. Stateful incremental sessions
compile frontend semantics without eagerly creating topology, fingerprint each
body's resolved profile, spatial, material, pose, and ordered-feature slice,
then builds dirty bodies on a bounded pool and merges cached or newly validated
body topology and delivery meshes in source order. A revision mutex prevents
partially published cache state across concurrent callers. Live viewer requests
run on a coalescing worker so the UI thread remains responsive; unchanged source
returns compact cached metadata unless an imported file timestamp changed.
The compiled render model is passed directly through the webview bridge and
mounted in the workbench canvas, avoiding temporary-file iframe navigation.
Full artifact export uses a separate clean compilation and atomic project build,
so it cannot corrupt the interactive cache or block editor refresh.

The canonical tolerance policy is dimensioned IR and participates in document
fingerprints. Contact analysis, exact-polyhedral closest-distance queries, and
plane-section classification consume its linear tolerance. Query results carry
representation metadata so tessellated curved geometry is never overclaimed.

## Materials and scenes

Material presets and typed overrides are compiler data, not viewer-only styling.
Semantic lowering resolves them into PBR values, physical texture scale, UV
mode, and stable procedural texture metadata.
The scene exporter generates and base64-embeds deterministic BMP textures.

Animation tracks reference semantic BODY names, joints, visibility targets, or
named cameras. Time, translation, rotation, easing, events, lights, and loops
are dimension/range checked and normalized before export.
The web viewer only interpolates valid canonical keyframes; it does not repair
invalid source at runtime.

## Test layers

- Unit: lexer, parser, units, resolver, semantic analysis, profiles, native
  faceted and exact topology geometry, spatial indexing and intersections,
  spatial mechanisms, constraints, STEP/STL/OBJ/glTF/GLB/3MF, documents,
  manufacturing, agent JSON,
  LSP completion/navigation/formatting, materials, embedded textures, advanced
  scene timelines, and viewer bundles.
- Integration: CLI status and diagnostic contracts.
- Sandbox: execution from a clean directory without repository-relative data.
- Benchmark: the advanced bridge plus a robotic-arm comparison across IR, STEP
  assembly read-back, STL/OBJ topology, engineering artifacts, textures,
  animation, and HTML outputs.
- Quality: warnings-as-errors plus AddressSanitizer/UndefinedBehaviorSanitizer.

## Agent rule

Agents edit source and call compiler/inspection APIs. They never mutate mesh
arrays, generated STEP entities, or viewer data directly. Future MCP and LSP
interfaces use the same compiler and transaction boundaries so every
change remains deterministic, reviewable, and testable.

## Agent protocol boundary

The native JSON module is the only protocol parser. MCP accepts complete source
text, composes compiler/analysis/project APIs, and never changes IR or mesh data
directly. Its build operation is workspace-confined and uses the same staged
artifact transaction as the CLI. Current stateless MCP discovery and legacy
initialization intentionally share a deterministic tool catalog.

Durable source mutations use fixed-width revision strings, validate through the
complete compiler before writing, acquire a cross-process lock, archive both
old and new versions, and atomically rename a same-directory temporary file.
This makes optimistic concurrency explicit to the model and prevents silent
last-writer-wins behavior across independent agent hosts.
