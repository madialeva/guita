#!/usr/bin/env bash
# Builds and installs the backend libraries in the user's directory (no sudo):
#   - libpqxx       C++ client for PostgreSQL (static, PIC: linked into the client too)
#   - cpp-httplib   embedded HTTP server (header-only), with zlib compression
#   - glaze         JSON with compile-time reflection (header-only, C++23)
#
# System requirements: g++ with C++23 (GCC 13+), cmake 3.31+, ninja, git, libpq (libpq5-devel /
# libpq-devel) and zlib (devel).
#
# Variables:
#   SOFTWARE_DIR         root of the local dependencies  [~/software]
#   LIBPQXX_VERSION      libpqxx tag                     [8.0.2]
#   HTTPLIB_VERSION      cpp-httplib tag (without "v")   [0.61.0]
#   GLAZE_VERSION        glaze tag (without "v")         [9.0.0]
set -euo pipefail

SOFTWARE_DIR="${SOFTWARE_DIR:-$HOME/software}"
LIBPQXX_VERSION="${LIBPQXX_VERSION:-8.0.2}"
HTTPLIB_VERSION="${HTTPLIB_VERSION:-0.61.0}"
GLAZE_VERSION="${GLAZE_VERSION:-9.0.0}"
SRC="$SOFTWARE_DIR/src"
mkdir -p "$SRC"

# fetch <git url> <tag> <directory name>
fetch() {
    [[ -d "$SRC/$3" ]] || git clone -q --depth 1 --branch "$2" "$1" "$SRC/$3"
}

# build <directory name> <install prefix> [cmake options...]
build() {
    local name="$1" prefix="$2"
    shift 2
    cmake -S "$SRC/$name" -B "$SRC/$name/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" "$@"
    cmake --build "$SRC/$name/build" -j"$(nproc)"
    cmake --install "$SRC/$name/build"
    echo ">> $name installed in $prefix"
}

fetch https://github.com/jtv/libpqxx.git "$LIBPQXX_VERSION" "libpqxx-$LIBPQXX_VERSION"
build "libpqxx-$LIBPQXX_VERSION" "$SOFTWARE_DIR/libpqxx-$LIBPQXX_VERSION" \
    -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_TEST=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF

# TLS is terminated by the reverse proxy; Brotli and zstd stay off so the
# result does not depend on which optional libraries happen to be installed
fetch https://github.com/yhirose/cpp-httplib.git "v$HTTPLIB_VERSION" "cpp-httplib-$HTTPLIB_VERSION"
build "cpp-httplib-$HTTPLIB_VERSION" "$SOFTWARE_DIR/cpp-httplib-$HTTPLIB_VERSION" \
    -DHTTPLIB_COMPILE=OFF -DHTTPLIB_REQUIRE_ZLIB=ON \
    -DHTTPLIB_USE_OPENSSL_IF_AVAILABLE=OFF \
    -DHTTPLIB_USE_BROTLI_IF_AVAILABLE=OFF -DHTTPLIB_USE_ZSTD_IF_AVAILABLE=OFF

# Header-only; its CMake install requires CMake 3.31+
fetch https://github.com/stephenberry/glaze.git "v$GLAZE_VERSION" "glaze-$GLAZE_VERSION"
build "glaze-$GLAZE_VERSION" "$SOFTWARE_DIR/glaze-$GLAZE_VERSION" \
    -DBUILD_TESTING=OFF -Dglaze_DEVELOPER_MODE=OFF -Dglaze_INSTALL=ON
