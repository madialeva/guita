#!/usr/bin/env bash
# Starts the server (API + static files of the WASM build in build/web).
# Variables with defaults: DB_CONN, HTTP_ADDRESS, HTTP_PORT, DOCROOT, WS_MAX_CHANNELS, API_TOKEN.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

export DB_CONN="${DB_CONN:-host=127.0.0.1 dbname=guita user=guita password=guita}"
export HTTP_PORT="${HTTP_PORT:-8090}"
export DOCROOT="${DOCROOT:-$ROOT/build/web}"
mkdir -p "$DOCROOT"

exec "$ROOT/build/native/bin/server"
