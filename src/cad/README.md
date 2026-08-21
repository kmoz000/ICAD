# Native geometry engine

This module is ICAD's only geometry engine. It creates primitives, applies
transforms, and owns both exact analytic topology and deterministic
tessellation. Exact solids expose stable vertex, edge, wire, face, shell, and
solid IDs; line/circle curves; and plane/cylinder/cone/sphere surfaces. The
validator enforces closed oriented shells and Euler consistency before a
compiled model can build. Exchange and viewer code consume this engine and may
not reconstruct source semantics independently.

Path and circle profiles retain exact circular edges and cylindrical extrusion
faces while the faceted model receives deterministic 32-segment-per-turn
tessellation. Surface area and volume are analytic for primitives, extrusions,
and line-profile revolutions; bounds, curved-revolution measurements, and
clearance broad phase remain tessellation-backed. Curved revolutions produce
validated faceted topology until the engine owns exact toroidal surfaces.

Feature transforms are followed by an optional semantic BODY `POSE`. The same
composed transform is applied to delivery meshes and analytic topology, so
agent inspection, bounds, exports, and stable topology IDs describe one
world-space arrangement.

Sequential feature CSG is implemented by the owned BSP classifier for union,
cut, and intersection. Reconstruction welds vertices, inserts shared collinear
boundary splits to eliminate T-junctions, removes degenerate and duplicate
triangles, and separates disconnected output components before topology and
exchange. Boolean output is a validated faceted B-Rep and deliberately does
not claim analytic surfaces after classification.

The modeling layer also resolves a named-point selector against the 12 sharp
edges of a translated axis-aligned box, then constructs chamfer or fillet
geometry in the chosen edge's right-handed principal-axis frame. Linear
patterns copy along normalized semantic vectors; mirror copies reflect through
a named point/normal plane with reversed winding. These modifier outputs use
the same faceted topology validation and connected-component separation as CSG.

Advanced profile construction remains dependency-free and native. `SWEEP`
translates a closed profile through a named polyline with fixed XY orientation.
`LOFT` resamples two closed profile perimeters to a common deterministic point
count, and `FREEFORM` interpolates those sections while applying cumulative
twist. Caps use the existing profile triangulator and section walls share
vertices, so the resulting faceted B-Reps pass the same closed-shell and Euler
validation. Curved full revolutions, including toroidal forms, use this honest
faceted delivery path until exact toroidal surfaces are implemented.

Native queries use the canonical project tolerance. Closest-distance evaluates
all vertex/triangle and edge/edge candidates and therefore returns the exact
Euclidean minimum on ICAD's validated polyhedral delivery boundary. Plane
sections normalize the requested normal, classify triangle edges using the
same tolerance, and retain body/part provenance for every segment.
