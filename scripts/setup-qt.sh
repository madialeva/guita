#!/usr/bin/env bash
# Installs in the user's directory (no sudo) the Qt toolchain of the project:
#   - Qt $QT_VERSION desktop (linux_gcc_64): native client, and host of the WASM kit
#   - Qt $QT_VERSION wasm_singlethread: web client
#     (both with the qtgrpc module, which contains Qt Protobuf)
#   - emsdk $EMSDK_VERSION: the Emscripten release that Qt version requires
#     (QT_EMCC_RECOMMENDED_VERSION in the kit's QtPublicWasmToolchainHelpers.cmake)
#   - Python 3.12 through uv (standard location): Emscripten needs >= 3.10, EL9 ships 3.9
set -euo pipefail

SOFTWARE_DIR="${SOFTWARE_DIR:-$HOME/software}"
QT_VERSION="${QT_VERSION:-6.12.0}"
EMSDK_VERSION="${EMSDK_VERSION:-5.0.5}"
QT_DIR="$SOFTWARE_DIR/Qt"
EMSDK_DIR="$SOFTWARE_DIR/emsdk"
AQT="$SOFTWARE_DIR/aqt-venv/bin/aqt"

mkdir -p "$SOFTWARE_DIR"
cd "$SOFTWARE_DIR"                     # aqtinstall writes aqtinstall.log to the cwd

if [[ ! -x "$AQT" ]]; then
    python3 -m venv "$SOFTWARE_DIR/aqt-venv"
    "$SOFTWARE_DIR/aqt-venv/bin/pip" install -q --upgrade pip aqtinstall
fi

# Modules beyond aqt's defaults: qttasktree (dependency of QmlAssetDownloader)
# and qtgrpc (Qt Protobuf, for the client side of the contract). A kit is
# (re)installed when it or one of these modules is missing
MODULES=(qttasktree qtgrpc)
[[ -d "$QT_DIR/$QT_VERSION/gcc_64/lib/cmake/Qt6Protobuf" ]] ||
    "$AQT" install-qt linux desktop "$QT_VERSION" linux_gcc_64 -m "${MODULES[@]}" -O "$QT_DIR"
[[ -d "$QT_DIR/$QT_VERSION/wasm_singlethread/lib/cmake/Qt6Protobuf" ]] ||
    "$AQT" install-qt all_os wasm "$QT_VERSION" wasm_singlethread -m "${MODULES[@]}" -O "$QT_DIR"

uv python install 3.12                 # creates ~/.local/bin/python3.12
export EMSDK_PYTHON="$HOME/.local/bin/python3.12"

[[ -d "$EMSDK_DIR" ]] || git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
git -C "$EMSDK_DIR" pull -q
"$EMSDK_DIR/emsdk" install "$EMSDK_VERSION"
"$EMSDK_DIR/emsdk" activate "$EMSDK_VERSION"

echo
echo ">> Done. Build with: cmake --preset native && cmake --build --preset native"
echo "   and the web client with: scripts/build-wasm.sh"
