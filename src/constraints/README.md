# Constraint validation

Implemented constraints are semantic and agent-readable:

- `MIN_DISTANCE` compares two BODY world-space AABBs;
- `COINCIDENT` compares two named `POINT3` values against a length tolerance;
- `PARALLEL` and `PERPENDICULAR` compare normalized named vectors;
- `ANGLE_BETWEEN` compares two vectors with a target angle.
- `MATE ... FACE` compares selected world-semantic occurrence face coordinates
  against an offset;
- `MATE ... EDGE` compares two parallel world-semantic occurrence edges against
  a coincidence tolerance.

Any failed constraint or mate rejects artifact generation. Joints separately validate
body ownership, named anchors and axes, parent-tree acyclicity, physical
dimensions, drive limits, and animated values. Mates intentionally validate
explicit assembly poses instead of introducing hidden transforms.
