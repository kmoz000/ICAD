---
name: icad-design
description: Author, inspect, validate, and compile agentic 3D designs in the native ICAD language, including programmable geometry, materials, animation, constraints, assemblies, and manufacturing outputs.
---

# ICAD Design

Keep declarative `.icad` source as the design authority. Use the `icad` compiler
installed by the ICAD VS Code extension or available on `PATH`. Do not introduce
OpenCASCADE and do not disguise text or mesh data with another extension.

## Dependency order

Declare parameters and typed angles, datums, sketch workspaces, named path
entities, ordered operations, materials and appearances, bodies/components,
poses, joints and constraints, scenes, then exports. Prefer stable names and
editable parameters that another agent can inspect and revise.

## Raw prompt protocol

1. Call MCP `icad.agent.conceptualize` exactly once for a new request. Treat its compressed tokens and blueprint plan as the internal brief.
2. Expand the brief into hierarchy, envelope, dimensions, materials, kinematics, constraints, manufacturing intent, required views, and a scene.
3. Emit only grammar advertised by the running `icad language` tool—no Markdown,
   prose, pseudocode, future syntax, or foreign CAD syntax. Design unfamiliar
   mechanism topology directly instead of forcing a maintained template.
4. Compile, then call `icad.visualize`. Use only its direct `icad.visual.snapshot.v1` object as visual feedback. Read the legend/body bounds, front, right, top, isometric rasters, and joints.
5. Revise named ICAD entities from the visual evidence without a second concept pass. Use `icad.compare` only after two independently acceptable candidates exist.

Read [references/blueprint-concept-pass.md](references/blueprint-concept-pass.md) for image, drawing, and underspecified mechanical requests.
Read [references/modeling-contract.md](references/modeling-contract.md) before
authoring a manufactured part or articulated assembly.
Read [references/reference-index.md](references/reference-index.md) when exact
grammar syntax or page-level blueprint interpretation is needed. The packaged
EBNF defines source structure; the packaged PDF is supporting design-reading
context and must not override `icad.language` from the running compiler.

## Validation workflow

Run these gates before claiming a design is complete:

```sh
icad check model.icad
icad diagnostics-json model.icad
icad validate model.icad
icad manufacturing model.icad
icad inspect-json model.icad
icad visual-json model.icad
icad compare-json first.icad second.icad
icad topology-json model.icad
icad build model.icad --output-dir build/icad/model
```

Read back the generated STEP and STL with `icad inspect-step` and
`icad inspect-stl`. A complete build includes STEP assembly, STL, OBJ, glTF,
GLB, 3MF, HTML viewer, scene data, BOM, manufacturing report, SVG, DXF, and
topology JSON.

## Agentic modeling rules

- Query `icad.language` before authoring. A persistent source may begin with
  `REQUIRES ICAD 1.0` and one or more `REQUIRES CAPABILITY NAME` lines from the
  returned registry. Requirements must precede `PROJECT`; never use a proposed
  capability name that the running compiler does not advertise.
- Prefer CAD-style body history for new manufactured parts: `SKETCH name ON
  PLANE XY|XZ|YZ`, `PAD feature FROM sketch DEPTH value NEW`, then `SKETCH name
  ON FACE earlier_feature X_MIN|X_MAX|Y_MIN|Y_MAX|Z_MIN|Z_MAX` followed by
  `PAD ... ADD` or `POCKET ...`. Declare supports before use and inspect
  `visual.json.featureHistory` after compilation.
- Give every body-local sketch an explicit `ON` support and a later consuming
  operation. Correlate agent feedback by canonical `body::sketch` `sketchId`
  and preserved `PAD` or `POCKET` command; local sketch names may repeat across
  different bodies.
- Treat a sketch as a constrained 2D workspace and its named `LINE`/`ARC`
  entities as an ordered SVG-path-like contour. Declare every point first,
  give every entity a stable name, close the chain end-to-start, and inspect
  the emitted `entities` array in `visual.json`. The current compiler accepts
  one explicit contour or one circle per sketch; do not invent the planned
  multi-`SHAPE` syntax until `icad language` advertises it.
- Use low-level `FEATURE` blocks only for operations without a history
  shorthand or while maintaining an existing model.
- Use named `POINT3`, `VECTOR`, `ANGLE`, `POSE`, and `JOINT` declarations for
  mechanisms instead of inferring structure from mesh vertices.
- After every geometry or pose edit, call `icad.visualize` through MCP (or
  `icad visual-json`) and inspect all four depth rasters. Reject a poor
  silhouette, misplaced component, or unexpected occlusion before building.
- For mechanisms, reject count-only success. Verify every joint anchor lies on
  both the parent and child component envelope, verify the base is grounded,
  and sample the first/middle/last scene frames. A scene that moves the entire
  structure or separates connected interfaces is a failed design.
- Never substitute `icad.agent.comparison.*` for the direct `visual.json`
  feedback schema `icad.visual.snapshot.v1`.
- When exploring alternatives, generate only two candidates at a time and make
  their body graphs or mechanism architectures meaningfully different. Call
  MCP `icad.compare` (or `icad compare-json`) to compare membership, materials,
  complete mechanism edges, spatial envelopes, scenes, topology cost, and both
  four-view snapshots. Read the shared-bounds difference grids (`A` first-only,
  `B` second-only, `=` same body, `!` changed body identity) and the intent-aware
  optimization matrix; preserve the selected candidate before generating the
  next pair.
- Use `REVOLUTE` and `PRISMATIC` joint limits and explicit constraints for
  movable assemblies.
- Use predefined material presets and embedded procedural textures.
- Use `SCENE`, `TRACK`, and ordered `KEYFRAME` declarations for animation.
- Use only stable semantic topology identifiers returned by ICAD inspection.
- Preserve the last valid source and repair focused compiler diagnostics before
  generating manufacturing artifacts.
