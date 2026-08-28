# Drawings

This module generates dependency-free SVG and R2013 ASCII DXF manufacturing
drawings. The SVG is a printable multi-sheet set: one detailed sheet for every
unique body definition, followed by the complete assembly sheet. Each part
sheet contains clean silhouette/crease projections, X/Y/Z envelope dimensions,
material and quantity, area and volume, feature parameters, sketch solve state,
profile roles and areas, circular-hole diameters, named interfaces, datums, and
general tolerances. The final assembly sheet contains orthographic views, BOM,
connection standards, fastener/fit data, authored clearances and gaps, and
inspection notes.

The DXF retains its compact top/front/right delivery-edge sheet for downstream
R2013 tooling. Standards-specific GD&T symbols and analytic hidden-line removal
remain future language depth.
