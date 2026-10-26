#!/usr/bin/env bash
# Builds and installs the backend libraries and tools in the user's directory (no sudo):
#   - libpqxx       C++ client for PostgreSQL (static, PIC: linked into the client too)
#   - cpp-httplib   embedded HTTP server (header-only), with zlib compression
#   - protobuf      C++ library and protoc for the contract (static, PIC, with abseil),
#                   and the library again for WebAssembly (the web client), with the
#                   emsdk that scripts/setup-qt.sh installs: run that script first
#   - buf           lint and breaking-change checks of the contract (binary)
#
# The native libraries are built with the project's compiler, the GCC that
# scripts/build-gcc.sh installs in $SOFTWARE_DIR/gcc-$GCC_VERSION (run it first), and
# all of them in C++26, the project's standard. Their executables (protoc) get a run
# path to that GCC's libstdc++, newer than the system's. Each build starts from an
# empty build directory, so that a build configured with another compiler is not reused.
#
# System requirements: cmake 3.25+, ninja, git, libpq (libpq5-devel / libpq-devel) and
# zlib (devel).
#
# Variables:
#   SOFTWARE_DIR         root of the local dependencies  [~/software]
#   GCC_VERSION          GCC of scripts/build-gcc.sh     [16.2.0]
#   EMSDK_DIR            emsdk for the WebAssembly build [$SOFTWARE_DIR/emsdk]
#   LIBPQXX_VERSION      libpqxx tag                     [8.0.2]
#   HTTPLIB_VERSION      cpp-httplib tag (without "v")   [0.61.0]
#   PROTOBUF_VERSION     protobuf tag (without "v")      [36.2]
#   BUF_VERSION          buf release (without "v")       [1.74.0]
set -euo pipefail

SOFTWARE_DIR="${SOFTWARE_DIR:-$HOME/software}"
GCC_VERSION="${GCC_VERSION:-16.2.0}"
LIBPQXX_VERSION="${LIBPQXX_VERSION:-8.0.2}"
HTTPLIB_VERSION="${HTTPLIB_VERSION:-0.61.0}"
PROTOBUF_VERSION="${PROTOBUF_VERSION:-36.2}"
BUF_VERSION="${BUF_VERSION:-1.74.0}"
SRC="$SOFTWARE_DIR/src"
mkdir -p "$SRC"

GCC_DIR="$SOFTWARE_DIR/gcc-$GCC_VERSION"
if [[ ! -x "$GCC_DIR/bin/g++" ]]; then
    echo "error: no GCC $GCC_VERSION in $GCC_DIR: run scripts/build-gcc.sh first" >&2
    exit 1
fi
# The directory of that GCC's libstdc++, for the run path of the executables
LIBSTDCXX_DIR="$(dirname "$(readlink -f "$("$GCC_DIR/bin/g++" -print-file-name=libstdc++.so)")")"

# fetch <git url> <tag> <directory name> [git clone options...]
fetch() {
    local url="$1" tag="$2" name="$3"
    shift 3
    [[ -d "$SRC/$name" ]] || git clone -q --depth 1 --branch "$tag" "$@" "$url" "$SRC/$name"
}

# build <directory name> <install prefix> [cmake options...]: a native build
build() {
    local name="$1" prefix="$2"
    shift 2
    rm -rf "$SRC/$name/build"
    cmake -S "$SRC/$name" -B "$SRC/$name/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DCMAKE_C_COMPILER="$GCC_DIR/bin/gcc" -DCMAKE_CXX_COMPILER="$GCC_DIR/bin/g++" \
        -DCMAKE_CXX_STANDARD=26 -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath,$LIBSTDCXX_DIR" "$@"
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
# with the project's C++ standard (by build()), so that abseil and the project
# agree on the std types abseil aliases (string_view, optional, ...)
fetch https://github.com/protocolbuffers/protobuf.git "v$PROTOBUF_VERSION" "protobuf-$PROTOBUF_VERSION" \
    --recurse-submodules --shallow-submodules
build "protobuf-$PROTOBUF_VERSION" "$SOFTWARE_DIR/protobuf-$PROTOBUF_VERSION" \
    -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_LIBUPB=OFF -Dprotobuf_INSTALL=ON \
    -Dprotobuf_ABSL_PROVIDER=module -DABSL_ENABLE_INSTALL=ON -DABSL_PROPAGATE_CXX_STD=ON

# The same protobuf and abseil for the web client, for Qt's wasm_singlethread
# kit: no threads and Emscripten's default exception handling, as the kit. The
# same version as the native one, which the generated code requires; without
# protoc (the native one generates the code of the WebAssembly build)
EMSDK_DIR="${EMSDK_DIR:-$SOFTWARE_DIR/emsdk}"
protobuf_wasm="$SOFTWARE_DIR/protobuf-$PROTOBUF_VERSION-wasm"
if [[ -f "$EMSDK_DIR/emsdk_env.sh" ]]; then
    (
        # Emscripten needs Python >= 3.10; emsdk_env.sh clears EMSDK_PYTHON
        export EMSDK_PYTHON="$HOME/.local/bin/python3.12"
        # shellcheck disable=SC1091
        source "$EMSDK_DIR/emsdk_env.sh" >/dev/null 2>&1
        export EMSDK_PYTHON="$HOME/.local/bin/python3.12"
        src="$SRC/protobuf-$PROTOBUF_VERSION"
        rm -rf "$src/build-wasm"
        emcmake cmake -S "$src" -B "$src/build-wasm" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$protobuf_wasm" \
            -DCMAKE_CXX_STANDARD=26 -DBUILD_SHARED_LIBS=OFF \
            -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_PROTOC_BINARIES=OFF -Dprotobuf_BUILD_LIBUPB=OFF \
            -Dprotobuf_INSTALL=ON -Dprotobuf_ABSL_PROVIDER=module -DABSL_ENABLE_INSTALL=ON \
            -DABSL_PROPAGATE_CXX_STD=ON
        cmake --build "$src/build-wasm" -j"$(nproc)"
        cmake --install "$src/build-wasm"
    )
    echo ">> protobuf-$PROTOBUF_VERSION for WebAssembly installed in $protobuf_wasm"
else
    echo ">> warning: no emsdk in $EMSDK_DIR, protobuf for WebAssembly skipped (needed by the web" \
         "client): run scripts/setup-qt.sh and then this script again" >&2
fi

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
