# ICAD Agentic CAD for VS Code

This extension provides ICAD syntax highlighting, native language-server
diagnostics, build/check commands, and desktop viewer launch support.

Run **ICAD: Create Design from Prompt** from the Command Palette to turn a
short robot-arm, bridge, or generic-part request into editable `.icad` source
and the complete generated artifact package in one compiler invocation.

Build ICAD first in the repository's single `build/` directory. The extension
automatically uses `build/bin/icad` and `build/bin/icad-viewer` when present;
installed binaries on `PATH` or explicit `icad.executablePath` and
`icad.viewerPath` settings are also supported.
