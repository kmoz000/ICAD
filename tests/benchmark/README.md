# ICAD benchmark suite

Benchmarks live beside the CTest definitions that execute them. Cases are
grouped by product behavior rather than compiler implementation and record
syntax, semantic, geometry, exact-topology, constraint, manufacturing,
read-back, and runtime outcomes.

The bridge baseline contains 35 solids, 250 exact vertices, 375 exact edges,
and 195 exact faces. The robotic-arm baseline contains 27 solids, 272 exact
vertices, 408 exact edges, and 190 exact faces in addition to reference STEP/STL
comparisons, four depth-raster views, actual mesh-volume joint attachment, three
connected scene samples, a stationary root, and its semantic mechanism graph. The boolean showcase locks
ordered union/cut/intersection behavior at 2,840 mm3, validates all repaired
shells, and structurally reads back its STEP and STL outputs.
The modeling-tools case locks semantic edge selection, fillet, chamfer, a
four-instance linear pattern, and a named-plane mirror at eight output solids.
The advanced-surfaces case locks translational polyline sweep, two-profile
loft, deterministic twisted profile morphing, and curved full revolution at
four validated solids and 2,148 facets, with structural STEP/STL read-back.
The constrained-sketch case solves nine geometric and dimensional equations to
zero remaining degrees of freedom, converts the solved loop into an extrusion
profile, and reads the resulting solid back from STEP and STL.
The sketch-history case executes a datum-plane pad, a face-attached additive
pad, and a face-attached circular pocket. It locks the complete 100 x 60 x 32 mm
envelope, agent-readable feature history, manifold solid, and STEP/STL
read-back so coplanar face operations cannot silently discard the base solid.
The multi-shape plate case classifies one stock region and four contained hole
regions from one sketch, consumes each through explicit `sketch.shape` history,
batches the four compatible pocket cutters into one boolean transaction, and
locks the 100 x 60 x 8 mm envelope, 45,502.8 mm3 volume, visual shape evidence,
and STEP/STL read-back.
The explicit-region plate case groups one stock outline and two hole shapes
into one `REGION`, consumes it with one PAD, checks net analytic volume,
requires the region dependency/visual contract, and structurally reads back
the resulting STEP and STL solid.
The persistent-face plate case names an earlier planar cap, reuses that alias
for an additive feature, consumes a direct cap path for a pocket, and validates
canonical topology provenance plus STEP/STL read-back.
The rounded-tangent plate case locks four explicit shared-endpoint line/arc
constraints, zero remaining sketch degrees of freedom, an 80 x 20 x 8 mm
capsule envelope, agent-visible tangency references, and STEP/STL read-back.
The selective-round vessel case creates two annular solids from explicit sketch
regions, selects top concave and convex circular rims independently, applies
native fillets, exposes FILLET/CHAMFER applicability, and reads both solids
back from STEP and STL.
The topology-query vessel promotes that same native operation into a named,
typed `SELECTION`, verifies the stable matched topology ID, query reason,
operation applicability and rejection reasons, dependency provenance, native
topology, and one-solid STEP/STL read-back.
The incremental case compiles a two-body dependency graph, proves full reuse on
an unchanged source, and then changes one parameter while requiring exactly one
dirty body recomputation and one cached topology reuse.
The geometric-query case locks a source-level 0.001 mm tolerance, a 10 mm exact
polyhedral closest-distance result, and a named-body plane section through the
CLI JSON contract.
The assembly-instance case reuses one body definition twice, solves a 90°
revolute occurrence through its named joint chain, and structurally reads back
all three STEP solids.

The agentic prompt case invokes `agent-create` once with a short articulated
robot-arm request. It requires the embedded low-iteration contract, 10 bodies,
10 joints, 9 driven degrees of freedom, valid topology, and structural
read-back of the generated 25-solid STEP assembly. It first validates the
supplied reference folder's 10 STL components and 20-solid STEP baseline, then
requires the generated response to contain the spatial/mechanism design map and
a visual snapshot with detailed gear and gripper silhouettes.
