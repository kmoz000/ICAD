# ICAD grammar v2: sketch, shape, feature, component

This document is the target language contract for industrial, agent-authored
CAD. It separates 2D intent, solid history, product structure, engineering
material, visual appearance, and mechanism state. Syntax marked **current** is
implemented by the compiler. Syntax marked **next** defines the next compatible
grammar layer and must not be emitted by tools until its parser gate lands.

The detailed semantics, capability gates, compiler stages, diagnostics, and
acceptance order are specified in the [language v2 RFC](grammar-v2-rfc.md). Its
complete syntax is recorded separately in the
[v2 proposed EBNF](../grammar/icad-v2-proposal.ebnf), with the implementation
boundary defined by the
[v2 compiler and engine architecture](compiler-v2-architecture.md). The only
production grammar remains [icad.ebnf](../grammar/icad.ebnf).

## Dependency model

```text
parameters and datums
        |
        v
sketch workspace -> named 2D shapes -> dimensional constraints
        |                    |
        +--------------------+
                     |
                     v
ordered features -> stable faces and edges -> manufacturable body
                     |
                     v
component interfaces -> assembly occurrences -> joints and mates
                     |
                     v
scene states and animation -> visual.json feedback -> exports
```

An agent must be able to identify which declaration produced every visible
surface. Mesh triangles are delivery data, never modeling authority.

## Sketch workspace

A `SKETCH` is a constrained planar document, similar to an SVG document with a
CAD coordinate system. It owns a support plane or a stable prior face, local
datums, shapes, and constraints. It does not mean “one polygon.”

```icad
SKETCH plate_layout ON PLANE XY
  DATUM center AT 0 mm 0 mm
  SHAPE outline CLOSED ROLE STOCK
    # path entities
  END
  SHAPE bore_left CLOSED ROLE HOLE
    # path entities
  END
  CONSTRAINT mirror_bores SYMMETRIC bore_left bore_right ABOUT y_axis
END
```

Current support syntax is `ON PLANE XY|XZ|YZ`, legacy `ON FACE feature selector`,
or—under `PERSISTENT_FACE_REFERENCES_V1`—direct
`ON FACE feature.face.top|bottom` and named
`FACE alias FROM feature.face.top|bottom`. Face support must refer to an earlier
feature in the same body. Side-face lineage, selection queries, projection, and
edge aliases remain capability-gated future slices.

## Named shape paths

A `SHAPE` is one named path or construction group. Closed shapes can create or
remove material. Open shapes can drive sweeps, engravings, split operations,
and reference geometry. The target shape grammar is:

```ebnf
shape = "SHAPE", identifier, ("OPEN" | "CLOSED"), "ROLE", shape_role,
        newline, { shape_item }, "END", newline ;
shape_role = "STOCK" | "ADDITIVE" | "HOLE" | "SLOT" | "ENGRAVE" |
             "CUT_PATH" | "CONSTRUCTION" ;
shape_item = point | line | arc | circle | ellipse | spline | slot | polygon ;
```

Every entity and point has a stable name. A path is geometric topology, so its
order is explicit and independent from the order in which points were declared.

```icad
SHAPE outer CLOSED ROLE STOCK
  POINT a -60 mm -40 mm FIXED
  POINT b  60 mm -40 mm
  POINT c  60 mm  40 mm
  POINT d -60 mm  40 mm
  LINE bottom FROM a TO b
  LINE right FROM b TO c
  ARC top FROM c TO d CENTER top_center CCW
  LINE left FROM d TO a
END
```

`MULTI_SHAPE_SKETCH_V1` is **current** for `STOCK`, `ADDITIVE`, `HOLE`, and
`CONSTRUCTION` roles; `OPEN|CLOSED` paths; named `POINT`, `LINE`, circular
`ARC`, and `CIRCLE`; qualified `shape.point` constraints; and explicit
`sketch.shape` PAD/POCKET inputs. Open paths are currently construction-only.
Role selectors and later curve types must lower to the same analytic path
interface before they can be advertised:

- `CIRCLE name CENTER point RADIUS value`
- `ELLIPSE name CENTER point RX value RY value ROTATION angle`
- `SPLINE name THROUGH p0 p1 ... DEGREE n [CLOSED]`
- `SLOT name FROM point TO point WIDTH value`
- `POLYGON name CENTER point SIDES n RADIUS value ROTATION angle`

`SKETCH_REGION_ARRANGEMENT_V1` is **current**. It groups already validated
closed shapes into an explicit material arrangement:

```icad
REGION plate_with_bores
OUTER outer
HOLES bore_left bore_right
END
```

The outer must be `STOCK` or `ADDITIVE`; every hole must be a contained `HOLE`
shape. `PAD` and `POCKET` accept `sketch.region`, and inspection exposes the
net area plus all contributing profiles.

## Constraints

Constraints address stable point, entity, shape, datum, face, edge, body, or
component names. Geometric constraints determine form; dimensional constraints
determine size; assembly constraints determine placement and motion.

Shape-level targets include:

- coincidence, horizontal, vertical, parallel, perpendicular, tangent;
- equal length, equal radius, concentric, midpoint, symmetry;
- distance, horizontal distance, vertical distance, radius, diameter, angle;
- fixed, construction, projected/reference geometry.

`ADVANCED_SKETCH_CONSTRAINTS_V1` is **current** for horizontal/vertical
distance, parallel, perpendicular, equal length, concentric, equal radius,
midpoint, and point-pair symmetry about a line entity.
`SKETCH_LINE_ARC_TANGENCY_V1` is **current** for endpoint-specific line/arc
tangency using `TANGENT line arc AT shared_point`. Full-circle, arc-arc,
spline, projected-geometry, radius/diameter solver variables, and shape-level
tangency remain future gates.

Constraints use qualified names when ambiguity is possible:

```icad
CONSTRAINT bore_pitch H_DISTANCE bore_left.center bore_right.center 90 mm
CONSTRAINT bore_pair EQUAL_RADIUS bore_left.circle bore_right.circle
CONSTRAINT centered SYMMETRIC bore_left.center bore_right.center ABOUT y_axis
CONSTRAINT rounded TANGENT outline.bottom outline.end_arc AT outline.corner
```

The solver must report status, degrees of freedom, residual, conflicting
constraint names, and movable entities. “Compiled” is not equivalent to “fully
constrained.”

### Selection and applicability

Selection is a typed query result, not an integer mesh index. The first
production gate, `SEMANTIC_EDGE_LOOP_SELECTION_V1`, classifies a circular rim
on the current annular solid by axial location (`TOP|BOTTOM`) and material side
(`INNER|OUTER`). Agent feedback returns the selection kind, classification,
and applicable operations. Today that list is exactly `FILLET` and `CHAMFER`.
Shell, offset, split, project, draft, and arbitrary edge-chain operations stay
unadvertised until their native geometry and failure diagnostics exist.

The next complete slice, `TOPOLOGY_QUERY_V1`, gives that result a body-local
name and makes its evidence explicit: `SELECTION name`, `FROM feature`, `EDGES
WHERE`, `LOOP`, `CIRCULAR`, `CONCAVE|CONVEX`, and `ADJACENT_TO FACE
top|bottom`. `SELECT EDGESET name` is permitted only for FILLET/CHAMFER and the
source must be the immediately preceding annular REGION extrusion. This narrow
rule is deliberate: ICAD rejects a stale query instead of pretending that a
mesh edge index survived an intervening feature. `visual.json` returns the
matched topology ID, a human-readable match reason, valid operations, and why
shell/offset/split are not applicable.

## Industrial capability ladder

ICAD grows by complete vertical capabilities, not by accepting disconnected
keywords. A capability may be advertised only when it has all of these pieces:
formal grammar, typed AST and IR, semantic validation, native execution,
persistent provenance, `visual.json` evidence, actionable diagnostics, focused
unit tests, an acceptance model, and exchange read-back where the operation
changes manufactured geometry.

The planned layers are ordered by dependency:

| Layer | Language and engine contract | Agent-visible result |
|---|---|---|
| Constrained profiles | Ellipse, conic, slot, polygon, B-spline, trim, extend, offset, project, construction entities, complete dimensional and geometric constraints | Entities, remaining DOF, conflicts, regions, winding, and manufacturability warnings |
| Persistent topology | Face/edge/vertex identities; queries by geometry, ancestry, role, adjacency, loop, chain, convexity, and concavity; named selection sets | Stable selections and a computed applicable-operation list after every feature |
| Native solid history | Typed holes, ribs, grooves, shell, thicken, draft, split, face/edge offset, robust fillet/chamfer chains, sweep frames, loft continuity, linear/circular/table patterns | Immutable feature results, source-to-result maps, failed-operation cause, and local repair suggestions |
| Surface design | Analytic and NURBS curves/surfaces, boundary and fill patches, trim, extend, stitch, thicken, and explicit G0/G1/G2 continuity | Patch network, continuity residuals, gaps, curvature bounds, and watertightness |
| Product structure | Parts, configurations, interfaces, subassemblies, flexible/rigid groups, joints, contacts, limits, motion envelopes, and kinematic goals | Solved occurrence graph, connection state, range of motion, collisions, and unreachable targets |
| Industrial definition | Physical material separate from appearance, process intent, standard fasteners and holes, fits, tolerances, datum systems, GD&T, BOM, revision and drawing associations | Mass and process evidence, standards checks, tolerance stack, drawing references, and release readiness |

Each selector is evaluated against the current immutable feature result. The
engine returns what the selected entity is, why it matched, what operations are
legal, and why a requested operation is rejected. This makes modeling legible
to an agent before it edits source and prevents mesh tessellation from becoming
the language's hidden topology model.

## Feature history

Features consume named shapes and produce a new immutable history result with
stable semantic selectors. A later step may use an earlier face; it cannot refer
to a future or deleted result.

```icad
PAD base FROM plate_layout.outer DEPTH 12 mm NEW
POCKET bolt_pattern FROM plate_layout[ROLE HOLE] THROUGH ALL

SKETCH boss_layout ON FACE base.TOP
  SHAPE boss CLOSED ROLE ADDITIVE
    CIRCLE rim CENTER origin RADIUS 28 mm
  END
END
PAD motor_boss FROM boss_layout.boss DEPTH 30 mm ADD

FILLET edge_rounds ON motor_boss
  SELECT EDGES TANGENT_TO motor_boss.TOP
  RADIUS 3 mm
END
```

The operation families are:

- form: pad/extrude, revolve, sweep, loft, shell, thicken;
- remove: pocket, hole, groove, cut sweep, split;
- modify: fillet, chamfer, draft, offset face, move face, replace face;
- repeat: linear, circular, mirror, table-driven pattern;
- reference: datum plane, axis, point, coordinate system, projected geometry.

Selectors such as `base.TOP` are semantic references. Generated identifiers in
topology JSON are the authority when a simple role selector is insufficient.

## Body, component, and assembly

A `BODY` is one continuous manufacturable solid. A `COMPONENT` is a product
unit containing one or more bodies, named interfaces, material assignments,
mass properties, and local mechanism datums. An `ASSEMBLY` contains component
occurrences and relationships.

```icad
COMPONENT elbow_module
  BODY housing
    # ordered sketch and feature history
  END
  INTERFACE shoulder_axis AXIS shoulder_center z_axis
  INTERFACE wrist_mount FACE housing.mount_face
END

ASSEMBLY robot
  OCCURRENCE elbow OF elbow_module
  JOINT elbow_pitch REVOLUTE upper_arm.elbow_axis elbow.shoulder_axis
    VALUE 0 deg LIMIT -110 deg 135 deg
  END
END
```

Joint animation changes descendants around the declared anchor and axis. It
must never rotate an unrelated whole structure. At every sampled frame the
engine must report disconnected interfaces, interference, limit violations,
and the transformed component bounds.

## Engineering material and visual appearance

Engineering material and visible skin are different contracts:

```icad
MATERIAL aluminum_6061
  STANDARD ASTM_B209
  DENSITY 2.70 g_cm3
  YIELD_STRENGTH 276 MPa
END

APPEARANCE anodized_blue
  BASE_COLOR 0.04 0.18 0.55 1
  METALLIC 0.85
  ROUGHNESS 0.28
  TEXTURE anodized_micrograin SEED 3301 SCALE 0.35 mm
  UV_MODE BOX
END

BODY housing
  MATERIAL aluminum_6061
  APPEARANCE anodized_blue
END
```

Preset procedural textures stay embedded and deterministic. External texture
imports, when added, must be project-relative, sandboxed, content-hashed, and
packaged into scene outputs. Appearance must never silently substitute for
physical density, strength, tolerance, or process data.

## Manufacturability transition

Before a closed shape becomes material, the compiler validates:

- one closed ordered boundary per contour and defined outer/hole winding;
- no self-intersection, zero-length entity, duplicate edge, or invalid arc;
- coplanarity and tolerance-resolved endpoints;
- minimum line, radius, web, wall, and hole dimensions for the selected process;
- operation depth, tool access, draft, and remaining wall thickness;
- manifold solid output and persistent source-to-face provenance.

Process validation is explicit (`MILLING`, `TURNING`, `LASER`, `SHEET_METAL`,
`ADDITIVE`) and produces warnings or errors tied to source entities.

## Agent and viewer contract

`visual.json` is the compact spatial feedback loop. It must include:

- sketch workspaces, shapes, entities, constraints, DOF, and solver conflicts;
- ordered feature history and source shape for every result;
- stable faces/edges, body/component hierarchy, material and appearance;
- per-joint parent/child attachment gaps and connection state;
- sampled scene transforms, limits, interference, and disconnected components;
- front, right, top, and isometric depth rasters plus per-component bounds.

The native engine library owns compile, solve, geometry, scene evaluation, and
serialization. Desktop and editor viewers only display engine results and send
source edits; they do not reimplement CAD semantics in JavaScript.

## Compatibility and migration

Existing `PROFILE`, primitive `FEATURE`, point-only `SKETCH`, and single
`CIRCLE` sketch syntax remain readable during migration. Formatter and LSP
fixes should offer conversion to named entities and shapes. New agent-generated
industrial designs should use the newest fully implemented layer reported by
the MCP `icad.language` tool, never target syntax that the running compiler
does not advertise.
