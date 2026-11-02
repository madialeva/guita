#!/usr/bin/env bash
# Builds the self-contained client for 64-bit Windows (docs/architecture.md §9.2): the
# client of the windows preset (CMakePresets.json), cross-built on Linux with Qt, the
# embedded backend, libpq with TLS and the C++ runtime linked into client.exe, which is
# copied without symbols to build/dist/client.exe. Then checks that the executable
# imports only DLLs of Windows: it fails, naming them, on any DLL that is not in the
# list below.
#
#   scripts/build-windows.sh                       build, copy and check the client
#   scripts/build-windows.sh --check <executable>  only check <executable>
#
# On demand only: the native build, the tests and the CI do not need it. The cross
# compiler, Qt for Windows and the libraries come from the components for Windows of
# toolchain-cpp, installed first.
#
# Variables:
#   SOFTWARE_DIR         the installation directory of toolchain-cpp, cloned next to
#                        guita                          [../toolchain-cpp/software]
#   GCC_VERSION          release of the cross compiler   [16.2.0]
#   QT_VERSION           Qt release                      [6.12.0]
#   POSTGRESQL_VERSION   release of libpq                [18.6]
#   OPENSSL_VERSION      release of OpenSSL              [3.5.9]
#   LIBPQXX_VERSION      release of libpqxx              [8.0.2]
#   HTTPLIB_VERSION      release of cpp-httplib          [0.61.0]
#   PROTOBUF_VERSION     release of protobuf             [36.2]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SOFTWARE_DIR="${SOFTWARE_DIR:-$(cd "$ROOT/.." && pwd)/toolchain-cpp/software}"
GCC_VERSION="${GCC_VERSION:-16.2.0}"
QT_VERSION="${QT_VERSION:-6.12.0}"
POSTGRESQL_VERSION="${POSTGRESQL_VERSION:-18.6}"
OPENSSL_VERSION="${OPENSSL_VERSION:-3.5.9}"
LIBPQXX_VERSION="${LIBPQXX_VERSION:-8.0.2}"
HTTPLIB_VERSION="${HTTPLIB_VERSION:-0.61.0}"
PROTOBUF_VERSION="${PROTOBUF_VERSION:-36.2}"

TOOLS="$SOFTWARE_DIR/gcc-$GCC_VERSION-mingw/bin/x86_64-w64-mingw32"

# The DLLs of Windows the client may import, in lower case: all of them in System32
# of Windows 10 1809, the oldest Windows Qt 6.12 supports, with the first Windows
# that has them according to Microsoft's API documentation. A DLL that is not here
# fails the check until it is reviewed and added
WINDOWS_DLLS=(
    # The kernel, the user interface and the shell (Windows 2000 / XP)
    kernel32.dll ntdll.dll user32.dll gdi32.dll advapi32.dll shell32.dll shlwapi.dll
    ole32.dll oleaut32.dll comdlg32.dll imm32.dll version.dll winmm.dll setupapi.dll
    userenv.dll wtsapi32.dll netapi32.dll authz.dll dbghelp.dll mscms.dll
    # Theming and the desktop window manager (XP, Vista), DPI awareness (8.1)
    uxtheme.dll dwmapi.dll shcore.dll
    # Graphics: OpenGL (Windows 95), Direct3D 9 (XP), DXGI (Vista), Direct3D 11 and
    # DirectWrite (7), Direct3D 12 (10)
    opengl32.dll d3d9.dll dxgi.dll d3d11.dll dwrite.dll d3d12.dll
    # Network: Winsock (95, 2000), IP helper (2000), WinHTTP (2000 SP3)
    ws2_32.dll wsock32.dll iphlpapi.dll winhttp.dll
    # Security and TLS: SSPI and Schannel (2000), certificates (2000), CNG (Vista)
    secur32.dll crypt32.dll bcrypt.dll ncrypt.dll
    # API sets: WinRT strings and activation, and WaitOnAddress (8)
    api-ms-win-core-winrt-l1-1-0.dll api-ms-win-core-winrt-string-l1-1-0.dll
    api-ms-win-core-synch-l1-2-0.dll
    # The Universal C Runtime, part of Windows since 10
    ucrtbase.dll
)

usage() {
    echo "usage: $0 [--check <executable>]" >&2
    exit 2
}

# allowed <dll>: whether <dll> is in WINDOWS_DLLS or is an API set of the UCRT
allowed() {
    local dll="${1,,}" known
    [[ "$dll" == api-ms-win-crt-*.dll ]] && return 0
    for known in "${WINDOWS_DLLS[@]}"; do
        [[ "$dll" == "$known" ]] && return 0
    done
    return 1
}

# check <executable>: the DLLs it imports (objdump -p of the cross compiler), each of
# them in the list
check() {
    local exe="$1" dlls problems=() dll
    [[ -x "$TOOLS-objdump" ]] ||
        { echo "error: no cross objdump in $TOOLS-objdump: install gcc-mingw with toolchain-cpp" >&2; return 1; }
    dlls="$(LC_ALL=C "$TOOLS-objdump" -p "$exe" | awk '/DLL Name:/ { print $3 }' | sort -fu)"
    [[ -n "$dlls" ]] || { echo "error: $exe imports no DLL: is it a Windows executable?" >&2; return 1; }
    while read -r dll; do
        allowed "$dll" || problems+=("$dll")
    done <<<"$dlls"

    if ((${#problems[@]})); then
        echo "error: $exe imports DLLs that are not of Windows:" >&2
        printf '  %s\n' "${problems[@]}" >&2
        return 1
    fi
    echo ">> $exe imports only these DLLs of Windows:"
    # shellcheck disable=SC2001 # indents every line of a multi-line text
    sed 's/^/     /' <<<"$dlls"
}

case "${1:-}" in
    "") [[ $# -eq 0 ]] || usage ;;
    --check) [[ $# -eq 2 ]] || usage; check "$2"; exit ;;
    *) usage ;;
esac

# The components for Windows of toolchain-cpp, by a file each installs
missing=()
for needed in "gcc-$GCC_VERSION-mingw/toolchain.cmake" \
    "Qt/$QT_VERSION-mingw/lib/cmake/Qt6/qt.toolchain.cmake" \
    "openssl-$OPENSSL_VERSION-mingw/lib/libssl.a" \
    "libpq-$POSTGRESQL_VERSION-mingw/lib/libpq.a" \
    "libpqxx-$LIBPQXX_VERSION-mingw/lib/libpqxx.a" \
    "cpp-httplib-$HTTPLIB_VERSION-mingw/include/httplib.h" \
    "protobuf-$PROTOBUF_VERSION-mingw/lib/libprotobuf-lite.a"; do
    [[ -e "$SOFTWARE_DIR/$needed" ]] || missing+=("$SOFTWARE_DIR/$needed")
done
if ((${#missing[@]})); then
    echo "error: the components for Windows of toolchain-cpp are not installed; missing:" >&2
    printf '  %s\n' "${missing[@]}" >&2
    echo "install them with: ../toolchain-cpp/install.sh qt-mingw libpq-mingw libpqxx-mingw" \
         "cpp-httplib-mingw protobuf-mingw" >&2
    exit 1
fi

cd "$ROOT"
cmake --preset windows
cmake --build --preset windows

DIST="$ROOT/build/dist"
mkdir -p "$DIST"
cp build/windows/bin/client.exe "$DIST/client.exe"
"$TOOLS-strip" "$DIST/client.exe"
check "$DIST/client.exe"
echo ">> Self-contained client for Windows in $DIST/client.exe ($(du -h "$DIST/client.exe" | cut -f1))"
