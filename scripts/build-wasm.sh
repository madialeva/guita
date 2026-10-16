#!/usr/bin/env bash
# Builds the Qt/QML client for WebAssembly and copies the static files to
# build/web, the docroot served by the `server` executable (scripts/run-server.sh).
# Qt/emsdk versions: those of the "wasm" preset (CMakePresets.json) and scripts/setup-qt.sh
set -euo pipefail

SOFTWARE_DIR="${SOFTWARE_DIR:-$HOME/software}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Emscripten needs Python >= 3.10. emsdk_env.sh uses it and then clears the
# variable, so it is exported again for emcc afterwards.
export EMSDK_PYTHON="$HOME/.local/bin/python3.12"
# shellcheck disable=SC1091
source "$SOFTWARE_DIR/emsdk/emsdk_env.sh" >/dev/null 2>&1
export EMSDK_PYTHON="$HOME/.local/bin/python3.12"

cd "$ROOT"
cmake --preset wasm
cmake --build --preset wasm

WEB="$ROOT/build/web"
mkdir -p "$WEB"
cd "$ROOT/build/wasm/bin"
cp client.html client.js client.wasm qtloader.js "$WEB"/
if [[ -f qtlogo.svg ]]; then cp qtlogo.svg "$WEB"/; fi
cp client.html "$WEB/index.html"

echo ">> Web client in $WEB"
