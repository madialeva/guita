#!/usr/bin/env bash
# Checks that the contract (src/proto) stays compatible with the one on the
# development branch: buf breaking with the rules of src/proto/buf.yaml
# (FILE: no reused, renumbered or retyped fields, no removals without
# reserving them, ...).
#
#   scripts/check-contract.sh [<git ref>]
#
# The default ref is the development branch of the version in CMakeLists.txt,
# origin/develop/vX.Y.Z when the remote branch exists, develop/vX.Y.Z otherwise.
# When that ref has no contract yet, there is nothing to break.
# buf: $BUF, the buf on PATH, or the one installed by scripts/build-deps.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

buf="${BUF:-$(command -v buf || ls -d "${SOFTWARE_DIR:-$HOME/software}"/buf-*/bin/buf 2>/dev/null | sort -V | tail -1)}"
if [[ -z "$buf" || ! -x "$buf" ]]; then
    echo "error: buf not found; install it with scripts/build-deps.sh or set BUF" >&2
    exit 1
fi

if [[ $# -ge 1 ]]; then
    ref="$1"
else
    version="$(sed -n 's/^project([A-Za-z]* VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
    ref="develop/v$version"
    git rev-parse --verify --quiet "origin/$ref" > /dev/null && ref="origin/$ref"
fi

if ! git rev-parse --verify --quiet "$ref^{commit}" > /dev/null; then
    echo "error: unknown git ref $ref" >&2
    exit 1
fi
if ! git cat-file -e "$ref:src/proto/buf.yaml" 2> /dev/null; then
    echo "No contract on $ref yet: nothing to compare"
    exit 0
fi

echo "Checking src/proto against $ref"
"$buf" breaking src/proto --against ".git#ref=$(git rev-parse "$ref"),subdir=src/proto"
echo "The contract is compatible with $ref"
