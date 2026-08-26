# Releasing ICAD

Release metadata must agree across CMake, the CLI/MCP servers, and the VS Code
package. `integration.release_metadata` enforces that contract.

1. Run `make quality` from the repository root.
2. Confirm `build/bin/icad --version` and the version in
   `editors/vscode/package.json` match the intended tag.
3. Push an exact semantic-version tag such as `v0.21.0`.

The `Release ICAD` workflow builds and tests the CLI plus native Qt/OpenGL
desktop viewer on Linux x86-64, Windows x86-64, Intel macOS, and
Apple Silicon macOS. It packages the bundled VS Code extension, verifies the tag
against its manifest, and attaches every ZIP/VSIX to the GitHub Release.
Each native ZIP has a companion `.sha256` file. The VS Code extension requires
that checksum before installing the compiler and viewer into its machine-local
extension storage.

The same workflow validates and packages the bundled Codex marketplace as
`icad-codex-plugin.zip`. It also publishes `install.sh` and `install.ps1` for
command-line setup. The plugin archive and both installers have companion
`.sha256` files, while the installers independently verify the plugin and native
archives before installing them. Keep these asset names stable because the
README and the plugin's automatic dependency bootstrap use the GitHub
`releases/latest/download` endpoints.

The dedicated `Release ICAD Studio Viewer` workflow independently builds the
`Viewer` install component in `MinSizeRel`, deploys only its required Qt runtime,
runs the compiler-to-renderer self-test, and attaches maximum-compression
`.tar.xz` (Linux/macOS) or `.7z` (Windows) archives and SHA-256 checksums.
macOS bundles always include the ICAD `.icns`, bundle metadata, `.icad` document
association, and a verified signature. Local/credential-free builds use an
ad-hoc signature so users can approve the downloaded app with macOS **Open** or
**Open Anyway**. A production release operator should configure
`ICAD_MACOS_CODESIGN_IDENTITY` with an imported Developer ID Application
identity and notarize the resulting archive before public distribution.

Set repository secret `VSCE_PAT` to also publish the VSIX to Visual Studio
Marketplace. Without that secret, GitHub Release publication remains complete
and the workflow reports that Marketplace publication was skipped.

Platform signing, macOS notarization, and named downstream CAD import
certification require project-owned credentials/applications and remain release
operator responsibilities.
