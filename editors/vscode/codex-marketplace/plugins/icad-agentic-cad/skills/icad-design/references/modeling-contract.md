# ICAD modeling contract

Use the dependency chain below for every manufactured or articulated design:

```text
parameters/datums -> sketch workspace -> named path entities -> feature history
-> body -> component interfaces -> assembly joints -> scene -> visual feedback
```

## Current sketch grammar

The compiler supports legacy one-contour sketches and, when
`MULTI_SHAPE_SKETCH_V1` is advertised, multiple named paths in one workspace.
Points define coordinates; ordered entities define topology. Point declaration
order is not the path.

```icad
SKETCH plate ON PLANE XY
POINT a 0 mm 0 mm FIXED
POINT b 80 mm 0 mm FIXED
POINT c 80 mm 50 mm FIXED
POINT d 0 mm 50 mm FIXED
LINE bottom FROM a TO b
LINE right FROM b TO c
LINE top FROM c TO d
LINE left FROM d TO a
END
PAD plate_solid FROM plate DEPTH 8 mm NEW
```

An arc is `ARC name FROM start TO end CENTER center CW|CCW`. Its endpoint radii
must match and the full entity list must close end-to-start. A sketch may
instead contain one `CIRCLE center-x center-y radius`.

For multi-shape parts, declare `SHAPE name CLOSED ROLE STOCK|ADDITIVE|HOLE`
or an open/closed `CONSTRUCTION` path, then select the exact result as
`sketch.shape`. When `SKETCH_REGION_ARRANGEMENT_V1` is advertised, declare one
outer plus optional holes in a named `REGION` and consume `sketch.region`.
When `ADVANCED_SKETCH_CONSTRAINTS_V1` is advertised, use the qualified
dimensional, line, circular, midpoint, and symmetry families in the EBNF. When
`SKETCH_LINE_ARC_TANGENCY_V1` is advertised, use `TANGENT line arc AT
shared_endpoint`. Use `SOLVE FULL` for released geometry. Richer curves and
broader tangency remain proposal-only.

When `SEMANTIC_EDGE_LOOP_SELECTION_V1` is advertised, a FILLET or CHAMFER may
use `SELECT EDGE TOP|BOTTOM INNER|OUTER` on the current circular annular solid.
Verify `selection.classification` and `selection.applicableOperations` in
`visual.json`. Shell, offset, split, project, and arbitrary chains are not yet
production operations.

When `TOPOLOGY_QUERY_V1` is advertised, prefer a named query over the direct
selector: `SELECTION name`, `FROM feature`, `EDGES WHERE`, `LOOP`, `CIRCULAR`,
`CONCAVE|CONVEX`, `ADJACENT_TO FACE top|bottom`, then `SELECT EDGESET name` in
the immediately following FILLET/CHAMFER. Verify `matchedTopologyId`,
`matchReason`, `applicability.allowed`, and every rejected-operation reason.
The production subset is one circular loop on an annular REGION extrusion; it
does not promise general topology remapping.

When `PERSISTENT_FACE_REFERENCES_V1` is advertised, name an earlier planar cap
with `FACE alias FROM feature.face.top|bottom` and attach the next sketch using
`ON FACE alias`. Direct `ON FACE feature.face.top|bottom` is also supported.
Confirm `supportTopologyId` in `visual.json`; never substitute a mesh index or
proximity guess.

## Acceptance

Compilation and body counts are necessary but not sufficient. Inspect:

1. four-view silhouette and per-body bounds;
2. feature history and the source sketch for each solid result;
3. every parent-child joint anchor and axis;
4. contact/attachment gaps at rest;
5. first, middle, and last animation frames;
6. interference, joint-limit, manifold, manufacturing, STEP, and STL read-back.

If a reference image and output disagree visibly, the output fails even when
all numeric count tests pass. Repair datums, dimensions, and topology from the
source model; never hide the failure with camera changes or export settings.
