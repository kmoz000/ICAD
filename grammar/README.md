# ICAD grammar

`icad.ebnf` documents the syntax implemented by ICAD's native C++ lexer and
parser. It is a reference for contributors and agent-tool authors; no parser
generator or grammar runtime is required by the compiler.

## Production grammar and v2 proposal

- [`icad.ebnf`](icad.ebnf) is the only syntax accepted by the current compiler.
- [`icad-v2-proposal.ebnf`](icad-v2-proposal.ebnf) is a non-production RFC for
  multi-shape sketching, persistent topology, industrial features, parts,
  assemblies, validation, drawings, and richer agent feedback.
- The [language v2 RFC](../docs/grammar-v2-rfc.md) defines the semantics and the
  implementation gates behind that proposal.
- The [v2 compiler and engine architecture](../docs/compiler-v2-architecture.md)
  specifies the lossless lexer, compiler passes, native modeling kernel,
  incremental execution, and thread-safety boundaries.

Agents must use `icad.language` capability results and must not emit proposed
syntax until the running compiler advertises the corresponding capability.

Production sources may now declare their contract before `PROJECT`:

```icad
REQUIRES ICAD 1.0
REQUIRES CAPABILITY BODY_HISTORY
```

`REQUIRES ICAD MAJOR.MINOR` rejects a newer unsupported language before normal
parsing. `REQUIRES CAPABILITY NAME` accepts only names returned by `icad
language` and MCP `icad.language`. Requirements are retained in the AST, must
precede every other declaration, and produce stable `ICAD-C0001` through
`ICAD-C0005` diagnostics for malformed, unsupported, duplicate, or late
headers. Compiler package version `0.0.1-alpha` and language contract version `1.0`
are intentionally separate.

`PARAMETER_EXPRESSIONS_V1` adds deterministic `+`, `-`, `*`, `/`, unary signs,
and parentheses to `PARAMETER`, `ANGLE`, and feature-property values.
`QUALIFIED_VALUE_REFERENCES_V1` accepts project-qualified scalar names such as
`bracket.width`. Addition/subtraction require equal dimensions. In this v1
slice multiplication requires one dimensionless operand; division accepts a
dimensionless divisor or equal dimensions. Derived area/volume dimensions are
reserved for the next expression capability.

`MULTI_SHAPE_SKETCH_V1` adds multiple named `SHAPE` blocks to one sketch.
Shapes declare `OPEN|CLOSED` and a `STOCK`, `ADDITIVE`, `HOLE`, or
`CONSTRUCTION` role. The implemented entity set is `POINT`, `LINE`, `ARC`, and
named `CIRCLE`; each closed shape becomes a stable `body::sketch.shape`
profile. `PAD` and `POCKET` must select `sketch.shape` explicitly. Hole regions
must be contained by exactly one stock or additive region, shape boundaries
may not intersect or touch, and every non-construction shape must feed a later
operation. `SOLVE FULL` rejects remaining degrees of freedom; qualified
cross-shape constraints use `shape.point` or `shape.entity`.

`SKETCH_REGION_ARRANGEMENT_V1` adds explicit `REGION` blocks. A region names
one closed stock/additive `OUTER` shape and optional contained `HOLES`; a single
`PAD` or `POCKET` may consume `sketch.region`. The native engine subtracts all
region holes in one boolean transaction while the dependency graph and
`visual.json` retain the outer, holes, and net area.

`ADVANCED_SKETCH_CONSTRAINTS_V1` adds `H_DISTANCE`, `V_DISTANCE`, `PARALLEL`,
`PERPENDICULAR`, `EQUAL_LENGTH`, `CONCENTRIC`, `EQUAL_RADIUS`, `MIDPOINT`, and
`SYMMETRIC point point ABOUT line`. Full-circle radii remain scalar inputs, so
`EQUAL_RADIUS` validates them rather than treating radius as a solver variable.

`SKETCH_LINE_ARC_TANGENCY_V1` adds the bounded endpoint form `TANGENT line arc
AT shared_point` (entity order may be reversed). The point must be an endpoint
shared by the line and a non-full-circle arc. The solver enforces a
perpendicular line direction and arc radius at that point. Full-circle,
arc-arc, spline, and projected-edge tangency remain proposal-only. Role-set
selectors, spline/ellipse entities, and automatic topology selectors also
remain proposal-only.

`PERSISTENT_FACE_REFERENCES_V1` is the first bounded persistent-topology slice.
Inside a body, `FACE alias FROM feature.face.top|bottom` names an earlier
feature's planar cap. A later sketch can use `ON FACE alias` or the direct
`ON FACE feature.face.top|bottom` form. AST, canonical IR, dependency output,
feature history, and `visual.json` retain the canonical
`body/feature/face.role` identity. Side-face and edge provenance selectors are
not advertised yet.

`SEMANTIC_EDGE_LOOP_SELECTION_V1` adds source-stable circular rim selection on
the current annular solid as `SELECT EDGE TOP|BOTTOM INNER|OUTER`. `FILLET` and
`CHAMFER` consume that selection, and `visual.json` reports the loop
classification plus `applicableOperations`. Arbitrary mesh-edge IDs, tangent
chains, face offsets, shelling, split, and project tools remain gated.

`TOPOLOGY_QUERY_V1` promotes that bounded selector into a named, typed query:

```icad
SELECTION upper_inner_rim
FROM wall_solid
EDGES WHERE
LOOP
CIRCULAR
CONCAVE
ADJACENT_TO FACE top
END
FEATURE soften_rim
TYPE FILLET
SELECT EDGESET upper_inner_rim
RADIUS 3 mm
END
```

The current resolver guarantees one circular annular loop from an immediately
preceding REGION extrusion. `CONCAVE|CONVEX` maps to inner/outer material side
and `top|bottom` maps to the axial cap. Inspection exposes the stable topology
ID, match reason, allowed operations, and explicit rejection reasons. General
query expressions and history remapping remain capability-gated.

The lexer reserves tokens needed by later slices: qualified-name dots,
selector brackets, JSON-style strings, comments, and decimal exponents. The
production parser still rejects unadvertised v2 constructs. Signed numeric
literals remain one token for source compatibility and are interpreted
correctly by the production expression parser.

Within a `BODY`, the primary grammar is an ordered CAD-style history. Start
with `SKETCH name ON PLANE XY|XZ|YZ`, create the first solid with `PAD name FROM
sketch[.shape|.region] DEPTH value NEW`, then use `SKETCH name ON FACE earlier-feature
face-selector|persistent-face-reference` before `PAD ... ADD` or `POCKET`. A face support must be declared
earlier in the same body. Every body-local sketch requires an explicit `ON`
support and every non-construction shape must feed a later operation. Open
construction shapes may remain unconsumed as reference geometry. Local sketch names are
scoped by body and become stable `body::sketch` identifiers in canonical IR.
These are language semantics implemented by the native geometry engine, not
exporter hints.

Low-level `FEATURE` blocks also execute in declaration order. The first feature
uses `OPERATION NEW` implicitly; following features may declare `UNION`, `CUT`,
or `INTERSECT`. They remain the advanced and compatibility surface for
operations without a concise history statement.

Modifier features follow an existing solid. `CHAMFER` and `FILLET` use `SELECT
EDGE NEAREST point`; `LINEAR_PATTERN` uses `DIRECTION`, integer `COUNT`, and a
typed `SPACING` property; `MIRROR` uses `PLANE point NORMAL vector`.

Profile surface features use explicit semantic references: `SWEEP` requires a
`PROFILE` and `PATH` containing at least two named `POINT3` values; `LOFT`
requires `PROFILE`, `TARGET_PROFILE`, and `HEIGHT`; `FREEFORM` adds `TWIST`
and a section `COUNT` from 3 through 128. Full `REVOLVE` accepts line, arc, and
circle profiles.

Legacy `SKETCH` blocks contain either one `CIRCLE center-x center-y radius` or named
`POINT name X Y [FIXED]` declarations followed by an ordered boundary of
`LINE name FROM point TO point` and `ARC name FROM point TO point CENTER point
CW|CCW` entities. An explicit entity chain must close end-to-start and makes
topology independent from point declaration order. Legacy point-only polygons
remain accepted for compatibility. Circle and point/entity forms cannot be
mixed. `HORIZONTAL`, `VERTICAL`, and `COINCIDENT` take two point
references; `DISTANCE` adds a length value; `ANGLE` takes point–vertex–point
references and an unsigned angle. Solved closed sketches are available as
profiles under the sketch name.

The optional project-wide `TOLERANCE LINEAR length ANGULAR angle` statement is
unit-checked, must be positive, and is limited to at most 1 mm and 1°.

`INSTANCE name OF body AT point ROTATION X Y Z` creates a named occurrence of
a body definition. `JOINT` parent and child references may use body or instance
occurrence names.
