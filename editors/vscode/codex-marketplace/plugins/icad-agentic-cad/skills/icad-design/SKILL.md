---
name: icad-design
description: Author, inspect, validate, and compile agentic 3D designs in the native ICAD language, including programmable geometry, materials, animation, constraints, assemblies, and manufacturing outputs.
---

# ICAD Design

Keep declarative `.icad` source as the design authority. Use the `icad` compiler
installed by the ICAD VS Code extension or available on `PATH`. Do not introduce
OpenCASCADE and do not disguise text or mesh data with another extension.

## Dependency order

Declare parameters and typed angles, named points and vectors, materials and
profiles, bodies and features, body poses, joints and constraints, scenes, then
exports. Prefer stable names and editable parameters that another agent can
inspect and revise.

## Validation workflow

Run these gates before claiming a design is complete:

```sh
icad check model.icad
icad diagnostics-json model.icad
icad validate model.icad
icad manufacturing model.icad
icad inspect-json model.icad
icad visual-json model.icad
icad topology-json model.icad
icad build model.icad --output-dir build/icad/model
```

Read back the generated STEP and STL with `icad inspect-step` and
`icad inspect-stl`. A complete build includes STEP assembly, STL, OBJ, glTF,
GLB, 3MF, HTML viewer, scene data, BOM, manufacturing report, SVG, DXF, and
topology JSON.

## Agentic modeling rules

- Use named `POINT3`, `VECTOR`, `ANGLE`, `POSE`, and `JOINT` declarations for
  mechanisms instead of inferring structure from mesh vertices.
- After every geometry or pose edit, call `icad.visualize` through MCP (or
  `icad visual-json`) and inspect all four depth rasters. Reject a poor
  silhouette, misplaced component, or unexpected occlusion before building.
- Use `REVOLUTE` and `PRISMATIC` joint limits and explicit constraints for
  movable assemblies.
- Use predefined material presets and embedded procedural textures.
- Use `SCENE`, `TRACK`, and ordered `KEYFRAME` declarations for animation.
- Use only stable semantic topology identifiers returned by ICAD inspection.
- Preserve the last valid source and repair focused compiler diagnostics before
  generating manufacturing artifacts.
