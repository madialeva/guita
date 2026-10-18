#!/usr/bin/env bash
# Checks that the program version in CMakeLists.txt (project(... VERSION ...))
# matches the version of a development or release branch:
#
#   scripts/check-version.sh <branch>
#
# develop/vX.Y.Z and release/vX.Y.Z must carry version X.Y.Z; any other branch
# (a change branch, for example) has no version to compare and passes. The CI
# passes the branch it pushes to or the base branch of the pull request.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
branch="${1:?usage: $0 <branch>}"

version="$(sed -n 's/^project([A-Za-z]* VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
if [[ -z "$version" ]]; then
    echo "error: no project version found in CMakeLists.txt" >&2
    exit 1
fi

if [[ ! "$branch" =~ ^(develop|release)/v([0-9]+\.[0-9]+\.[0-9]+)$ ]]; then
    echo "Branch $branch carries no version: nothing to compare (CMakeLists.txt has $version)"
    exit 0
fi

expected="${BASH_REMATCH[2]}"
if [[ "$version" != "$expected" ]]; then
    echo "error: CMakeLists.txt has version $version, but branch $branch is version $expected" >&2
    exit 1
fi
echo "CMakeLists.txt version $version matches branch $branch"
