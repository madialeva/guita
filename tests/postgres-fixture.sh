#!/usr/bin/env bash
# CTest fixture that provides the PostgreSQL server of the database tests.
#
#   postgres-fixture.sh start <conn-file>   writes a libpq connection string to <conn-file>
#   postgres-fixture.sh stop  <conn-file>   removes what start created
#
# With GUITA_TEST_DB set (a key=value libpq connection string to a server where
# the tests may create and drop databases, as in CI), that server is used and
# nothing is started. Otherwise an ephemeral container is started, Testcontainers
# style: image GUITA_TEST_PG_IMAGE (default postgres:18.6), random loopback port
# and random password, removed by `stop`. The tests never use the application's
# default connection string.
set -euo pipefail

action="${1:?usage: $0 start|stop <conn-file>}"
conn_file="${2:?usage: $0 start|stop <conn-file>}"
container_file="$conn_file.container"
image="${GUITA_TEST_PG_IMAGE:-postgres:18.6}"

# One container per build directory: a rerun replaces its own leftover from an
# interrupted run without touching other build directories
container="guita-test-pg-$(printf '%s' "$conn_file" | md5sum | cut -c1-12)"

start() {
    rm -f "$conn_file" "$container_file"

    if [[ -n "${GUITA_TEST_DB:-}" ]]; then
        printf '%s\n' "$GUITA_TEST_DB" > "$conn_file"
        echo "Database tests use the server in GUITA_TEST_DB"
        return
    fi

    if ! command -v docker > /dev/null || ! docker info > /dev/null 2>&1; then
        cat >&2 <<'EOF'
error: the database tests need a PostgreSQL server. Either
  - install Docker (and let your user run it): an ephemeral PostgreSQL
    container is started and removed for every test run, or
  - set GUITA_TEST_DB to a key=value libpq connection string of a server where
    the tests may create and drop databases, for example
      GUITA_TEST_DB="host=127.0.0.1 port=5432 user=postgres password=secret dbname=postgres"
The suites without database still run: ctest --preset native -L unit
EOF
        exit 1
    fi

    docker rm -f "$container" > /dev/null 2>&1 || true
    local password
    password="$(head -c 24 /dev/urandom | base64 | tr -dc 'A-Za-z0-9')"
    docker run --detach --rm --name "$container" \
        --env POSTGRES_PASSWORD="$password" \
        --publish 127.0.0.1::5432 \
        "$image" > /dev/null
    printf '%s\n' "$container" > "$container_file"

    # The image initializes the cluster on a Unix socket first and only listens
    # on TCP once it is ready, so a TCP check is reliable
    local ready=0
    for _ in $(seq 1 120); do
        if docker exec "$container" pg_isready --quiet --host 127.0.0.1 --username postgres; then
            ready=1
            break
        fi
        sleep 0.5
    done
    if [[ "$ready" != 1 ]]; then
        docker logs "$container" >&2 || true
        echo "error: PostgreSQL container $container did not become ready" >&2
        exit 1
    fi

    local port
    port="$(docker port "$container" 5432/tcp | head -1 | sed 's/.*://')"
    (umask 077; printf 'host=127.0.0.1 port=%s user=postgres password=%s dbname=postgres\n' \
        "$port" "$password" > "$conn_file")
    echo "Database tests use ephemeral container $container ($image) on port $port"
}

stop() {
    if [[ -f "$container_file" ]]; then
        docker rm -f "$(cat "$container_file")" > /dev/null 2>&1 || true
        echo "Removed container $(cat "$container_file")"
    fi
    rm -f "$conn_file" "$container_file"
}

case "$action" in
    start) start ;;
    stop)  stop ;;
    *)     echo "usage: $0 start|stop <conn-file>" >&2; exit 2 ;;
esac
