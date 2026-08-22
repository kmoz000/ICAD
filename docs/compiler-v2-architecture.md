# ICAD v2 conceptual lexer and native engine architecture

Status: **proposed architecture**. It explains how the
[language v2 RFC](grammar-v2-rfc.md) and
[proposed EBNF](../grammar/icad-v2-proposal.ebnf) should be implemented. The
current compiler continues to follow [grammar/icad.ebnf](../grammar/icad.ebnf)
until individual capability gates land.

ICAD v2 should be one native, repository-owned modeling engine with several
thin clients. The CLI, MCP server, LSP, desktop viewer, web viewer, exporters,
and tests all call the same public engine API. None may reinterpret source or
construct private geometry.

## 1. Architectural boundary

```text
bytes and imported modules
  -> SourceManager
  -> lossless incremental lexer
  -> recovery parser and concrete syntax tree
  -> scoped typed AST
  -> name, expression, unit, and capability resolution
  -> sketch topology and constraint systems
  -> ordered feature/product dependency IR
  -> native modeling engine
       -> analytic geometry and robust predicates
       -> boundary representation and topology provenance
       -> sketch/feature execution
       -> part and assembly evaluation
       -> validation and measurement
       -> deterministic tessellation
  -> immutable ModelRevision
       -> visual.json / agent views
       -> viewer render packets
       -> STEP / STL / OBJ / DWG / drawing outputs
```

The dependency rule is strict:

```text
source <- lexer <- parser <- semantic <- canonical IR <- engine <- consumers
```

An arrow means “is consumed by.” A lower layer never imports a higher layer.
The lexer knows nothing about sketches. The parser knows nothing about B-Reps.
The engine knows nothing about Markdown, MCP transport, editor state, or HTML.

## 2. Source manager and module composition

`SourceManager` owns immutable source snapshots. Each snapshot has:

- a stable `SourceId` within the compile revision;
- canonical project-relative path;
- content hash and byte length;
- validated UTF-8 bytes;
- an indexed line-start table;
- import parent and import-site span;
- revision number supplied by the caller.

Imports resolve before semantic compilation but are not pasted into one giant
string. The parser sees a module graph so diagnostics retain original file and
line locations. Resolution must:

- accept only project-relative `.icad` paths;
- canonicalize and confine every path beneath the declared project root;
- reject symlink escapes, cycles, excessive depth, and aggregate size;
- cache snapshots by canonical path plus content hash;
- never execute a source file or pass it to a shell;
- produce a deterministic topological module order.

`IMPORT "link.icad" AS link` creates a namespace. `INJECT` is explicit textual
composition with source identity preserved. Duplicate declarations created by
injection are normal scope errors, not last-writer-wins behavior.

## 3. Conceptual lexer

### 3.1 Responsibilities

The lexer converts validated source bytes into a lossless token stream. It is
responsible for byte correctness, token boundaries, trivia, and source spans.
It is not responsible for:

- recognizing whether `PAD` is legal inside a `BODY`;
- resolving whether `base.top` names a face;
- attaching a unit to an expression;
- determining whether a path is closed;
- solving a constraint or creating geometry.

This narrow role keeps lexing fast, independently fuzzable, and reusable by the
formatter, parser, syntax highlighter, and incremental editor pipeline.

### 3.2 Token representation

The present lexer has only identifier, number, newline, EOF, and invalid token
kinds. V2 needs punctuation and strings while retaining a compact value type:

```cpp
enum class TokenKind : std::uint8_t {
    word,
    number,
    string,
    newline,
    dot,
    comma,
    colon,
    plus,
    minus,
    star,
    slash,
    left_paren,
    right_paren,
    left_bracket,
    right_bracket,
    comment,
    end_of_file,
    invalid,
};

struct SourceSpan {
    SourceId source;
    std::uint32_t byte_begin;
    std::uint32_t byte_end;
};

struct Token {
    TokenKind kind;
    SourceSpan span;
    SymbolId symbol;       // set only for interned words
    NumericId numeric;     // set only for validated number spelling
    TokenFlags flags;
};
```

Tokens reference the immutable source snapshot instead of owning a string for
every lexeme. This prevents repeated allocations in large files. `SymbolId`
comes from a compile-session string interner; it is never a process-global
mutable table.

Keywords are soft. A token such as `SKETCH` is a `word` plus an optional
`KeywordId` lookup, not a different enum member for every keyword. This keeps
the lexer stable as capability-gated grammar expands and permits a keyword as
a name where the grammar explicitly allows it.

### 3.3 Lexical rules

- Source is UTF-8. Invalid sequences produce one diagnostic and one bounded
  invalid token; the lexer always advances.
- Identifiers use Unicode XID start/continue rules, plus underscore. They are
  normalized to NFC for comparison while source spelling remains unchanged.
- ASCII keywords are compared case-sensitively. The canonical formatter emits
  uppercase keywords and preserves identifier spelling.
- `.` is a separate token, making `body.feature.face` a qualified reference.
- `+` and `-` are operators, not part of a number. Thus `a-2` and `a - 2` have
  identical tokenization.
- Decimal numbers support `12`, `12.5`, `.5`, and an optional exponent such as
  `1.2e-4`. The semantic layer decides whether the resulting value is allowed.
- Strings use JSON-compatible escapes. Invalid escapes remain one recoverable
  string token plus a precise diagnostic.
- `#` begins a line comment outside a string. Comments and newlines are kept in
  the lossless stream; horizontal whitespace is trivia attached to neighboring
  tokens.
- Newline is syntactic. `CRLF` and `CR` normalize to one newline token while
  spans retain original bytes.
- Indentation is formatting only. Blocks close with `END`, avoiding invisible
  tab/space semantics in machine-generated source.

Compatibility transition: the first lexer slice keeps an immediately signed
literal such as `-12.5` in one number token because the production parser has no
unary-expression node yet. The expression-parser slice must normalize signed
literals and standalone signs to the same unary/binary AST semantics before the
final operator-only tokenization rule is enabled. Until then this behavior is
not advertised as the `EXPRESSIONS` capability.

The lexer uses locale-independent conversion. It does not call APIs whose
decimal parsing changes with the host locale. It rejects NaN and infinity
spellings at the lexical or numeric-literal boundary.

### 3.4 Lossless and semantic streams

One lex pass creates a lossless stream containing comments and whitespace
attachments. The parser reads a filtered cursor that exposes significant
tokens and newlines. The formatter reads the lossless stream and the concrete
syntax tree. This avoids the common failure where formatting destroys comments
because the semantic AST never stored them.

The lexer returns tokens and diagnostics together. A bad byte does not abort
the file. Each recovery rule must consume at least one byte, which guarantees
linear progress on hostile input.

### 3.5 Incremental lexing

For an editor change, the source manager identifies the smallest changed byte
range and expands it to safe line boundaries. Lexing resumes from the preceding
checkpoint. A checkpoint contains only lexical state needed across lines, such
as whether a future multiline string or comment capability is active.

The new token slice is compared with the old stream by kind and source text.
Matching suffixes are reused. Token IDs outside the changed window remain
stable, allowing parser nodes and diagnostics to survive small edits.

Worst-case fallback is a full linear lex. Incremental mode must produce exactly
the same token stream and diagnostics as a clean lex of the complete snapshot.

### 3.6 Lexer resource safety

The lexer enforces configurable limits for source bytes, token count, identifier
length, string length, numeric digits, and diagnostics. Counters use checked
arithmetic. Offsets are validated before narrowing to 32 bits; larger files are
rejected with a resource diagnostic rather than wrapped.

Fuzz assertions are:

- no crash, exception escape, hang, or unbounded allocation;
- cursor advances or terminates on every loop;
- all spans are ordered and inside the source snapshot;
- concatenated token/trivia spans cover every source byte exactly once;
- clean and incremental lexing are equivalent.

## 4. Recovery parser and syntax trees

The parser consumes tokens into a lossless concrete syntax tree (CST). A second
lowering step creates a typed AST. Keeping both solves two different problems:

- CST: comments, incomplete editor input, formatter, syntax-aware selection,
  and exact repair ranges;
- AST: declarations, expressions, scopes, semantic validation, and lowering.

Parser blocks use a context stack. At a bad line, recovery skips to newline,
`END`, or a keyword valid in the current block. Missing `END` creates a
synthetic zero-width token so later declarations remain visible to the LSP.

Every syntax node has a stable structural fingerprint derived from its kind,
significant child tokens, and child fingerprints. Unchanged subtrees are reused
after incremental lexing. Parser caches belong to one document session and are
published as immutable snapshots.

The parser checks grammar shape only. For example, it accepts a `PAD` node with
a region reference and extent; later passes decide whether that reference is a
closed solved region declared earlier in the same body.

## 5. Semantic compiler passes

Semantic compilation is a fixed sequence. Each pass consumes an immutable
result and produces another immutable result plus diagnostics:

1. **Capability pass:** validate `REQUIRES` declarations before dependent
   constructs are accepted.
2. **Module pass:** construct namespaces and deterministic import order.
3. **Declaration pass:** build project, part, body, sketch, feature, assembly,
   drawing, and scene scopes.
4. **Reference pass:** resolve names and qualified paths to stable declaration
   IDs; report ambiguity with alternatives.
5. **Expression pass:** build dependency graphs, detect cycles, infer dimensions,
   evaluate constants, and enforce ranges.
6. **Sketch topology pass:** validate entity connectivity and classify candidate
   paths and regions without solving geometry yet.
7. **Constraint pass:** construct solver equations, solve, classify DOF and
   conflicts, and publish solved analytic curves.
8. **Feature pass:** validate chronological dependencies, operation cardinality,
   extents, and selection requirements.
9. **Product pass:** validate bodies, parts, interfaces, occurrences, mates,
   joints, drawings, scenes, and validation targets.
10. **IR pass:** lower only valid declarations into canonical engine commands.

No pass mutates earlier objects. Diagnostics carry source spans, related spans,
stable declaration IDs, structured parameters, and fix identifiers.

## 6. Canonical engine IR

The frontend emits instructions, not meshes. Core IDs are small generational
handles scoped to one revision:

```cpp
struct SketchCommand {
    SketchId id;
    SupportRef support;
    std::span<const ShapeCommand> shapes;
    std::span<const ConstraintCommand> constraints;
    SolveRequirement requirement;
};

struct FeatureCommand {
    FeatureId id;
    BodyId body;
    FeatureKind kind;
    FeatureInputs inputs;
    FeatureParameters parameters;
    ResultOperation operation;
};

struct ProductGraph {
    std::span<const PartDefinition> parts;
    std::span<const Occurrence> occurrences;
    std::span<const MateCommand> mates;
    std::span<const JointCommand> joints;
};
```

IR arrays are stored in source-stable order. References use typed IDs instead
of strings. Unit-normalized values use `double` with an explicit dimension tag
and original expression provenance. The IR hash includes relevant tolerance,
engine version, and capability version.

## 7. Native geometry engine

The engine is a direct ICAD implementation, not an OpenCASCADE adapter. It is
split into layers so higher-level features do not duplicate geometry code.

### 7.1 Mathematical foundation

The base layer owns:

- `Vec2`, `Vec3`, points, directions, matrices, rigid transforms, and frames;
- length/angle tolerances and scale-aware comparisons;
- intervals and axis-aligned/oriented bounds;
- robust orientation, incidence, intersection, and classification predicates;
- analytic line, circle, ellipse, conic, and spline curves;
- plane, cylinder, cone, sphere, torus, and spline surfaces;
- curve parameter domains and surface UV domains.

Public construction validates finite values, normalization, domain ordering,
and tolerance. Internal vectors are ordinary values; owned resources use RAII.
No raw owning pointer crosses a module boundary.

Floating-point construction and topological decisions are separated. Fast
double-precision evaluation handles normal work; near a decision boundary a
filtered robust predicate uses expansion or higher-precision arithmetic. The
engine never treats a render tessellation as proof of exact intersection.

### 7.2 Boundary representation

The exact model uses an oriented half-edge boundary representation:

```text
Solid -> Shell -> Face -> Loop -> Coedge -> Edge -> Vertex
                  |                |
                Surface          Curve
```

An edge has one geometric curve and one or more oriented coedge uses. A face has
one analytic surface, an outer loop, optional inner loops, and orientation.
Handles are generational, so a deleted entity cannot be accessed through a
stale index. Builders work in a private transaction and publish only after
validation.

The topology validator checks:

- referenced handles exist and have the expected generation;
- edge endpoints lie on their curves;
- coedges form closed, consistently oriented loops;
- loop curves lie on their owning surface within tolerance;
- manifold edges have the required opposite incidences;
- shells are closed and consistently oriented;
- solids have positive classified volume;
- Euler and adjacency invariants match the supported body class.

Non-manifold construction is a typed failure unless a surface-model capability
explicitly requests it.

### 7.3 Sketch and region engine

Sketch entities retain analytic definitions. The solver has two cooperating
parts:

1. a graph reducer removes fixed values, propagates obvious equality and
   incidence relations, identifies independent connected systems, and computes
   a symbolic sparsity structure;
2. a numerical solver handles remaining nonlinear equations with deterministic
   ordering, damping, scaling, rank estimation, and bounded iterations.

Each constraint produces normalized residual terms. A failed solve reports the
largest residuals and a minimal conflicting subset approximation. Rank analysis
produces meaningful DOF descriptions such as “point `tip` can translate along
local X,” not just the integer `1`.

After solving, a planar arrangement builder splits intersections, joins
endpoints under tolerance, rejects degeneracy, and classifies bounded regions.
Roles (`STOCK`, `HOLE`, and so on) and containment together determine material
regions. Analytic curves remain analytic in region wires.

### 7.4 Feature execution

Each feature operator has this contract:

```text
validated input bodies + regions + selections + parameters + tolerance
  -> private topology transaction
  -> analytic construction/intersection/classification
  -> topology validation
  -> provenance reconciliation
  -> immutable FeatureResult or structured FeatureFailure
```

Operators are added in dependency order:

1. planar pad and pocket with fixed/symmetric/through extents;
2. typed holes and revolves;
3. fillet, chamfer, draft, and shell on stable selections;
4. sweep, loft, rib, thicken, and split;
5. linear, circular, mirror, and table patterns;
6. direct face operations and advanced surface workflows.

Boolean operations share one intersection graph, face splitter, classifier,
and stitcher. Feature code does not contain separate ad hoc triangle booleans.
If an exact operation is not implemented, the compiler reports the missing
capability; it does not emit a visually plausible but invalid mesh.

### 7.5 Persistent topology

Each output entity receives a semantic provenance key based on:

- producing feature;
- source region and source curve when applicable;
- operation role such as cap, side, inner wall, or blend;
- parent topology lineage;
- deterministic split/merge ordinal based on geometry, never container address.

Regeneration matches old and new topology using provenance first, then analytic
geometry and adjacency as a constrained fallback. The result records preserved,
split, merged, created, and deleted IDs. A reference is invalid if matching is
ambiguous; selecting a nearby item silently is forbidden.

### 7.6 Parts, assemblies, and scenes

Part compilation freezes validated body results and interfaces in the part's
local frame. Occurrences reference a part plus a rigid transform and optional
parameter variant. Geometry is shared between identical occurrences; only
transforms and per-occurrence state differ.

The assembly solver creates equations from mates and explicit mobility from
joints. It first solves the rigid parent graph, then evaluates joint state.
Closed kinematic loops require a separate advertised solver. Interference uses
a deterministic bounds hierarchy for broad phase and exact/surface-aware tests
for candidates.

Scene evaluation copies only occurrence transforms and visual state. A joint
track applies to its declared child subtree. It cannot modify part geometry,
feature history, or an unrelated root occurrence.

### 7.7 Tessellation and render packets

Tessellation is a consumer of validated B-Rep faces. It uses chord, normal, and
edge tolerances and shares boundary samples so adjacent faces do not crack.
Render packets contain indexed positions, normals, UVs, material IDs, component
IDs, topology pick IDs, bounds, and revision hashes.

The viewer receives immutable changed packets plus removed IDs. It does not
compile source or recreate CAD operations in JavaScript. Orthographic view
orientation, selection, scene playback, and export commands call the same
engine revision.

## 8. Public engine API

All clients use a small C++ API with opaque implementation ownership:

```cpp
struct CompileRequest {
    ProjectRoot root;
    SourceSnapshot entry;
    CompileProfile profile;
    CancellationToken cancellation;
};

struct CompileResult {
    RevisionId revision;
    CompileStatus status;
    std::shared_ptr<const ModelRevision> model;
    std::vector<Diagnostic> diagnostics;
    IncrementalStats incremental;
};

class EngineSession {
public:
    [[nodiscard]] CompileResult compile(const CompileRequest& request);
    [[nodiscard]] QueryResult query(RevisionId, const Query& query) const;
    [[nodiscard]] ExportResult export_model(RevisionId,
                                            const ExportRequest& request) const;
};
```

`ModelRevision` is immutable and can be read concurrently. A compile failure
may return the last valid revision marked stale, but never publishes a partially
built new revision. Export identifies the exact revision it consumed.

Queries include stable selection, dimensions, mass properties, section,
clearance, interference, topology provenance, assembly state, render packets,
and `visual.json`. Query methods do not mutate the revision.

## 9. Incremental and parallel compilation

Each node in the dependency graph has a content fingerprint derived from its
semantic inputs, dependency fingerprints, tolerance, and engine capability
version. The cache stores immutable successful results only.

An edit invalidates the narrowest graph cut:

```text
changed token range
  -> changed CST/AST declarations
  -> changed typed semantic nodes
  -> dirty sketch or product nodes
  -> downstream feature/topology nodes
  -> dependent interface/assembly/drawing/view nodes
```

Independent modules, parts, sketches, and bodies may run on a bounded work-
stealing pool. Features inside one body remain sequential unless dependency
analysis proves independent branches. Results are merged in stable ID order,
so thread scheduling cannot change output hashes.

Thread-safety rules are:

- one `CompileContext` per requested revision;
- immutable inputs and immutable published results;
- no process-global mutable tolerance, locale, parser, or geometry state;
- per-task scratch arenas with strict lifetime boundaries;
- shared caches guarded at publication, not during expensive computation;
- duplicate concurrent cache misses may compute independently, but only an
  identical verified result is published;
- cooperative cancellation at module, solver-iteration, feature, tessellation,
  validation, and export boundaries;
- lock ordering is documented and tested under ThreadSanitizer where available.

Memory budgets are explicit per request. Large temporary arrays use checked
sizes and monotonic scratch arenas released at task completion. Long-lived
caches use byte-weighted LRU eviction and never retain source snapshots solely
through accidental shared-pointer cycles.

## 10. Determinism and transactions

Given identical source bytes, project-relative paths, compiler version,
capabilities, tolerance, and export profile, ICAD must produce identical
semantic hashes and stable entity ordering. Hashes exclude wall-clock time,
absolute workspace paths, thread IDs, addresses, and unordered-container order.

Compilation is transactional:

1. capture immutable sources;
2. build a candidate revision in private state;
3. validate all required gates;
4. atomically publish the revision pointer;
5. notify viewers and tools with the new revision ID.

Artifact export writes into a staging directory, validates read-back where a
reader exists, then atomically publishes the output folder. Live preview never
writes into or depends on the export transaction.

## 11. Agent feedback loop

The compiler should give an agent evidence at three costs:

1. **Fast semantic feedback:** diagnostics, dependency changes, solver state,
   body/part counts, bounds, mass, connections, and validation summaries.
2. **Structured spatial feedback:** `visual.json` topology provenance,
   occurrence transforms, interfaces, clearances, silhouettes, and per-view
   projected bounds.
3. **Rendered evidence:** deterministic front/right/top/isometric images and
   optional requested cameras with component/topology ID buffers.

An agent first reads levels 1 and 2, then asks the viewer binary for images only
when spatial judgment is needed. A render request names revision, camera frame,
projection, resolution, display mode, and visible set. This makes visual
iteration reproducible rather than dependent on an interactive camera left in
an unknown position.

## 12. Tests and acceptance gates

### Lexer and parser

- golden tokens and spans for every lexical form;
- UTF-8, escape, numeric exponent, punctuation, CRLF, and comment cases;
- malformed input recovery with multiple later valid declarations;
- clean versus incremental token/CST equivalence;
- property-based and coverage-guided fuzzing;
- resource-limit tests with long words, digits, strings, lines, and files;
- formatter parse-format-parse semantic equivalence.

### Semantic and sketch layers

- unit inference and incompatible-dimension rejection;
- parameter and import cycle detection;
- every sketch entity and constraint family;
- fully/under/over/inconsistently constrained classifications;
- analytic residual and DOF fixtures;
- region nesting, role, winding, degeneracy, and tolerance tests;
- stable diagnostics and safe auto-fix tests.

### Geometry engine

- analytic primitive invariants and robust predicate adversarial cases;
- topology transaction rollback and stale-handle rejection;
- feature history golden provenance maps;
- boolean classification, manifold, volume, and orientation checks;
- persistent naming under parameter changes and feature insertion;
- STEP/STL/OBJ/DWG export inspection and supported read-back;
- sanitizer, fuzz, cancellation, memory-budget, and race tests.

### Assembly, viewer, and agent system

- occurrence sharing and transform correctness;
- mate DOF accounting and joint parent/child motion;
- articulated scenes that leave fixed ancestors unchanged;
- interference/clearance fixtures at sampled joint positions;
- component/topology picking against the ID buffer;
- `visual.json` schema, stable ordering, and incremental deltas;
- deterministic renders from named camera coordinates;
- large-project edit latency and cache-reuse benchmarks.

Performance targets must be stored as versioned benchmark budgets with hardware
metadata. Initial goals for the reference development machine are:

- linear full lexing with no per-token string allocation;
- a one-line warm edit re-lexes/reparses only the affected window;
- unchanged compile publishes cache metadata without rebuilding topology;
- a local sketch edit rebuilds only its downstream body history;
- interactive semantic/preview feedback targets a 50 ms budget for the robotic
  arm benchmark and reports when it misses rather than hiding latency;
- viewer joint playback reuses part meshes and updates transforms only.

Correctness gates take priority over a time target. A fast compile that emits a
non-manifold part, detached assembly, stale topology reference, or fabricated
engineering result is a failed compile.

## 13. Delivery sequence

Implementation should proceed as vertical slices, each usable by CLI, LSP, MCP,
viewer, and tests before the next slice begins:

1. source snapshots, v2 tokens, spans, incremental lexer, and fuzz gates;
2. lossless CST, recovery parser, expressions, qualified names, and formatter;
3. scoped AST/IR and capability negotiation;
4. multi-shape sketch arrangement plus solver reporting;
5. planar pad/pocket history with exact topology provenance;
6. persistent selectors plus native fillet/chamfer/hole primitives;
7. advanced feature operators and process validation;
8. parts, occurrences, interfaces, mates, and joints;
9. `visual.json` v2, deterministic viewer renders, drawings, and exports;
10. industrial examples, large-project benchmarks, plugin grammar snapshot,
    release packages, and compatibility migration.

Each slice moves syntax from the proposal EBNF into the production EBNF only
after the complete vertical gate passes. This makes the language, compiler,
engine, viewer, and agent tools advance as one coherent system.
