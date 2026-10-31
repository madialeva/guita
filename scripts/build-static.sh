#!/usr/bin/env bash
# Builds the self-contained desktop client (docs/architecture.md §9.2): the client of
# the static preset (CMakePresets.json), with Qt, libpq and the C++ runtime linked
# into the executable, which is copied without symbols to build/dist/client. Then
# checks that the executable needs only libraries of the operating system: it fails,
# naming them, if it carries a run path, or needs a shared library of Qt, the C++
# runtime, PostgreSQL or protobuf, or one from the local dependencies or this
# repository.
#
#   scripts/build-static.sh                       build, copy and check the client
#   scripts/build-static.sh --check <executable>  only check <executable>
#
# On demand only: the native build, the tests and the CI do not need it. The static
# Qt and libpq come from scripts/build-static-deps.sh, which runs first.
#
# Variables:
#   SOFTWARE_DIR         root of the local dependencies  [~/software]
#   QT_VERSION           static Qt release               [6.12.0]
#   POSTGRESQL_VERSION   release of the static libpq     [18.6]
set -euo pipefail

SOFTWARE_DIR="${SOFTWARE_DIR:-$HOME/software}"
QT_VERSION="${QT_VERSION:-6.12.0}"
POSTGRESQL_VERSION="${POSTGRESQL_VERSION:-18.6}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

usage() {
    echo "usage: $0 [--check <executable>]" >&2
    exit 2
}

# check <executable>: the run path and the shared libraries it needs, directly
# (readelf) and once resolved (ldd)
check() {
    local exe="$1" problems=() dynamic needed lib line path
    dynamic="$(LC_ALL=C readelf -d "$exe")"
    while read -r line; do
        [[ -n "$line" ]] && problems+=("$line")
    done < <(sed -n -E 's/.*\((RUNPATH|RPATH)\).*\[(.*)\]/\1 \2/p' <<<"$dynamic")
    needed="$(sed -n -E 's/.*\(NEEDED\).*\[(.*)\]/\1/p' <<<"$dynamic")"
    while read -r lib; do
        [[ "$lib" =~ ^lib(Qt6|icu|stdc\+\+|gcc_s|pq\.|protobuf|absl) ]] && problems+=("needs $lib")
    done <<<"$needed"
    while read -r line; do
        lib="${line%% *}"
        if [[ "$line" == *"=> not found"* ]]; then
            problems+=("$lib not found")
        elif [[ "$line" == *" => /"* ]]; then
            path="${line#* => }"
            path="${path%% (*}"
            [[ "$path" == "$SOFTWARE_DIR"/* || "$path" == "$ROOT"/* ]] && problems+=("$lib from $path")
        fi
    done < <(LC_ALL=C ldd "$exe" | sed 's/^[[:space:]]*//')

    if ((${#problems[@]})); then
        echo "error: $exe is not self-contained:" >&2
        printf '  %s\n' "${problems[@]}" >&2
        return 1
    fi
    echo ">> $exe needs only these libraries of the system:"
    sed 's/^/     /' <<<"$needed"
}

case "${1:-}" in
    "") [[ $# -eq 0 ]] || usage ;;
    --check) [[ $# -eq 2 ]] || usage; check "$2"; exit ;;
    *) usage ;;
esac

qt_dir="$SOFTWARE_DIR/Qt/$QT_VERSION-static"
libpq_dir="$SOFTWARE_DIR/libpq-$POSTGRESQL_VERSION-static"
if [[ ! -d "$qt_dir/lib/cmake/Qt6LinguistTools" || ! -f "$libpq_dir/lib/libpq.a" ]]; then
    echo "error: no static Qt in $qt_dir or no static libpq in $libpq_dir: run" \
         "scripts/build-static-deps.sh first" >&2
    exit 1
fi

cd "$ROOT"
cmake --preset static
cmake --build --preset static

DIST="$ROOT/build/dist"
mkdir -p "$DIST"
cp build/static/bin/client "$DIST/client"
strip "$DIST/client"
check "$DIST/client"
echo ">> Self-contained client in $DIST/client ($(du -h "$DIST/client" | cut -f1))"
