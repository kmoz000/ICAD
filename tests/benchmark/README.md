# ICAD benchmark suite

Benchmarks live beside the CTest definitions that execute them. Cases are
grouped by product behavior rather than compiler implementation and record
syntax, semantic, geometry, exact-topology, constraint, manufacturing,
read-back, and runtime outcomes.

The bridge baseline contains 35 solids, 250 exact vertices, 375 exact edges,
and 195 exact faces. The robotic-arm baseline contains 20 solids, 98 exact
vertices, 147 exact edges, and 89 exact faces in addition to reference STEP/STL
comparisons and its semantic mechanism graph. The boolean showcase locks
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
10 joints, 7 driven degrees of freedom, valid topology, and structural
read-back of the generated 20-solid STEP assembly. It first validates the
supplied reference folder's 10 STL components and 20-solid STEP baseline, then
requires the generated response to contain the spatial/mechanism design map.
