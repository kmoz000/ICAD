# Releasing ICAD

Release metadata must agree across CMake, the CLI/MCP servers, and the VS Code
package. `integration.release_metadata` enforces that contract.

1. Run `make quality` from the repository root.
2. Confirm `build/bin/icad --version` and the version in
   `editors/vscode/package.json` match the intended tag.
3. Push an exact semantic-version tag such as `v0.21.0`.

The `Release ICAD` workflow builds and tests the CLI plus optional
`webview/webview` desktop host on Linux x86-64, Windows x86-64, Intel macOS, and
Apple Silicon macOS. It packages the bundled VS Code extension, verifies the tag
against its manifest, and attaches every ZIP/VSIX to the GitHub Release.
Each native ZIP has a companion `.sha256` file. The VS Code extension requires
that checksum before installing the compiler and viewer into its machine-local
extension storage.

Set repository secret `VSCE_PAT` to also publish the VSIX to Visual Studio
Marketplace. Without that secret, GitHub Release publication remains complete
and the workflow reports that Marketplace publication was skipped.

Platform signing, macOS notarization, and named downstream CAD import
certification require project-owned credentials/applications and remain release
operator responsibilities.
