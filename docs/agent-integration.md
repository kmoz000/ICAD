# Provider-neutral agent integration

ICAD 0.21 exposes its compiler, durable document model, and artifact pipeline through Model Context
Protocol stdio without an SDK dependency. Any MCP host can use the same tools;
the model vendor is outside ICAD's architecture.

## Start the server

```sh
/absolute/path/to/ICAD/build/bin/icad mcp \
  --workspace /absolute/path/to/design-workspace
```

The host launches this process and exchanges one UTF-8 JSON-RPC object per
line. Standard output contains protocol messages only. The server supports
`server/discover` for MCP `2026-07-28` and `initialize` for older MCP hosts.

## Modern discovery

```json
{"jsonrpc":"2.0","id":1,"method":"server/discover","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientInfo":{"name":"cad-agent","version":"1.0"},"io.modelcontextprotocol/clientCapabilities":{}}}}
```

Call `tools/list` to obtain deterministic JSON Schemas. The host can pass these
schemas unchanged to any model API that supports tool calling.

## Recommended design loop

1. Prefer `icad.agent.create` for recognized robot-arm or bridge requests. One
   call selects a maintained parametric source, performs the complete readiness
   review, commits it with `expectedRevision`, and builds every artifact.
2. For a new design class, call `icad.agent.bootstrap`. It returns valid source,
   explicit acceptance criteria, a named-parameter strategy, and the shortest
   next-tool sequence.
3. Make topology-level changes in source and call `icad.agent.review`. This one
   response combines compilation, constraints, manufacturing, topology,
   metrics, and interference, so the model only sees actionable failures. Read
   its `designMap` in order: overall bounds, body centers/sizes, named points and
   vectors, parent-child joints, contacts, then animation tracks.
4. Keep the exact hexadecimal revision returned by each durable operation.
   Group dimensional decisions into one `icad.project.set_parameters` call;
   reread on `ICAD-PROJECT-CONFLICT`.
5. Require user confirmation in the host UI for consequential designs, then
   build the reviewed revision if `icad.agent.create` was not used.
6. Verify returned artifacts, including `.topology.json`, and use
   `inspect-step`/`inspect-stl` for structural read-back.

The embedded templates are deterministic design accelerators, not an opaque
natural-language geometry service. The returned `.icad` file remains the
authoritative editable model, and all source still passes through the same
compiler and owned geometry engine.

### One-call prompt example

```json
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"icad.agent.create","arguments":{"prompt":"Create a detailed articulated industrial robotic arm with a gripper and animated joints","path":"designs/robotic_arm.icad","expectedRevision":"absent","outputDirectory":"artifacts/robotic_arm","modelName":"robotic_arm"}}}
```

The successful structured result contains `schema: icad.agent.create.v1`, the
selected intent, expected model-iteration budget, complete source and revision,
prompt interpretation and assumptions, editable parameter/angle handles,
composite review with `icad.agent.design-map.v1`, exact design counts, and every
generated artifact path.

When rounded geometry is required, prefer `START`/`LINE`/`ARC`/`CLOSE` or
`CIRCLE` profiles over dense `POINT` approximations. An `ARC` endpoint and its
start must be equidistant from `CENTER`; choose `CW` or `CCW` explicitly.
Inspect `icad.topology` to confirm circular edges and cylindrical extrusion
faces. Curved profiles are valid for full `REVOLVE`; those results are faceted
and must not be described as analytic toroidal surfaces.

For assemblies and mechanisms, declare values in dependency order: scalar
`PARAMETER` and typed `ANGLE` values, `POINT3` anchors, normalized `VECTOR`
axes, optional body `POSE` records, then `JOINT` relationships. Every moving
joint carries a current `VALUE` and ordered `LIMIT` values of the correct
physical dimension. Use `WORLD` only as a joint parent. The compiler rejects
unknown anchors, axes, or bodies; multiple parents for one child; graph cycles;
dimension mismatches; and drive values outside their limits. `icad.inspect`
resolves these declarations into a spatial table and mechanism graph with the
current degrees of freedom, allowing an LLM to reason about structure without
guessing from mesh triangles.

Prefer derived `POINT3 name FROM point ALONG vector DISTANCE value`, `VECTOR
name FROM point TO point`, and `VECTOR name ROTATE source AROUND axis BY angle`
declarations for link chains. ICAD evaluates mixed point/vector dependencies,
rejects cycles, and exposes both the resolved value and expression provenance
to inspection. Constraint targets may reuse compatible named parameters or
`ANGLE` values, avoiding duplicated numeric values.

`icad.inspect` also returns bounds, center, and size for every body plus a named
cross-body contact graph. Use those summaries before requesting full topology:
they give the model a compact spatial picture of the assembly and make isolated
or unexpectedly contacting components visible.

For sequential solid modeling, keep the base feature first and use `OPERATION
UNION`, `CUT`, or `INTERSECT` on each following operand. Inspection exposes the
boolean count and deterministic reconstruction repairs. Query topology after
every boolean edit: successful results are validated faceted B-Reps, and a
disconnected union is returned as multiple solids with stable component names.

Use `SELECT EDGE NEAREST point` for a fillet or chamfer so the source keeps
semantic intent instead of a triangle index. The current selector searches all
sharp edges of a translated axis-aligned box. Repetition uses
`LINEAR_PATTERN`, a named `DIRECTION`, integer `COUNT`, and typed `SPACING`;
symmetry uses `MIRROR` with `PLANE point NORMAL vector`. Inspection returns
these references in `modeling.operations` and reports every generated component
or faceted edge result in `modeling.repairs`.

Use `SWEEP` with a profile and a `PATH` of named 3D points for rail-like forms.
The current profile frame stays fixed in XY. Use `LOFT` with `TARGET_PROFILE`
and `HEIGHT` for a two-profile transition, or `FREEFORM` with `TWIST` and
`COUNT` for a deterministic multi-section morph. Inspection exposes every
surface operation, target profile, path, and section count. These shapes and
curved revolutions are validated faceted topology, not NURBS or analytic tori.

For constrained 2D design, declare approximate named point seeds in a `SKETCH`,
anchor intentional reference points with `FIXED`, and add horizontal, vertical,
coincident, distance, or unsigned angle equations. Reuse named parameters and
angles for dimensional targets. Inspection distinguishes `fullyConstrained`,
`underConstrained`, and `inconsistent`, exposes remaining DOF and solver
residual, and returns both initial and solved coordinates. A solved closed
sketch is also a profile with the same name, avoiding coordinate duplication.

Read the `dependencies` object before making a narrow edit. Each node has a
semantic ID and direct `dependsOn` list, and `evaluationOrder` is stable. Native
hosts that keep a compiler session alive can use `IncrementalCompiler`; its
report distinguishes reused, recomputed, and removed bodies. Cache decisions
use lowered dependency values, so changing an unrelated body's parameter does
not invalidate stable topology.

Declare `TOLERANCE LINEAR ... ANGULAR ...` when the design requires a policy
different from the conservative default. Use MCP `icad.distance` or CLI
`distance-json` for closest points and MCP `icad.section` or CLI `section-json`
for plane cuts.
Both results state their representation; `exactPolyhedral` is exact for the
delivery boundary but must not be presented as analytic curved-surface distance.

`POSE` is an explicit world-space body transform and affects meshes, bounds,
and exact topology. `INSTANCE name OF body` reuses a definition at a named
point and rotation. Joints targeting instances solve revolute and prismatic
values through their parent chain on delivery geometry; inspection exposes
solved bounds. Direct body joints remain compatible semantic mechanism records,
so use instances for driven assembly motion.

Tool failures use `isError: true` with a structured error object so models can
self-correct. Unknown protocol methods and unknown tool names use JSON-RPC
errors.

## Build confinement

The process receives one workspace root at launch. `icad.build` rejects:

- absolute output paths;
- `..` traversal;
- existing symlinks resolving outside the workspace;
- unsafe model names;
- compilation, constraint, or manufacturing failures.

Artifacts are generated in a unique staging directory and moved to their final
names only after all exporters succeed. ICAD does not execute source code,
download assets, invoke a CAD kernel, or contact an LLM provider.

## Durable collaboration

Project paths must be workspace-relative `.icad` files. All writes are compiler
validated before commit. A cross-process directory lock serializes competing
writers; the revision is compared while that lock is held, preventing a lost
update when multiple MCP processes edit the same model. Immutable source
snapshots support history listing and validated restore. Revisions are encoded
as fixed-width hexadecimal strings so JSON number precision cannot corrupt
optimistic concurrency.

Use `icad.project.set_parameters` when one design decision changes several
dimensions. All replacements are resolved from one snapshot, compiler checked
together, and committed under one new revision; a missing name or invalid value
leaves the previous source untouched.

## Direct tool-call example

```json
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"icad.compile","arguments":{"source":"PROJECT demo\nUNITS mm\nBODY part\nFEATURE cube\nTYPE BOX\nWIDTH 10 mm\nDEPTH 10 mm\nHEIGHT 10 mm\nEND\nEND\n"},"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientInfo":{"name":"cad-agent","version":"1.0"},"io.modelcontextprotocol/clientCapabilities":{}}}}
```

The Codex plugin at `/Users/kimo/plugins/icad-agentic-cad` provides a ready
local MCP configuration for this checkout. Other hosts only need the command
and argument configuration shown in the root README.
