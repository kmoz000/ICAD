#!/bin/sh
set -eu

repository="valorisystems/ICAD"
release_tag="latest"
install_plugin=1
install_dependencies=1

usage() {
    printf '%s\n' \
        'Install the ICAD Codex plugin and checksum-verified native toolchain.' \
        '' \
        'Usage: install.sh [--version vX.Y.Z] [--plugin-only|--dependencies-only]'
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --version)
            [ "$#" -ge 2 ] || { printf '%s\n' 'missing value for --version' >&2; exit 2; }
            release_tag="$2"
            shift 2
            ;;
        --plugin-only)
            install_dependencies=0
            shift
            ;;
        --dependencies-only)
            install_plugin=0
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            printf 'unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

for command_name in curl unzip; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'required command is missing: %s\n' "$command_name" >&2
        exit 3
    }
done
if [ "$install_plugin" -eq 1 ]; then
    command -v codex >/dev/null 2>&1 || {
        printf '%s\n' 'Codex CLI is required to register the plugin.' >&2
        exit 3
    }
fi

if [ "$release_tag" = "latest" ]; then
    release_tag=$(curl -fsSL "https://api.github.com/repos/$repository/releases/latest" |
        sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' |
        head -n 1)
    [ -n "$release_tag" ] || { printf '%s\n' 'could not resolve the latest ICAD release' >&2; exit 4; }
fi

release_url="https://github.com/$repository/releases/download/$release_tag"
data_root="${XDG_DATA_HOME:-$HOME/.local/share}/icad"
mkdir -p "$data_root"
temporary_root=$(mktemp -d "${TMPDIR:-/tmp}/icad-install.XXXXXX")
trap 'rm -rf "$temporary_root"' EXIT HUP INT TERM

verify_download() {
    asset_name="$1"
    curl -fL --retry 3 -o "$temporary_root/$asset_name" "$release_url/$asset_name"
    curl -fL --retry 3 -o "$temporary_root/$asset_name.sha256" "$release_url/$asset_name.sha256"
    expected=$(awk 'NR == 1 { print tolower($1) }' "$temporary_root/$asset_name.sha256")
    if command -v sha256sum >/dev/null 2>&1; then
        actual=$(sha256sum "$temporary_root/$asset_name" | awk '{ print tolower($1) }')
    else
        actual=$(shasum -a 256 "$temporary_root/$asset_name" | awk '{ print tolower($1) }')
    fi
    [ "$expected" = "$actual" ] || {
        printf 'SHA-256 verification failed for %s\n' "$asset_name" >&2
        exit 5
    }
}

if [ "$install_plugin" -eq 1 ]; then
    plugin_asset="icad-codex-plugin.zip"
    verify_download "$plugin_asset"
    plugin_staging="$temporary_root/plugin"
    mkdir -p "$plugin_staging"
    unzip -q "$temporary_root/$plugin_asset" -d "$plugin_staging"
    marketplace_source="$plugin_staging/codex-marketplace"
    [ -f "$marketplace_source/.agents/plugins/marketplace.json" ] || {
        printf '%s\n' 'plugin release does not contain the ICAD marketplace' >&2
        exit 6
    }
    marketplace_root="$data_root/codex-marketplace"
    rm -rf "$marketplace_root.new"
    mv "$marketplace_source" "$marketplace_root.new"
    rm -rf "$marketplace_root"
    mv "$marketplace_root.new" "$marketplace_root"
    if codex plugin marketplace list | awk 'NR > 1 { print $1 }' | grep -qx icad; then
        codex plugin marketplace remove icad --json >/dev/null
    fi
    codex plugin marketplace add "$marketplace_root" --json >/dev/null
    codex plugin add icad-agentic-cad@icad --json >/dev/null
    printf 'Installed ICAD Agentic CAD plugin %s.\n' "$release_tag"
fi

if [ "$install_dependencies" -eq 1 ]; then
    operating_system=$(uname -s)
    architecture=$(uname -m)
    case "$operating_system:$architecture" in
        Darwin:arm64) native_asset="icad-macos-arm64" ;;
        Darwin:x86_64) native_asset="icad-macos-x86_64" ;;
        Linux:x86_64|Linux:amd64) native_asset="icad-linux-x86_64" ;;
        *) printf 'no ICAD native release is available for %s/%s\n' "$operating_system" "$architecture" >&2; exit 7 ;;
    esac
    archive="$native_asset.zip"
    verify_download "$archive"
    native_staging="$temporary_root/native"
    mkdir -p "$native_staging"
    unzip -q "$temporary_root/$archive" -d "$native_staging"
    native_root="$data_root/toolchain/$release_tag/$native_asset"
    mkdir -p "$(dirname "$native_root")"
    rm -rf "$native_root.new"
    mv "$native_staging" "$native_root.new"
    rm -rf "$native_root"
    mv "$native_root.new" "$native_root"
    compiler="$native_root/stage/bin/icad"
    if [ "$operating_system" = "Darwin" ]; then
        viewer="$native_root/stage/icad-viewer.app/Contents/MacOS/icad-viewer"
    else
        viewer="$native_root/stage/bin/icad-viewer"
    fi
    [ -x "$compiler" ] && [ -x "$viewer" ] || {
        printf '%s\n' 'native release does not contain the compiler and viewer' >&2
        exit 8
    }
    user_bin="$HOME/.local/bin"
    mkdir -p "$user_bin"
    ln -sfn "$compiler" "$user_bin/icad"
    ln -sfn "$viewer" "$user_bin/icad-viewer"
    "$compiler" --version
    printf 'Installed compiler and viewer under %s.\n' "$native_root"
    case ":${PATH:-}:" in
        *":$user_bin:"*) ;;
        *) printf 'Add %s to PATH to run icad and icad-viewer from a new shell.\n' "$user_bin" ;;
    esac
fi

printf '%s\n' 'Start a new Codex conversation before using the updated ICAD plugin.'
