#!/usr/bin/env bash
# Builds and installs the backend libraries and tools in the user's directory (no sudo):
#   - libpqxx       C++ client for PostgreSQL (static, PIC: linked into the client too)
#   - cpp-httplib   embedded HTTP server (header-only), with zlib compression
#   - protobuf      C++ library and protoc for the contract (static, PIC, with abseil)
#   - buf           lint and breaking-change checks of the contract (binary)
#
# System requirements: g++ with C++23 (GCC 13+), cmake 3.21+, ninja, git, libpq (libpq5-devel /
# libpq-devel) and zlib (devel).
#
# Variables:
#   SOFTWARE_DIR         root of the local dependencies  [~/software]
#   LIBPQXX_VERSION      libpqxx tag                     [8.0.2]
#   HTTPLIB_VERSION      cpp-httplib tag (without "v")   [0.61.0]
#   PROTOBUF_VERSION     protobuf tag (without "v")      [36.2]
#   BUF_VERSION          buf release (without "v")       [1.74.0]
set -euo pipefail

SOFTWARE_DIR="${SOFTWARE_DIR:-$HOME/software}"
LIBPQXX_VERSION="${LIBPQXX_VERSION:-8.0.2}"
HTTPLIB_VERSION="${HTTPLIB_VERSION:-0.61.0}"
PROTOBUF_VERSION="${PROTOBUF_VERSION:-36.2}"
BUF_VERSION="${BUF_VERSION:-1.74.0}"
SRC="$SOFTWARE_DIR/src"
mkdir -p "$SRC"

# fetch <git url> <tag> <directory name> [git clone options...]
fetch() {
    local url="$1" tag="$2" name="$3"
    shift 3
    [[ -d "$SRC/$name" ]] || git clone -q --depth 1 --branch "$tag" "$@" "$url" "$SRC/$name"
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

# abseil comes as a submodule and is installed next to protobuf. Both are built
# with the project's C++ standard, so that abseil and the project agree on the
# std types abseil aliases (string_view, optional, ...)
fetch https://github.com/protocolbuffers/protobuf.git "v$PROTOBUF_VERSION" "protobuf-$PROTOBUF_VERSION" \
    --recurse-submodules --shallow-submodules
build "protobuf-$PROTOBUF_VERSION" "$SOFTWARE_DIR/protobuf-$PROTOBUF_VERSION" \
    -DCMAKE_CXX_STANDARD=23 -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_LIBUPB=OFF -Dprotobuf_INSTALL=ON \
    -Dprotobuf_ABSL_PROVIDER=module -DABSL_ENABLE_INSTALL=ON -DABSL_PROPAGATE_CXX_STD=ON

# buf: a single binary, checked against the release checksums
buf_dir="$SOFTWARE_DIR/buf-$BUF_VERSION"
if [[ ! -x "$buf_dir/bin/buf" ]]; then
    buf_url="https://github.com/bufbuild/buf/releases/download/v$BUF_VERSION"
    mkdir -p "$buf_dir/bin"
    curl -fsSL -o "$buf_dir/bin/buf" "$buf_url/buf-Linux-x86_64"
    expected="$(curl -fsSL "$buf_url/sha256.txt" | awk '$2 == "buf-Linux-x86_64" {print $1}')"
    actual="$(sha256sum "$buf_dir/bin/buf" | cut -d' ' -f1)"
    if [[ -z "$expected" || "$expected" != "$actual" ]]; then
        rm -f "$buf_dir/bin/buf"
        echo "error: checksum mismatch for buf $BUF_VERSION" >&2
        exit 1
    fi
    chmod +x "$buf_dir/bin/buf"
fi
echo ">> buf-$BUF_VERSION installed in $buf_dir"
