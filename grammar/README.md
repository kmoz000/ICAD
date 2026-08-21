# ICAD grammar

`icad.ebnf` documents the syntax implemented by ICAD's native C++ lexer and
parser. It is a reference for contributors and agent-tool authors; no parser
generator or grammar runtime is required by the compiler.

Within a `BODY`, features execute in declaration order. The first feature uses
`OPERATION NEW` implicitly; following features may declare `UNION`, `CUT`, or
`INTERSECT`. These keywords are language semantics implemented by the native
geometry engine, not exporter hints.

Modifier features follow an existing solid. `CHAMFER` and `FILLET` use `SELECT
EDGE NEAREST point`; `LINEAR_PATTERN` uses `DIRECTION`, integer `COUNT`, and a
typed `SPACING` property; `MIRROR` uses `PLANE point NORMAL vector`.

Profile surface features use explicit semantic references: `SWEEP` requires a
`PROFILE` and `PATH` containing at least two named `POINT3` values; `LOFT`
requires `PROFILE`, `TARGET_PROFILE`, and `HEIGHT`; `FREEFORM` adds `TWIST`
and a section `COUNT` from 3 through 128. Full `REVOLVE` accepts line, arc, and
circle profiles.

`SKETCH name` blocks contain named `POINT name X Y [FIXED]` declarations and
scoped constraints. `HORIZONTAL`, `VERTICAL`, and `COINCIDENT` take two point
references; `DISTANCE` adds a length value; `ANGLE` takes point–vertex–point
references and an unsigned angle. Solved closed sketches are available as
profiles under the sketch name.

The optional project-wide `TOLERANCE LINEAR length ANGULAR angle` statement is
unit-checked, must be positive, and is limited to at most 1 mm and 1°.

`INSTANCE name OF body AT point ROTATION X Y Z` creates a named occurrence of
a body definition. `JOINT` parent and child references may use body or instance
occurrence names.
