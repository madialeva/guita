#!/usr/bin/env bash
# Builds and installs in the user's directory (no sudo) the static dependencies of
# the self-contained desktop client (scripts/build-static.sh, docs/architecture.md §9.2):
#   - Qt $QT_VERSION, static, with only the modules the client uses: qtbase,
#     qtshadertools, qtdeclarative, qtwebsockets and the Linguist tools of qttools,
#     in $SOFTWARE_DIR/Qt/$QT_VERSION-static
#   - libpq of PostgreSQL $POSTGRESQL_VERSION, static, the client part only, in
#     $SOFTWARE_DIR/libpq-$POSTGRESQL_VERSION-static
#
# Only the on-demand static build needs them: the native preset, the tests and the
# CI do not. They are built with the GCC that scripts/build-gcc.sh installs (run it
# first); the Qt tools that run during a build (moc, qmlcachegen, lrelease, ...) get
# a run path to its libstdc++, as protoc does. The sources stay in $SOFTWARE_DIR/src
# for later rebuilds, every download is checked against the SHA-256 published by
# its server before anything is built, and nothing is downloaded nor built again
# for what is already installed.
#
# Every Qt feature that would otherwise depend on the -devel packages installed is
# fixed here, so that the result does not change with them: X11 (xcb) only, Qt's own
# copies of the libraries that not every desktop has, and the system's of those that
# every desktop has (zlib, freetype with fontconfig, OpenGL, X11). OpenSSL and D-Bus
# are loaded at run time.
#
# System requirements: cmake 3.25+, ninja, curl, xz, and the development packages of
# OpenSSL, libxcb and xcb-util (-wm, -image, -keysyms, -renderutil, -cursor),
# libxkbcommon and libxkbcommon-x11, fontconfig, freetype, Mesa (GL and EGL), X11 and
# zlib; and make and bzip2 for PostgreSQL. Building Qt takes about half an hour with
# 12 cores, and several GB of free disk for the build trees, removed at the end.
#
# Variables:
#   SOFTWARE_DIR         root of the local dependencies  [~/software]
#   GCC_VERSION          GCC of scripts/build-gcc.sh     [16.2.0]
#   QT_VERSION           Qt release                      [6.12.0]
#   POSTGRESQL_VERSION   PostgreSQL release of libpq     [18.6]
set -euo pipefail

SOFTWARE_DIR="${SOFTWARE_DIR:-$HOME/software}"
GCC_VERSION="${GCC_VERSION:-16.2.0}"
QT_VERSION="${QT_VERSION:-6.12.0}"
POSTGRESQL_VERSION="${POSTGRESQL_VERSION:-18.6}"
SRC="$SOFTWARE_DIR/src"
QT_PREFIX="$SOFTWARE_DIR/Qt/$QT_VERSION-static"
LIBPQ_PREFIX="$SOFTWARE_DIR/libpq-$POSTGRESQL_VERSION-static"

GCC_DIR="$SOFTWARE_DIR/gcc-$GCC_VERSION"
if [[ ! -x "$GCC_DIR/bin/g++" ]]; then
    echo "error: no GCC $GCC_VERSION in $GCC_DIR: run scripts/build-gcc.sh first" >&2
    exit 1
fi
# The directory of that GCC's libstdc++, for the run path of the executables
LIBSTDCXX_DIR="$(dirname "$(readlink -f "$("$GCC_DIR/bin/g++" -print-file-name=libstdc++.so)")")"
CMAKE_ARGS=(-DCMAKE_C_COMPILER="$GCC_DIR/bin/gcc" -DCMAKE_CXX_COMPILER="$GCC_DIR/bin/g++"
    -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath,$LIBSTDCXX_DIR")

# download <url> <file>: downloads <url> into <file> unless it is there, and checks
# it against the SHA-256 published at <url>.sha256 (the first field of that file)
download() {
    local url="$1" file="$2" expected actual
    if [[ ! -f "$file" ]]; then
        curl -fsSL -o "$file.part" "$url"
        mv "$file.part" "$file"
    fi
    expected="$(curl -fsSL "$url.sha256" | awk 'NR == 1 {print $1}')"
    actual="$(sha256sum "$file" | cut -d' ' -f1)"
    if [[ -z "$expected" || "$expected" != "$actual" ]]; then
        echo "error: checksum mismatch for $file: remove it and run this script again" >&2
        exit 1
    fi
}

# ---- Qt ---------------------------------------------------------------------------

# The modules, in build order, and the CMake package that marks each one as installed
QT_MODULES=(qtbase qtshadertools qtdeclarative qtwebsockets qttools)
declare -A QT_MARKER=([qtbase]=Qt6Core [qtshadertools]=Qt6ShaderTools [qtdeclarative]=Qt6Quick
    [qtwebsockets]=Qt6WebSockets [qttools]=Qt6LinguistTools)

# qtbase: static, release, and every system-dependent feature fixed
QTBASE_OPTIONS=(
    -static -release -nomake examples -nomake tests
    # X11 only, with its GLX and EGL integrations; no Wayland nor embedded backends
    -qpa xcb -default-qpa xcb -xcb -xcb-xlib -xkbcommon -bundled-xcb-xinput
    -no-feature-xcb-sm -no-feature-wayland -no-eglfs -no-linuxfb -no-kms -no-gbm
    -no-feature-vnc -no-feature-vkkhrdisplay -no-feature-vulkan
    -no-libudev -no-libinput -no-mtdev -no-tslib -no-evdev
    -opengl desktop -egl
    # Qt's own copies of what not every desktop has, or has in another version
    -qt-pcre -qt-harfbuzz -qt-libpng -qt-libjpeg -qt-doubleconversion
    -no-feature-system-libb2 -no-feature-system-textmarkdownreader
    # The system's of what every desktop has. fontconfig loads the system's
    # freetype, so a copy of Qt's own would leave two in the process
    -system-zlib -system-freetype -fontconfig
    # Loaded at run time: libssl for wss:// and libdbus-1 (accessibility, portals).
    # -dbus-runtime does not reach CMake in 6.12.0, hence the feature itself
    -openssl-runtime -no-feature-dbus-linked
    # Not used by the client: ICU (Qt has its own CLDR data), GLib (Qt's own event
    # loop), Widgets (and with it the GTK theme), SQL, printing (and with it CUPS),
    # session management, and the network extras that would link more libraries
    -no-icu -no-glib -no-widgets -no-feature-sql -no-feature-printsupport
    -no-feature-sessionmanager -no-feature-gssapi -no-feature-brotli
    -no-feature-zstd -no-libproxy -no-journald
)
# qttools: the Linguist tools only (lupdate, lrelease). Its graphical applications
# need Widgets, and qdoc needs libclang
QTTOOLS_OPTIONS=(
    -no-feature-assistant -no-feature-designer -no-feature-distancefieldgenerator
    -no-feature-kmap2qmap -no-feature-pixeltool -no-feature-qdbus -no-feature-qdoc
    -no-feature-clang -no-feature-qev -no-feature-qtdiag -no-feature-qtplugininfo
)

qt_src="$SRC/qt-$QT_VERSION"
qt_url="https://download.qt.io/official_releases/qt/${QT_VERSION%.*}/$QT_VERSION/submodules"
qt_missing=()
for module in "${QT_MODULES[@]}"; do
    [[ -d "$QT_PREFIX/lib/cmake/${QT_MARKER[$module]}" ]] || qt_missing+=("$module")
done

if ((${#qt_missing[@]} == 0)); then
    echo ">> Qt $QT_VERSION (static) already installed in $QT_PREFIX"
else
    # Every source is checked before any module is built
    mkdir -p "$qt_src"
    for module in "${qt_missing[@]}"; do
        tarball="$module-everywhere-src-$QT_VERSION.tar.xz"
        download "$qt_url/$tarball" "$qt_src/$tarball"
        [[ -d "$qt_src/$module-everywhere-src-$QT_VERSION" ]] || tar -xf "$qt_src/$tarball" -C "$qt_src"
    done

    for module in "${qt_missing[@]}"; do
        src="$qt_src/$module-everywhere-src-$QT_VERSION"
        build="$qt_src/build-$module"
        rm -rf "$build"
        mkdir -p "$build"
        (
            cd "$build"
            case "$module" in
                qtbase) "$src/configure" -prefix "$QT_PREFIX" "${QTBASE_OPTIONS[@]}" -- "${CMAKE_ARGS[@]}" ;;
                qttools) "$QT_PREFIX/bin/qt-configure-module" "$src" "${QTTOOLS_OPTIONS[@]}" -- "${CMAKE_ARGS[@]}" ;;
                *) "$QT_PREFIX/bin/qt-configure-module" "$src" -- "${CMAKE_ARGS[@]}" ;;
            esac
            cmake --build . --parallel "$(nproc)"
            cmake --install .
        )
        rm -rf "$build"
        echo ">> $module $QT_VERSION installed in $QT_PREFIX"
    done
fi

# ---- libpq ------------------------------------------------------------------------

# The client part of PostgreSQL only (headers, libpq, the common and port libraries
# libpq.a needs, and pg_config), as in its documentation's client-only installation.
# The shared libpq is removed afterwards, so that FindPostgreSQL can only find
# libpq.a. GSSAPI and LDAP are off by default; ICU, readline and zlib are only used
# by the server and the tools
if [[ -f "$LIBPQ_PREFIX/lib/libpq.a" ]]; then
    echo ">> libpq $POSTGRESQL_VERSION already installed in $LIBPQ_PREFIX"
else
    mkdir -p "$SRC"
    tarball="postgresql-$POSTGRESQL_VERSION.tar.bz2"
    download "https://ftp.postgresql.org/pub/source/v$POSTGRESQL_VERSION/$tarball" "$SRC/$tarball"
    src="$SRC/postgresql-$POSTGRESQL_VERSION"
    [[ -d "$src" ]] || tar -xf "$SRC/$tarball" -C "$SRC"
    build="$SRC/postgresql-$POSTGRESQL_VERSION-build"
    rm -rf "$build" "$LIBPQ_PREFIX"
    mkdir -p "$build"
    (
        cd "$build"
        "$src/configure" --prefix="$LIBPQ_PREFIX" CC="$GCC_DIR/bin/gcc" \
            --with-ssl=openssl --without-icu --without-readline --without-zlib
        for dir in src/include src/common src/port src/interfaces/libpq src/bin/pg_config; do
            make -C "$dir" -j"$(nproc)" install
        done
    )
    rm -rf "$build"
    rm -f "$LIBPQ_PREFIX"/lib/libpq.so*
    echo ">> libpq $POSTGRESQL_VERSION installed in $LIBPQ_PREFIX"
fi
