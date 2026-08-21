# Exchange backends

The STEP writer maps ICAD's owned exact topology to ISO 10303-21 AP214 analytic
B-Reps with line/circle curves and plane/cylinder/cone/sphere surfaces. It also
emits hierarchical body assembly records directly. The STL writer emits named
ASCII solids, the OBJ writer emits named mesh objects with material assignments,
and the glTF/GLB and 3MF writers emit portable mesh packages. Every backend
consumes the same validated native geometry model; no external converter or CAD
kernel is used.

Each exchange family has a structural read-back inspector. This validates the
container and entity contract produced by ICAD; downstream CAD interoperability
remains a separate release-certification gate.
