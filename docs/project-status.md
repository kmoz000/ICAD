# ICAD v0.21 project status

This is the final cleanup audit for the current compiler milestone. It
separates proven v0.21 capability from the work still required for the complete
industrial agentic CAD platform.

## Current repository inventory

- 18 implementation modules under `src/`;
- 40 C++ implementation files, all compiled by CMake;
- 37 public headers, all consumed by implementation or tests;
- 29 unit/fuzz executables plus integration, sandbox, optional browser, and 12 benchmark cases;
- 10 maintained `.icad` examples;
- one build tree: `build/`;
- one geometry engine and no OpenCASCADE dependency.

The robotic reference assets are retained because the benchmark actively reads
all 10 STL components and the reference STEP assembly. Generated MCP and source
document workspaces outside `build/` are disposable and ignored.

## Complete for v0.21

The compiler frontend, physical units, canonical IR, materials and embedded
textures, primitives, validated polygon/arc/circle profiles, extrusion and full
line-profile revolution, deterministic meshes, exact current-feature topology,
analytic current-feature area/volume, body poses, semantic mechanism graphs,
constraints, scenes, viewer bundle, STEP/STL/OBJ, drawings, BOM/manufacturing
reports, revisions, LSP diagnostics, MCP tools, plugin, and quality gates are
implemented and tested. Exchange output includes analytic AP214 STEP mapping
for ICAD's exact curves and surfaces, STL, OBJ, embedded-buffer glTF 2.0, binary
GLB, OPC 3MF, and genuine 2D R2013 ASCII DXF. Every new package has native
structural read-back coverage. The native geometry engine now also owns deterministic
sweep-and-prune AABB indexing, segment/plane, ray/triangle, triangle/triangle,
and point-in-solid classification. Agent inspection separates penetrating and
contained polyhedral solids from surface-only contacts. The
spatial language now evaluates dependency graphs containing offset points,
between-point vectors, and axis-angle rotated vectors, while rejecting unknown
or cyclic expressions. Named values can drive constraint targets. Inspection
retains expression provenance and exposes per-body geometry summaries plus a
named cross-body contact graph so an agent can form a compact spatial picture.

The first advanced-modeling package is now complete. Features support ordered
`NEW`, `UNION`, `CUT`, and `INTERSECT` semantics using ICAD's native BSP
classifier. Reconstruction welds vertices, repairs T-junctions with conforming
boundary splits, removes degenerate and duplicate triangles, and separates
disconnected components. Every result must pass closed oriented-shell and
Euler validation. Boolean volume and bounds feed analysis, STEP/STL/OBJ share
the repaired geometry, and agent inspection exposes operation and repair
provenance. Boolean output is honestly identified as faceted B-Rep.

The second advanced-modeling package adds stable `SELECT EDGE NEAREST point`
semantics across all 12 sharp edges of translated axis-aligned boxes, native
faceted `CHAMFER` and `FILLET`, vector-driven `LINEAR_PATTERN`, and named-plane
`MIRROR`. Patterns and mirrors separate disconnected output into independently
named valid solids. Selection intent, operation parameters, and reconstruction
actions are visible in inspection. The maintained modeling example and
benchmark validate eight STEP/STL read-back solids. Fillet tessellation uses a
documented deterministic eight-segment arc; it is not mislabeled analytic.

The third advanced-modeling package adds named-point fixed-frame polyline
`SWEEP`, two-profile `LOFT`, deterministic multi-section twisted `FREEFORM`,
and full curved-profile `REVOLVE`. Profile resampling, section construction,
cap triangulation, topology validation, inspection provenance, and all delivery
meshes are owned by ICAD. The maintained surface benchmark validates four
STEP/STL read-back solids and 2,148 facets. These results are deliberately
identified as faceted B-Reps rather than NURBS or analytic toroidal surfaces.

The dimensional-sketch package adds named 2D point seeds, fixed anchors, and
horizontal, vertical, coincident, distance, and unsigned angle equations. The
owned deterministic solver reports convergence iterations, maximum residual,
Jacobian-rank degrees of freedom, and solved coordinates. Inconsistent systems
fail semantic compilation, while consistent closed loops become canonical
profiles for downstream solid features. Its maintained benchmark solves a
parameter-driven rectangle to zero DOF, extrudes it, and reads the solid back
from STEP and STL.

The source-module package adds project-root-confined `IMPORT`/`INJECT`
composition with canonical path checks, `.icad` filtering, cycle rejection,
depth and aggregate-size limits, dependency reporting, CLI/LSP/live-viewer
integration, and live dependency invalidation.

The dependency package adds a stable semantic DAG spanning parameters, angles,
spatial expressions, sketches, profiles, ordered features, bodies, poses,
materials, constraints, and scenes. Agent inspection exposes direct edges and
deterministic evaluation order. The stateful incremental compiler fingerprints
resolved dependencies per body, reuses unchanged validated topology and delivery
meshes, recomputes dirty bodies on a bounded worker pool, protects coherent
cache revisions across concurrent callers, removes stale cache entries, and
merges results in source order. Its benchmarks prove two-body full reuse followed by one-body recomputation and
8-of-10 mesh reuse on the robotic arm. The live viewer coalesces edits on a
background worker, passes the compiled model directly to its canvas (no
temporary iframe), and exports the complete package to a selected folder
without blocking preview compilation. The viewer starts stationary and adds a
lit dim studio grid, orthographic view cube, component/scene menus, per-scene
play actions, and synchronized direct geometry picking.

Agent visual feedback now has its own deterministic compiler product. The
`visual-json` command and `icad.visualize` MCP tool rasterize the current
delivery model into depth-resolved 64x32 front, right, top, and isometric grids
with a stable body legend, bounds, triangle counts, and current joint state.
The maintained robot benchmark requires these views and recognizable detailed
gripper/gear components, so model acceptance is no longer based only on counts.

The query package adds a dimensioned project-wide linear/angular tolerance
policy to canonical IR, revisions, agent inspection, and contact/query
classification. The native closest-distance implementation evaluates the exact
Euclidean minimum on validated polyhedral boundaries and reports closest
points. Plane sections return deterministic body/part-provenance segments.
Both APIs expose representation metadata so curved tessellation is not called
analytic. The CLI benchmark locks a 10 mm result and a 0.001 mm section policy.

The assembly-occurrence package adds `INSTANCE name OF body` reuse with named
seed transforms. Joints accept body or instance occurrences; revolute and
prismatic values are solved through instance parent chains around arbitrary
named axes. Solved geometry feeds topology, metrics, all exchange outputs, and
agent-visible bounds. The benchmark reuses one definition twice, solves a 90°
revolute occurrence, and reads back three STEP solids.

The assembly-semantics package adds source-level `FACE` and `EDGE` mates with
stable world-semantic selectors, explicit offset/tolerance targets, dependency
edges, build-blocking validation, and agent-visible actual values. Native
polyhedral interference classification now distinguishes crossing or contained
solid volume from surface-only contact. `JOINT` scene tracks accept dimensioned
values, enforce joint limits, export solved pivots and axes, and animate the
complete descendant occurrence chain in the dependency-free browser viewer.
The maintained acceptance model validates two mates, a non-penetrating face
contact, inherited instance material/BOM ownership, three articulated
keyframes, and STEP read-back.

The product-layer package adds range-checked material blocks with base-color,
metallic, roughness, physical texture scale, and UV mode overrides. Scenes own
easing, finite loops, lights, events, and visibility tracks. The browser library
prefers a WebGL2 depth backend and provides semantic selection, measurements,
section clipping, explode control, accessible input, and Canvas fallback. A
pinned optional `webview/webview` desktop host opens the same compiled HTML.

The agent/editor package adds LSP completion, declaration navigation,
formatting, and document lifecycle to diagnostics. MCP exposes structured
distance, interference, and section queries beside revision-checked project
patch/build operations. A bundled VS Code extension launches the language
server, compiler, and viewer. GitHub Actions build and test native CLI/viewer
packages on Linux, Windows, Intel macOS, and Apple Silicon macOS, package the
VSIX, and publish all artifacts from version tags.

The low-iteration agent package embeds the maintained robotic-arm and advanced
bridge source models into the binary. `agent-create` and `icad.agent.create`
turn a recognized short prompt into reviewed editable source plus the complete
artifact set in one invocation. The composite readiness report combines
compiler diagnostics, constraints, manufacturing, topology, measurements, and
interference; atomic multi-parameter edits collapse related dimensional changes
into one revision. Its benchmark proves the robotic prompt path at 10 bodies,
10 joints, 7 driven degrees of freedom, and a structurally readable 24-solid
STEP assembly with one expected model iteration. The response now embeds prompt
interpretation, assumptions, editable scalar/angle handles, and a compact design
map containing bounds, spatial references, joint connectivity, contacts, and
animation, so an agent does not need another call merely to understand layout.
The source now uses toothed gear profiles, tapered arm and wrist shells, hooked
opposed gripper fingers, linkage profiles, and four flange fasteners instead of
accepting a primitive-box proxy as the visual benchmark.

The engineering package adds process-aware material compatibility and checks
for wall proxy, hole diameter, tooling radius, bend radius, overhang, stock,
and tolerances. SVG and DXF sheets now use projected native edges and include
dimensions, datums, general tolerance, title-block data, and BOM count. A
deterministic compiler fuzz smoke and live headless-Chromium viewer smoke join
the sanitizer, Werror, structural read-back, robotic-arm, and bridge gates.

## Remaining release boundary: external certification

All locally testable work packages in this milestone are implemented. A public
industrial certification still requires release artifacts to be imported in
named downstream CAD applications and platform binaries to be signed/notarized
with project-owned credentials. Projected drawing edges are exact delivery-mesh
edges; standards-specific GD&T authoring and analytic hidden-line removal remain
future language depth and are not overclaimed as certified drafting.

DWG is intentionally not counted as a 3D target: ICAD will not disguise mesh
data as DWG. The implemented DXF is genuine 2D drawing data.
