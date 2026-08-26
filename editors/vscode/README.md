# ICAD Agentic CAD for VS Code

This extension provides complete ICAD authoring support:

- dedicated point, line, curve, surface, object, event, parameter, material,
  constraint, unit, and lint highlighting through the bundled **ICAD Industrial
  Dark** theme, plus completion and definition navigation,
  formatting, diagnostics, and safe quick-fix suggestions;
- arithmetic parameter expressions, unit highlighting, and navigation from
  project-qualified scalar references such as `robot.width`;
- multi-shape sketch highlighting/completion and definition navigation for
  stable `sketch.shape`, `sketch.region`, `shape.point`, and `shape.entity`
  references, including the production advanced constraint family;
- named topology-query highlighting/completion and definition navigation for
  `SELECTION` declarations and `SELECT EDGESET` consumers;
- manufacturing-interface highlighting for `INTERFACE`, `CONNECT`, compatible
  interface types, standards, fits, fasteners, clearance, and magnetic seating;
- configurable format-on-save and compiler check-on-save;
- direct `.icad` launch into the native live editor and 3D workbench;
- prompt-to-design agentic creation and complete artifact builds;
- workspace MCP setup and a bundled ICAD Agentic CAD plugin installer for
  Codex CLI;
- automatic, SHA-256-verified installation of the matching `icad` and
  `icad-viewer` native release for macOS, Linux, or Windows x86-64.

Run **ICAD: Create Design from Prompt** from the Command Palette to turn a
short robot-arm, bridge, or generic-part request into editable `.icad` source
and the complete generated artifact package in one compiler invocation.

Choose **Preferences: Color Theme → ICAD Industrial Dark** for the bundled
design-language palette. Compiler errors, warnings, information, and hints use
separate lint colors; declaration scopes distinguish parameters, points,
lines, curves, surfaces, objects, materials, constraints, and scene events.

For local development, build ICAD in the repository's single `build/`
directory. A workspace build or explicit `icad.executablePath` and
`icad.viewerPath` takes precedence over the managed download.

Run **ICAD: Open Settings** to control the toolchain, LSP, format/check on
save, MCP, agentic helpers, and Codex plugin installation. MCP configuration
is written by merging an `icad` stdio server into `.vscode/mcp.json`; existing
servers are preserved and VS Code still asks the user to trust local servers.

Set **ICAD › Viewer: Initial View** to `isometric`, `front`, `back`, `left`,
`right`, `top`, or `bottom`. VS Code autocompletes the enum and the extension
passes that standard side to the native viewer every time it opens a design.
