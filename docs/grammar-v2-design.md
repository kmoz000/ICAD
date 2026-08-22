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

Current support syntax is `ON PLANE XY|XZ|YZ` or `ON FACE feature selector`.
Face support must refer to an earlier feature in the same body.

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

`LINE` and circular `ARC` entities are **current** in a single-contour sketch.
The enclosing multi-shape block and qualified `sketch.shape` selector are
**next**. Later curve types must lower to the same analytic path interface:

- `CIRCLE name CENTER point RADIUS value`
- `ELLIPSE name CENTER point RX value RY value ROTATION angle`
- `SPLINE name THROUGH p0 p1 ... DEGREE n [CLOSED]`
- `SLOT name FROM point TO point WIDTH value`
- `POLYGON name CENTER point SIDES n RADIUS value ROTATION angle`

## Constraints

Constraints address stable point, entity, shape, datum, face, edge, body, or
component names. Geometric constraints determine form; dimensional constraints
determine size; assembly constraints determine placement and motion.

Shape-level targets include:

- coincidence, horizontal, vertical, parallel, perpendicular, tangent;
- equal length, equal radius, concentric, midpoint, symmetry;
- distance, horizontal distance, vertical distance, radius, diameter, angle;
- fixed, construction, projected/reference geometry.

Constraints use qualified names when ambiguity is possible:

```icad
CONSTRAINT bore_pitch H_DISTANCE bore_left.center bore_right.center 90 mm
CONSTRAINT bore_pair EQUAL_RADIUS bore_left.circle bore_right.circle
CONSTRAINT centered SYMMETRIC bore_left.center bore_right.center ABOUT y_axis
```

The solver must report status, degrees of freedom, residual, conflicting
constraint names, and movable entities. “Compiled” is not equivalent to “fully
constrained.”

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
