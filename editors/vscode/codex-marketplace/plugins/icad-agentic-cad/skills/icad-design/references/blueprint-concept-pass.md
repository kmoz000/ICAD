# Blueprint-aware concept pass

Perform this translation once before writing ICAD:

1. Read title-block intent: identity, scale, units, materials, general tolerances, notes, and revision.
2. Form the BOM mentally: components, quantities, material callouts, and assembly ownership.
3. Separate size from location dimensions. Preserve linear, angular, diameter, radius, and tolerance intent as named ICAD parameters or angles.
4. Choose the primary view with clearest identity and fewest hidden lines. Use only sufficient orthographic views, a section for hidden internals, and an auxiliary view for true inclined-surface shape.
5. Expand compressed tokens into a spatial envelope, hierarchy, materials, parent-child joint graph, constraints, manufacturing rules, and an inspection animation.
6. For each component, order the manufacturing construction: datum-plane sketch, first pad, face-attached sketches, additive pads, pockets, then advanced finishing features. Give every history step a functional name.

Then output only dependency-ordered ICAD grammar. Compile it and consume the direct `icad.visual.snapshot.v1` object from `icad.visualize`:

- `legend` and body bounds establish component identity, size, and placement.
- `front` establishes principal silhouette and working reach.
- `right` exposes depth and unintended overlap.
- `top` exposes footprint and lateral alignment.
- `isometric` confirms assembly identity and mechanical character.
- `joints` confirms anchors, axes, limits, and state.
- `featureHistory` maps each visible result back to its sketch plane, supporting feature and face, and additive or subtractive operation.

Revise named ICAD entities from this evidence. Do not reconceptualize and do not use `icad.agent.comparison.*` as visual feedback. Use `icad.compare` only after independently acceptable candidates exist.
