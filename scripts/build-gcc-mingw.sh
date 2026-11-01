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
if [[ -f "$PREFIX/$TARGET/lib/libstdc++.a" ]]; then
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
