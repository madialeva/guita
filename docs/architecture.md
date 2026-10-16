# Guita architecture

Technical reference for developers. For a quick overview and how to build and
run the project, see the [README](../README.md).

## 1. Goals

A single C++ code base that produces:

| Mode | Client | Backend | Database |
|---|---|---|---|
| **Web** | Qt/QML compiled to WebAssembly | remote server | server PostgreSQL |
| **Desktop connected** | native Qt/QML | remote server | server PostgreSQL |
| **Desktop standalone** | native Qt/QML | backend embedded in the client process (`127.0.0.1`) | local PostgreSQL (embedded instance planned, see §11) |

Principles:

- The **client always talks HTTP/JSON** to the backend and never knows whether
  it is remote or embedded.
- **All business logic lives in the backend** (plain C++: cpp-httplib, libpqxx,
  glaze). The client only presents data and validates for user experience; the
  backend repeats every critical validation, because the WASM client is in the
  user's hands.
- **Same engine (PostgreSQL), same SQL and same migrations** in every mode.
- The **backend never depends on Qt** and the **client never accesses the database**.
- The backend uses **small libraries, not a framework**: an embedded HTTP
  server, a PostgreSQL client and a JSON library, all with permissive licenses.

## 2. Overview

```
                ┌──────────────────────────────┐
                │  Qt/QML client               │
                │  (native or WASM)            │
                │  ApiConnection + repositories│
                └──────────────┬───────────────┘
                               │ HTTP/JSON + Bearer token
                ┌──────────────▼───────────────┐
                │  backend/api                 │
                │  cpp-httplib server          │
                │  routes + glaze JSON         │
                └──────────────┬───────────────┘
                               │
                ┌──────────────▼───────────────┐
                │  backend/core                │
                │  services → stores (SQL)     │
                │  libpqxx + connection pool   │
                └──────────────┬───────────────┘
                               │
                ┌──────────────▼───────────────┐
                │  PostgreSQL                  │
                └──────────────────────────────┘
```

In standalone mode, `api` + `core` run **inside the desktop client process**,
listening on `127.0.0.1` only.

## 3. Source layout and build targets

```text
src/
├── backend/                 backend, native only (never built for WASM)
│   ├── core/                domain, persistence (libpqxx), migrations, services
│   ├── api/                 HTTP server, REST routes and Backend
│   └── server/              server executable
└── client/                  Qt/QML client, native and WASM
    └── translations/        Qt Linguist catalogs
tests/                       automated tests (§10)
scripts/                     dependency setup, WASM build, server launcher, DB SQL
docs/                        this document
```

| CMake target | Type | Contents | Links |
|---|---|---|---|
| `core` | static | `backend/core` | `libpqxx::pqxx` |
| `api` | static | `backend/api` | `core`; privately `httplib::httplib`, `glaze::glaze` |
| `server` | executable | `backend/server` | `api` |
| `client` | executable + `Guita` QML module | `client` | Qt Quick, Qt Network; `api` on native builds |

Dependencies flow `client → api → core`, and the client reaches the backend
only through `backend/api/Backend.h`, solely for standalone mode. The root
`CMakeLists.txt` adds `src/backend` only when not building for Emscripten.
Project headers are included with angle brackets from `src/`
(`<backend/core/InvoiceService.h>`, `<client/ApiConnection.h>`). The whole
project is C++23. Executables are written to `build/<preset>/bin`.

## 4. Backend

### 4.1 Persistence (`backend/core`)

There is no ORM: SQL is explicit and lives in one place per aggregate.

- **Entities** are plain structs (`Invoice`, `NewInvoice`); nullable columns are
  `std::optional`.
- **Money** is never floating point: amounts are integer cents (`Cents`,
  `std::int64_t`) in C++ and in the JSON API, and `numeric(12,2)` in
  PostgreSQL. The store converts in SQL (`$3::numeric / 100` on write,
  `(total * 100)::bigint` on read), and a `CHECK` constraint backs the
  service's validation.
- **Stores** (`InvoiceStore`) hold all the SQL of an aggregate. A store works
  inside the caller's transaction (`InvoiceStore(tx).insert(...)`) and maps rows
  to structs with `pqxx` tuple conversions.
- **Prepared statements**: every store registers its statements in a static
  `prepare(pqxx::connection &)`, and `prepareStatements()` (`Statements.cpp`)
  calls them all on each new connection. The backend opens one connection at
  startup (`Database::warmUp()`), so a statement that does not match the schema
  fails at startup, not when a user reaches that screen.
- **Connection pool**: `pqxx::connection` is not thread-safe and the HTTP
  server handles requests concurrently. `Database` lends a pooled connection
  for each transaction; broken connections are dropped and reopened on demand.
- **Transactions**: services decide their boundaries with
  `db.inTransaction([&](pqxx::work &tx) { ... })`, which commits when the
  lambda returns and rolls back when it throws.
- **Optimistic locking**: every table has a `version` column. Updates use
  `UPDATE ... SET ..., version = version + 1 WHERE id = $1 AND version = $2`
  and treat zero affected rows as a concurrent modification.
- **Services** (`InvoiceService`) hold the business rules, use stores and
  throw `std::invalid_argument` for validation errors.

### 4.2 Migrations

The schema is managed by numbered SQL migrations in `Migrations.cpp`, applied
at startup by the server and by standalone mode alike, before the statements
are prepared:

- `schema_version` records the applied versions.
- Everything runs in one transaction (PostgreSQL has transactional DDL),
  serialized between instances with `pg_advisory_xact_lock`.
- One statement per migration; a released migration is never edited, a new one
  is added. Table and column names are English `snake_case`; every table has a
  `bigserial` `id` and an integer `version`.

### 4.3 REST API (`backend/api`)

Routes are registered on the cpp-httplib server per resource
(`addInvoiceRoutes()`), and are called **concurrently** from its thread pool,
which is why services must be thread-safe. `Http.h` holds what every route
shares:

- `guarded()` wraps a handler with Bearer authorization and the error mapping.
- `jsonBody<T>()` and `sendJson()` convert between JSON and plain structs with
  glaze's compile-time reflection: the member names are the JSON field names.
  Unknown fields are ignored, so newer clients keep working with older servers.
- `rejectOtherMethods()` answers `405` with an `Allow` header.

| Method | Path | Body | Response |
|---|---|---|---|
| `GET` | `/api/invoices` | — | `200` `[{id, customer, customerTaxId, totalCents}, ...]` |
| `POST` | `/api/invoices` | `{customer, customerTaxId, totalCents}` | `201` `{id, customer, customerTaxId, totalCents}` |

The sample domain is a minimal invoice: a required customer, an optional
customer tax ID and a total in cents, which must be a whole number between 0
and 999 999 999 999. Numbering, dates and invoice lines are not modeled yet.

JSON field names are English `camelCase` and form a public contract once
released. Errors are `{"error": "..."}` with these status codes: `400`
(validation, malformed JSON, with the position of the error), `401`
(authentication), `405` (method), `415` (body not `application/json`), `500`
(unexpected, logged). Error messages are English and not translated yet.

Responses are compressed with gzip when the client accepts it (cpp-httplib is
built with zlib); request bodies are limited to 16 MiB.

### 4.4 Backend and server

`Backend` assembles database, services, routes and the HTTP server, and is
shared by the `server` executable and standalone mode. It is configured with
`BackendOptions` (connection string, authorizer, address, port, docroot, access
log). Its constructor applies the migrations, prepares the statements and
registers the routes, and throws on failure; `start()` binds the port and runs
the server on its own thread pool without blocking. With port `0` the OS picks
a free port, returned by `port()`.

`Backend.h` includes neither cpp-httplib nor libpqxx (pimpl), so the client
only sees that small interface.

With a docroot, the same server serves the WASM files (`.wasm` as
`application/wasm`). The `server` executable blocks `SIGINT` and `SIGTERM`
before any thread starts and waits for them with `sigwait()` to stop cleanly.
It is configured through environment variables:

| Variable | Default | Purpose |
|---|---|---|
| `DB_CONN` | `host=127.0.0.1 dbname=guita user=guita password=guita` | libpq connection string |
| `HTTP_ADDRESS` | `0.0.0.0` | listen address |
| `HTTP_PORT` | `8090` | port |
| `DOCROOT` | `./web` | static files (WASM build) |
| `API_TOKEN` | empty | required Bearer token; empty accepts everything (development only) |

cpp-httplib also supports WebSockets (`Server::WebSocket()`), the planned
channel for real-time notifications.

## 5. Client

### 5.1 API connection and repositories

`ApiConnection` is a `QML_SINGLETON` holding the base URL, the token, the
`QNetworkAccessManager` and the mode (`Web`, `Connected`, `Standalone`).
`main.cpp` calls `ApiConnection::configure()` before loading the QML, and QML
gets the instance through the static `create()`; the class is not
default-constructible on purpose (Qt prefers a default constructor over `create()`).

Repositories (`InvoiceRepository`) are `QML_ELEMENT`s instantiated by the view
that uses them, with a typed `REQUIRED` `connection` property. They expose
`Q_INVOKABLE` operations and report results through signals.

**Everything is asynchronous** (signals and callbacks): WebAssembly forbids
blocking waits and nested `QEventLoop::exec()`.

### 5.2 Views

Views live in the `Guita` QML module. C++ types reach QML only by declarative
registration (`QML_ELEMENT`, `QML_SINGLETON`), never through context
properties, `qmlRegister*` calls or objects injected from `main.cpp`. Views use
typed properties so `qmllint` checks every access.

### 5.3 Platform-specific pieces

Only two pieces differ between native and WASM:

- `ApiConfig.h`: in WASM the API base URL is the page origin + `/api/` (same
  origin, so no CORS); natively it is `API_URL` (default `http://127.0.0.1:8090/api/`).
- `main.cpp`: standalone support (`APP_STANDALONE_SUPPORT`, defined only on
  native builds) starts the embedded backend.

### 5.4 Internationalization

English is the source language: `qsTr()` in QML, `tr()` or
`QCoreApplication::translate()` in C++. Languages are declared in
`qt_standard_project_setup` and the catalogs (`src/client/translations/guita_*.ts`)
are embedded under `:/i18n`; `main.cpp` loads the one matching the system
locale (the browser locale in WASM). Refresh the catalogs with the
`update_translations` target.

## 6. Standalone mode

`client --standalone [--db <conn>]`:

1. Generates a random 32-byte token for this launch.
2. Creates `Backend` on `127.0.0.1`, port `0`, without docroot, and an
   authorizer that accepts only that token.
3. Configures `ApiConnection` with `http://127.0.0.1:<port>/api/` and the token.

The backend runs on the HTTP server threads, so it never blocks the UI and
needs no `QThread`. `Backend` is declared before the QML engine in `main()`, so
it is destroyed after the views are gone.

## 7. Authentication

Authentication is pluggable rather than tied to an identity provider:

- **Backend**: an `Authorizer` (`backend/api/Authorizer.h`) receives the
  `Authorization` header of every request and decides whether to accept it.
  `tokenAuthorizer()` implements the current shared-token check; validating
  tokens from any identity provider (for example JWTs from an OIDC server) is
  just another `Authorizer`, with no changes elsewhere.
- **Client**: `ApiConnection` sends the Bearer token it is configured with;
  whatever obtains the token only has to hand it over.

| Mode | Current |
|---|---|
| Web | none: the server runs without `API_TOKEN` (development only) |
| Desktop connected | shared `API_TOKEN` |
| Desktop standalone | random 32-byte token per launch, loopback only |

The shared `API_TOKEN` is a development aid, not user authentication. User
authentication owned by the backend (users in PostgreSQL, Argon2 password
hashes, session tokens issued by the backend) is pending (§11): it keeps every
mode self-contained, without external services.

## 8. Toolchain

| Component | Version | Source |
|---|---|---|
| GCC | C++23 (13 or later) | system |
| CMake | 3.21+ (3.31+ to install glaze) | system |
| Qt (desktop + `wasm_singlethread`) | 6.12.0 LTS | `scripts/setup-qt.sh` (aqtinstall) |
| Emscripten | 5.0.5 | `scripts/setup-qt.sh` (emsdk) |
| libpqxx | 8.0.2 (requires C++20) | `scripts/build-deps.sh` (static, PIC) |
| cpp-httplib | 0.61.0 | `scripts/build-deps.sh` (header-only, with zlib) |
| glaze | 9.0.0 (requires C++23) | `scripts/build-deps.sh` (header-only) |
| libpq, zlib | system | system development packages |
| PostgreSQL | 17 tested | system |

- The scripts install the non-system dependencies (Qt, emsdk, libpqxx,
  cpp-httplib, glaze) without root privileges under `$SOFTWARE_DIR` (default
  `~/software`), which is where the CMake presets look for them; with other
  locations, pass `CMAKE_PREFIX_PATH` or use a `CMakeUserPresets.json`.
- The Qt WASM kit requires the desktop kit of the **same version** as host
  (`QT_HOST_PATH`) and a specific Emscripten release
  (`QT_EMCC_RECOMMENDED_VERSION` in `QtPublicWasmToolchainHelpers.cmake`):
  upgrade Qt and emsdk together.
- Emscripten needs Python ≥ 3.10 (`EMSDK_PYTHON`, set by the `wasm` preset and
  `scripts/build-wasm.sh`; `emsdk_env.sh` clears it).
- Presets: `native` (Debug, `build/native`) and `wasm` (Release, `build/wasm`).
  `scripts/build-wasm.sh` also copies the web files to `build/web`, the docroot
  used by `scripts/run-server.sh`.

## 9. Deployment

### 9.1 Server

```
/opt/guita/
├─ server            executable
└─ web/              WASM build: client.html (also as index.html), client.js,
                     client.wasm, qtloader.js
```

The same server serves the static files and the API. A reverse proxy in front
terminates TLS (and can speak HTTP/2 to browsers), for example HAProxy:

```
frontend https
    bind :443 ssl crt /etc/haproxy/certs/guita.pem
    default_backend guita

backend guita
    server guita1 127.0.0.1:8090 check
```

COOP/COEP headers (`Cross-Origin-Opener-Policy: same-origin`,
`Cross-Origin-Embedder-Policy: require-corp`) are needed only if the client is
built with the multithreaded WASM kit; the project uses `wasm_singlethread`.
Everything is same-origin, so no CORS is needed.

A `manifest.json` plus a service worker caching `client.html`, `client.js`,
`client.wasm` and `qtloader.js` would make the web client installable as a PWA
and speed up later launches (the API still needs a connection).

### 9.2 Desktop

Not packaged yet. The package will ship the executable with the Qt 6.12
libraries (RUNPATH `$ORIGIN/lib`) and the Qt plugins and QML modules it uses;
the backend libraries are linked statically or are header-only, and libpq
comes from the system or the package.

## 10. Testing

### 10.1 Framework and layout

All tests use Qt Test, and Qt Quick Test for QML logic, and are registered in
CTest; they are native only and run headless (`QT_QPA_PLATFORM=offscreen`).
The backend tests link Qt only in their own executables: `core`, `api` and
`server` stay Qt-free.

- `tests/CMakeLists.txt` defines `guita_add_test(<name> LABELS ... [LIBS ...]
  [SOURCES ...] [DATABASE])`, which builds `test_<name>.cpp` against the layer
  under test. Helpers live in `tests/support/` and are included as
  `<support/...>`.
- Every suite has a label saying what it needs: `unit` (nothing), `db`
  (PostgreSQL), `api` (in-process backend and HTTP client), `client` (client
  classes against an embedded backend), `qml` (Qt Quick Test).
- `ctest --preset native` runs everything; `-L <label>` and `-LE <label>`
  select subsets; `-j` runs suites in parallel.

### 10.2 Ephemeral PostgreSQL

The database suites run against a real PostgreSQL, the only safety net of
explicit SQL. There is no mature Testcontainers for C++, so the same idea is
expressed with CTest fixtures:

- `postgres_start` (`FIXTURES_SETUP`) runs `tests/postgres-fixture.sh start`:
  without `GUITA_TEST_DB` it starts an ephemeral `postgres:18.6` container
  (`GUITA_TEST_PG_IMAGE` overrides the image) on a random loopback port with a
  random password, waits until it accepts connections and writes the
  connection string to a file in the build directory (`GUITA_TEST_DB_FILE`).
  With `GUITA_TEST_DB` set it only writes that string.
- `postgres_stop` (`FIXTURES_CLEANUP`) removes the container, also when suites
  fail. Suites declared with `DATABASE` require the fixture, so the container
  starts once per run and only when a selected suite needs it.
- Without Docker or `GUITA_TEST_DB`, `postgres_start` fails with instructions
  and the database suites are reported as not run (failed), never skipped.

`TestDatabase` (`tests/support/`) gives each suite a database of its own,
`guita_test_<suite>_<random>`, created empty from `template0` and dropped with
`WITH (FORCE)` on destruction. Suites apply the migrations themselves, run in
parallel without interfering, and leave nothing behind. No test ever uses the
application's default connection string.

### 10.3 What is tested

| Suite | Label | Covers |
|---|---|---|
| `testdatabase` | `db` | the isolation of `TestDatabase` itself |
| `invoiceservice` | `unit` | invoice validation (customer, negative and too large totals) without connecting to the database |
| `authorizer` | `unit` | `tokenAuthorizer`: right, wrong, missing and malformed tokens; an empty token accepts everything |

### 10.4 Continuous integration

A GitHub Actions job will declare a `postgres:18.6` service container with a
health check and export `GUITA_TEST_DB` with its connection string, so the
fixture starts nothing; the rest is `cmake --preset native`, `cmake --build
--preset native` and `ctest --preset native`. The same suites run locally and
in CI; only the provider of the server changes.

## 11. Pending work

- **Embedded PostgreSQL** for standalone mode: ship the PostgreSQL binaries
  (`initdb`, `pg_ctl`, `postgres`, `createdb`, `lib/`, `share/`) next to the
  executable (EDB ZIP on Windows, *zonky embedded-postgres-binaries* on
  Linux/macOS, ~30–60 MB trimmed) and manage them as a child process: `initdb`
  with `scram-sha-256` and a random password stored with owner-only
  permissions on first launch, `pg_ctl start` on a free loopback port, and
  recovery of an orphaned instance after a crash. It does not run as root on
  Linux; the first `initdb` takes a few seconds, later starts about one.
- **Typed contract with Protobuf** and RPC over HTTP between client and backend.
- **User authentication** owned by the backend (§7).
- **Continuous integration** on GitHub Actions (§10.4).
- **Desktop packaging** (§9.2).
- **Translated backend error messages**.

## 12. Considerations

- **Licenses:** the project is MIT. The backend libraries are permissive
  (cpp-httplib and glaze MIT, libpqxx BSD-3-Clause, libpq PostgreSQL License).
  The Qt modules the client uses (Core, Gui, Qml, Quick, Quick Controls,
  Network, and Protobuf if adopted) are LGPLv3, GPL or commercial. Using Qt
  under the LGPL is compatible with MIT and with selling the product; the
  obligations apply when builds are distributed, paid or free:
  - **Desktop**: link Qt dynamically (as the build does) so users can replace
    the Qt libraries, ship the LGPL text and Qt's notices, and offer Qt's
    source code.
  - **Web**: `client.wasm` links Qt statically, and serving it to browsers is
    distribution. The LGPL then requires letting users relink the client with
    a modified Qt (for example by publishing the client's sources or object
    files); with this project's sources public under MIT, that is covered.
  - Avoid Qt modules available only under the GPL or commercially, which would
    impose the GPL on distributed builds.
- **Concurrency:** the HTTP server handles requests in parallel on a thread
  pool (one thread per connection, also for WebSockets), which suits business
  applications but not tens of thousands of simultaneous connections. Services
  stay thread-safe through the connection pool and one transaction per call.
- **Standalone security:** loopback only, random port and per-launch random token.
