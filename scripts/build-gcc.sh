#!/usr/bin/env bash
# Builds GCC from the GNU sources and installs it in the user's directory (no sudo):
# the native compiler of the project (C++26 with reflection, docs/architecture.md §8).
#
# It uses the configure options of the official gcc Docker image the CI runs in
# (gcc:<version>-trixie), so that the local compiler and the CI's are the same:
# --disable-multilib and the default full bootstrap. Only C and C++ are built; the
# image also builds Fortran and Go, which change nothing for C++.
#
# The assembler and the linker are fixed to the `as` and `ld` found in the PATH
# (--with-as, --with-ld): GCC checks their features when it is configured, and it
# must not pick others later if the PATH changes (on EL9 with gcc-toolset-14 enabled,
# binutils 2.41 instead of the system's 2.35). If those binutils are removed, build
# this GCC again.
#
# Nothing is done when the compiler is already installed. The CI does not run this
# script: its image already has the same GCC (.github/workflows/ci.yml checks that
# the image's version is GCC_VERSION).
#
# System requirements: a C++ compiler to build it (the system's g++), make, m4, bzip2,
# xz, wget, gpgv (to check the release signature) and about 10 GB of free disk for the
# build tree, removed at the end. A full bootstrap takes from half an hour to an hour.
#
# Variables:
#   SOFTWARE_DIR   root of the local dependencies  [~/software]
#   GCC_VERSION    GCC release                     [16.2.0]
set -euo pipefail

SOFTWARE_DIR="${SOFTWARE_DIR:-$HOME/software}"
GCC_VERSION="${GCC_VERSION:-16.2.0}"
PREFIX="$SOFTWARE_DIR/gcc-$GCC_VERSION"
SRC="$SOFTWARE_DIR/src"

if [[ -x "$PREFIX/bin/g++" ]]; then
    echo ">> gcc-$GCC_VERSION already installed in $PREFIX"
    exit 0
fi

as_path="$(command -v as)" || { echo "error: no assembler (as, binutils) in the PATH" >&2; exit 1; }
ld_path="$(command -v ld)" || { echo "error: no linker (ld, binutils) in the PATH" >&2; exit 1; }

# The release and its signature, checked against the GNU keyring
mkdir -p "$SRC"
cd "$SRC"
tarball="gcc-$GCC_VERSION.tar.xz"
url="https://ftpmirror.gnu.org/gcc/gcc-$GCC_VERSION"
if [[ ! -f "$tarball" ]]; then
    wget -q -O "$tarball.part" "$url/$tarball"
    mv "$tarball.part" "$tarball"
fi
wget -q -O "$tarball.sig" "$url/$tarball.sig"
[[ -f gnu-keyring.gpg ]] || wget -q https://ftp.gnu.org/gnu/gnu-keyring.gpg
if ! gpgv --keyring "$SRC/gnu-keyring.gpg" "$tarball.sig" "$tarball"; then
    echo "error: bad signature for $SRC/$tarball: remove it and run this script again" >&2
    exit 1
fi

# GMP, MPFR, MPC and ISL are built in the source tree: no -devel packages needed
src="$SRC/gcc-$GCC_VERSION"
build="$SRC/gcc-$GCC_VERSION-build"
rm -rf "$src" "$build"
tar -xf "$tarball"
(cd "$src" && ./contrib/download_prerequisites)

mkdir -p "$build"
cd "$build"
"$src/configure" --prefix="$PREFIX" --disable-multilib --enable-languages=c,c++ \
    --with-as="$as_path" --with-ld="$ld_path"
make -j"$(nproc)"
make install-strip

# The tarball stays for a later rebuild; the source and build trees take several GB
cd "$SRC"
rm -rf "$src" "$build"
echo ">> gcc-$GCC_VERSION installed in $PREFIX"
