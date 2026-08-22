# ICAD modeling contract

Use the dependency chain below for every manufactured or articulated design:

```text
parameters/datums -> sketch workspace -> named path entities -> feature history
-> body -> component interfaces -> assembly joints -> scene -> visual feedback
```

## Current sketch grammar

The compiler currently supports one contour in each sketch. Points define
coordinates; ordered entities define topology. Point declaration order is not
the path.

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

The long-term grammar treats `SKETCH` as a workspace containing multiple named
`SHAPE` paths selected as `sketch.shape`. That syntax is a design contract in
`docs/grammar-v2-design.md`, not current source syntax. Never emit it until the
running `icad language` response declares support.

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
