#!/usr/bin/env bash
# Builds from the signed sources and installs in the user's directory (no sudo) a GCC
# cross compiler that runs on Linux and builds 64-bit Windows executables
# (x86_64-w64-mingw32), the base of a self-contained Windows client
# (docs/architecture.md §8). Everything goes to $SOFTWARE_DIR/gcc-$GCC_VERSION-mingw:
#   - binutils $BINUTILS_VERSION   assembler, linker and the other binary tools
#   - mingw-w64 $MINGW_VERSION     Windows and C runtime headers, the C runtime (CRT)
#                                  and winpthreads
#   - GCC $GCC_VERSION, C and C++  the same release as the native compiler
#
# The target is fixed: Windows 10 or later (the minimum of Qt 6.12) with the Universal
# C Runtime (UCRT), SEH exceptions, POSIX threads on winpthreads and GCC's native
# Windows TLS for thread_local. Only the static runtime is built (libgcc, libstdc++,
# winpthreads): whatever the compiler builds carries it inside, with no extra flags,
# and needs only DLLs of the system (KERNEL32.dll and the UCRT's).
#
# The prefix is also the sysroot: mingw-w64 goes to x86_64-w64-mingw32/{include,lib},
# next to libstdc++, and a relative link `mingw` points there, where GCC looks for the
# system headers and libraries. The compilers find everything relative to themselves,
# so the directory can be moved.
#
# It is built with the native GCC of scripts/build-gcc.sh (run it first), from the same
# GCC tarball in $SOFTWARE_DIR/src. Every download is checked against its signature
# before anything is built or removed: GCC and binutils with the GNU keyring, as
# scripts/build-gcc.sh does, and mingw-w64 with the key of its release manager,
# fetched once from keyserver.ubuntu.com by the fingerprint fixed below. Nothing is
# done when the compiler is already installed; an install that did not finish is
# removed and built again from the start.
#
# The install ends checking the compiler: it builds a C++26 test program with threads,
# thread_local, exceptions and shared_ptr, fails if the program imports any DLL that is
# not of the system or uses the emulated TLS, and runs it with Wine if there is one,
# in a Wine prefix of its own (~/.wine is not touched) and without a display.
#
#   scripts/build-gcc-mingw.sh                       build and install, then check
#   scripts/build-gcc-mingw.sh --check               only check the installed compiler
#   scripts/build-gcc-mingw.sh --check <executable>  only check the DLLs and the TLS of
#                                                    <executable>
#
# On demand only: the native build, the tests and the CI do not need it.
#
# System requirements: those of scripts/build-gcc.sh (make, m4, bzip2, xz, wget,
# gpgv), and about 9 GB of free disk for the build trees, removed at the end. It takes
# about a quarter of an hour with 12 cores; the compiler takes 0.5 GB.
#
# Variables:
#   SOFTWARE_DIR       root of the local dependencies  [~/software]
#   GCC_VERSION        GCC release                     [16.2.0]
#   BINUTILS_VERSION   binutils release                [2.47]
#   MINGW_VERSION      mingw-w64 release               [14.0.0]
set -euo pipefail

SOFTWARE_DIR="${SOFTWARE_DIR:-$HOME/software}"
GCC_VERSION="${GCC_VERSION:-16.2.0}"
BINUTILS_VERSION="${BINUTILS_VERSION:-2.47}"
MINGW_VERSION="${MINGW_VERSION:-14.0.0}"
TARGET=x86_64-w64-mingw32
PREFIX="$SOFTWARE_DIR/gcc-$GCC_VERSION-mingw"
SRC="$SOFTWARE_DIR/src"
# The key that signs the mingw-w64 releases (Jonathan Yong)
MINGW_KEY=CAF5641F74F7DFBA88AE205693BDB53CD4EBC740
# libstdc++.a is installed last: the compiler driver alone is there halfway
INSTALLED="$PREFIX/$TARGET/lib/libstdc++.a"

usage() {
    echo "usage: $0 [--check [<executable>]]" >&2
    exit 2
}

require_compiler() {
    if [[ ! -f "$INSTALLED" ]]; then
        echo "error: no cross compiler in $PREFIX: run scripts/build-gcc-mingw.sh first" >&2
        exit 1
    fi
}

# check_executable <executable>: fails, naming them, if <executable> imports a DLL that
# is not of the system, the Windows kernel's or the UCRT's (objdump -p), or carries
# symbols of the emulated TLS (nm: an executable without symbols shows none)
check_executable() {
    local exe="$1" problems=() dlls dll symbol
    dlls="$(LC_ALL=C "$PREFIX/bin/$TARGET-objdump" -p "$exe" | sed -n 's/^[[:space:]]*DLL Name: //p')"
    while read -r dll; do
        case "${dll,,}" in
            "" | kernel32.dll | ucrtbase.dll | api-ms-win-crt-*.dll) ;;
            *) problems+=("imports $dll") ;;
        esac
    done <<<"$dlls"
    while read -r symbol; do
        problems+=("uses the emulated TLS: $symbol")
    done < <(LC_ALL=C "$PREFIX/bin/$TARGET-nm" "$exe" 2>/dev/null | awk '$NF ~ /^__emutls/ {print $NF}')

    if ((${#problems[@]})); then
        echo "error: $exe does not pass the check:" >&2
        printf '  %s\n' "${problems[@]}" >&2
        return 1
    fi
    echo ">> $exe imports only these DLLs of the system:"
    sed 's/^/     /' <<<"$dlls"
}

# The test program and what it prints: threads that count in a thread_local variable
# and catch an exception, a shared_ptr copied into each, and a C++26 feature
EXPECTED="threads=4 thread_local=4000 exceptions=4 shared_ptr=1 c++26=26"
write_test_program() {
    cat <<'EOF'
#include <atomic>
#include <cstdio>
#include <format>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

thread_local int counter = 0;

// Pack indexing, new in C++26
template <typename... Ts>
constexpr auto first(Ts... values) {
    return values...[0];
}

}  // namespace

int main() {
    constexpr int threads = 4, rounds = 1000;
    std::atomic<int> total = 0, caught = 0;
    auto shared = std::make_shared<int>(26);
    {
        std::vector<std::jthread> workers;
        for (int i = 0; i < threads; ++i) {
            workers.emplace_back([&total, &caught, shared] {
                for (int round = 0; round < rounds; ++round) {
                    ++counter;
                }
                total += counter;
                try {
                    throw std::runtime_error("thrown in a thread");
                } catch (const std::runtime_error &) {
                    ++caught;
                }
            });
        }
    }
    // Not std::println: on Windows it needs libstdc++exp.a (-lstdc++exp)
    std::fputs(std::format("threads={} thread_local={} exceptions={} shared_ptr={} c++26={}\n",
                           threads, total.load(), caught.load(), shared.use_count(),
                           first(*shared, 0))
                   .c_str(),
               stdout);
    return total == threads * rounds && caught == threads && shared.use_count() == 1 ? 0 : 1;
}
EOF
}

# The check's directory: the test program, and the Wine prefix and its log
check_dir=""
cleanup_check() {
    if [[ -d "$check_dir/wine" ]]; then
        WINEPREFIX="$check_dir/wine" wineserver -k 2>/dev/null || true
        WINEPREFIX="$check_dir/wine" wineserver -w 2>/dev/null || true
    fi
    rm -rf "$check_dir"
}

# check_compiler: builds the test program with no extra flags, checks it, and runs it
# with Wine if it is in the PATH
check_compiler() {
    local output status=0
    check_dir="$(mktemp -d)"
    trap cleanup_check EXIT
    cd "$check_dir"
    write_test_program >test.cpp
    "$PREFIX/bin/$TARGET-g++" -std=c++26 -O2 -o test.exe test.cpp
    check_executable test.exe

    if ! command -v wine >/dev/null; then
        echo ">> warning: no wine in the PATH: test.exe was not run" >&2
        return
    fi
    # A prefix of its own, no display so that no window opens, no offer to install
    # Mono or Gecko, and a time limit should the program hang
    output="$(env -u DISPLAY -u WAYLAND_DISPLAY -u WINEARCH WINEPREFIX="$check_dir/wine" \
        WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" timeout 120 wine test.exe \
        2>wine.log)" || status=$?
    output="${output//$'\r'/}"
    if ((status != 0)) || [[ "$output" != "$EXPECTED" ]]; then
        echo "error: test.exe failed under $(wine --version) (exit status $status), printing:" >&2
        sed 's/^/  /' <<<"$output" >&2
        echo "instead of:" >&2
        echo "  $EXPECTED" >&2
        tail -n 20 wine.log >&2
        exit 1
    fi
    echo ">> test.exe under $(wine --version): $output"
    echo ">> the cross compiler in $PREFIX works"
}

case "${1:-}" in
    "") [[ $# -eq 0 ]] || usage ;;
    --check)
        require_compiler
        case $# in
            1) check_compiler ;;
            2) check_executable "$2" ;;
            *) usage ;;
        esac
        exit ;;
    *) usage ;;
esac

if [[ -f "$INSTALLED" ]]; then
    echo ">> gcc-$GCC_VERSION-mingw already installed in $PREFIX"
    exit 0
fi

GCC_DIR="$SOFTWARE_DIR/gcc-$GCC_VERSION"
if [[ ! -x "$GCC_DIR/bin/g++" ]]; then
    echo "error: no GCC $GCC_VERSION in $GCC_DIR: run scripts/build-gcc.sh first" >&2
    exit 1
fi

# ---- The sources, checked before anything is built or removed ---------------------

mkdir -p "$SRC"
cd "$SRC"

# fetch <url> <file>: downloads <url> into <file> unless it is there, and its signature
# <url>.sig into <file>.sig every time
fetch() {
    local url="$1" file="$2"
    if [[ ! -f "$file" ]]; then
        wget -q -O "$file.part" "$url" || { echo "error: cannot download $url" >&2; exit 1; }
        mv "$file.part" "$file"
    fi
    wget -q -O "$file.sig" "$url.sig" || { echo "error: cannot download $url.sig" >&2; exit 1; }
}

# verify <keyring> <file> [<fingerprint>]: checks <file>.sig with the keys of <keyring>
# and, given a fingerprint, that the key that made the signature is that one
verify() {
    local keyring="$1" file="$2" key="${3:-}" status
    if status="$(gpgv --status-fd 1 --keyring "$SRC/$keyring" "$file.sig" "$file")"; then
        [[ -z "$key" ]] && return
        awk -v key="$key" '$2 == "VALIDSIG" && $NF == key {found = 1} END {exit !found}' \
            <<<"$status" && return
    fi
    echo "error: bad signature for $SRC/$file: remove it and run this script again" >&2
    exit 1
}

gcc_tarball="gcc-$GCC_VERSION.tar.xz"
binutils_tarball="binutils-$BINUTILS_VERSION.tar.xz"
mingw_tarball="mingw-w64-v$MINGW_VERSION.tar.bz2"
fetch "https://ftpmirror.gnu.org/gcc/gcc-$GCC_VERSION/$gcc_tarball" "$gcc_tarball"
fetch "https://ftpmirror.gnu.org/binutils/$binutils_tarball" "$binutils_tarball"
fetch "https://downloads.sourceforge.net/project/mingw-w64/mingw-w64/mingw-w64-release/$mingw_tarball" \
    "$mingw_tarball"

if [[ ! -f gnu-keyring.gpg ]]; then
    wget -q -O gnu-keyring.gpg.part https://ftp.gnu.org/gnu/gnu-keyring.gpg ||
        { echo "error: cannot download the GNU keyring" >&2; exit 1; }
    mv gnu-keyring.gpg.part gnu-keyring.gpg
fi
# The key comes armored; gpgv reads binary keyrings: the base64 lines between the
# headers and the checksum
if [[ ! -f mingw-w64-keyring.gpg ]]; then
    if ! wget -q -O - "https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x$MINGW_KEY" |
        sed -e '1,/^$/d' -e '/^[=-]/,$d' | base64 -d >mingw-w64-keyring.gpg.part; then
        echo "error: cannot fetch the mingw-w64 key $MINGW_KEY from keyserver.ubuntu.com" >&2
        exit 1
    fi
    mv mingw-w64-keyring.gpg.part mingw-w64-keyring.gpg
fi

verify gnu-keyring.gpg "$gcc_tarball"
verify gnu-keyring.gpg "$binutils_tarball"
verify mingw-w64-keyring.gpg "$mingw_tarball" "$MINGW_KEY"

# ---- The build, in a work directory of its own ------------------------------------

# The compilers and flags of the environment are not meant for Windows, and the
# configure scripts of mingw-w64 would take them
unset CC CXX CPP CFLAGS CXXFLAGS CPPFLAGS LDFLAGS

# The source and build trees, apart from those of scripts/build-gcc.sh. GMP, MPFR, MPC
# and ISL are built in the GCC source tree, as in the native compiler
work="$SRC/gcc-$GCC_VERSION-mingw-build"
rm -rf "$work"
mkdir -p "$work"
cd "$work"
for tarball in "$gcc_tarball" "$binutils_tarball" "$mingw_tarball"; do
    tar -xf "$SRC/$tarball"
done
(cd "gcc-$GCC_VERSION" && ./contrib/download_prerequisites)
mingw_src="$work/mingw-w64-v$MINGW_VERSION"

# Every download is checked: only now does an install that did not finish go away
rm -rf "$PREFIX"
# The cross tools are found by name (x86_64-w64-mingw32-gcc, ...), and the system may
# have others of an older release
export PATH="$PREFIX/bin:$PATH"
build_cc=(CC="$GCC_DIR/bin/gcc" CXX="$GCC_DIR/bin/g++")

# 1. binutils
mkdir build-binutils
(
    cd build-binutils
    "../binutils-$BINUTILS_VERSION/configure" --prefix="$PREFIX" --target="$TARGET" \
        --with-sysroot="$PREFIX" "${build_cc[@]}"
    make -j"$(nproc)"
    make install-strip
)

# 2. The mingw-w64 headers, with the UCRT and Windows 10 as defaults, and the link to
# them where GCC looks for the system headers and libraries (<sysroot>/mingw)
mkdir build-headers
(
    cd build-headers
    "$mingw_src/mingw-w64-headers/configure" --prefix="$PREFIX/$TARGET" --host="$TARGET" \
        --with-default-msvcrt=ucrt --with-default-win32-winnt=0x0A00
    make install
)
ln -s "$TARGET" "$PREFIX/mingw"

# 3. The compiler alone (all-gcc), to build the C runtime with it. POSIX threads, native
# TLS and the static runtime only; libstdc++'s steady_clock and sleeps on the
# clock_gettime and nanosleep of winpthreads, instead of the wall clock
mkdir build-gcc
(
    cd build-gcc
    "../gcc-$GCC_VERSION/configure" --prefix="$PREFIX" --target="$TARGET" \
        --with-sysroot="$PREFIX" --enable-languages=c,c++ --disable-multilib \
        --enable-threads=posix --enable-tls --disable-shared --enable-libstdcxx-time=yes \
        "${build_cc[@]}"
    make -j"$(nproc)" all-gcc
    make install-gcc
)

# 4. The C runtime, 64-bit only, with the UCRT as libmsvcrt.a
mkdir build-crt
(
    cd build-crt
    "$mingw_src/mingw-w64-crt/configure" --prefix="$PREFIX/$TARGET" --host="$TARGET" \
        --enable-lib64 --disable-lib32 --with-default-msvcrt=ucrt
    make -j"$(nproc)"
    make install
)

# 5. winpthreads, static only: its configure links no program, so it does not need
# libgcc yet
mkdir build-winpthreads
(
    cd build-winpthreads
    "$mingw_src/mingw-w64-libraries/winpthreads/configure" --prefix="$PREFIX/$TARGET" \
        --host="$TARGET" --disable-shared --enable-static
    make -j"$(nproc)"
    make install
)

# 6. The rest of GCC: libgcc, libstdc++ and the other runtime libraries
(
    cd build-gcc
    make -j"$(nproc)"
    make install-strip
)

# The tarballs stay for a later rebuild; the source and build trees take several GB
cd "$SRC"
rm -rf "$work"
echo ">> gcc-$GCC_VERSION-mingw installed in $PREFIX"
check_compiler
