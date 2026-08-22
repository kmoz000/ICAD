# ICAD Agentic CAD for VS Code

This extension provides complete ICAD authoring support:

- syntax and material highlighting, completion, definition navigation,
  formatting, diagnostics, and safe quick-fix suggestions;
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

For local development, build ICAD in the repository's single `build/`
directory. A workspace build or explicit `icad.executablePath` and
`icad.viewerPath` takes precedence over the managed download.

Run **ICAD: Open Settings** to control the toolchain, LSP, format/check on
save, MCP, agentic helpers, and Codex plugin installation. MCP configuration
is written by merging an `icad` stdio server into `.vscode/mcp.json`; existing
servers are preserved and VS Code still asks the user to trust local servers.
