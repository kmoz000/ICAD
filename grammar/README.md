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
headers. Compiler package version `0.21.0` and language contract version `1.0`
are intentionally separate.

`PARAMETER_EXPRESSIONS_V1` adds deterministic `+`, `-`, `*`, `/`, unary signs,
and parentheses to `PARAMETER`, `ANGLE`, and feature-property values.
`QUALIFIED_VALUE_REFERENCES_V1` accepts project-qualified scalar names such as
`bracket.width`. Addition/subtraction require equal dimensions. In this v1
slice multiplication requires one dimensionless operand; division accepts a
dimensionless divisor or equal dimensions. Derived area/volume dimensions are
reserved for the next expression capability.

The lexer reserves tokens needed by later slices: qualified-name dots,
selector brackets, JSON-style strings, comments, and decimal exponents. The
production parser still rejects unadvertised v2 constructs. Signed numeric
literals remain one token for source compatibility and are interpreted
correctly by the production expression parser.

Within a `BODY`, the primary grammar is an ordered CAD-style history. Start
with `SKETCH name ON PLANE XY|XZ|YZ`, create the first solid with `PAD name FROM
sketch DEPTH value NEW`, then use `SKETCH name ON FACE earlier-feature
face-selector` before `PAD ... ADD` or `POCKET`. A face support must be declared
earlier in the same body. Every body-local sketch requires an explicit `ON`
support and must feed a later operation; unfinished construction sketches are
rejected until construction-only semantics exist. Local sketch names are
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

`SKETCH` blocks contain either one `CIRCLE center-x center-y radius` or named
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
