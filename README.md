<h1 align="center">Guita</h1>

<p align="center">
  <a href="LICENSE.md"><img src="https://img.shields.io/badge/license-MIT-green.svg" alt="License" /></a>
  <a href="#"><img src="https://img.shields.io/badge/c%2B%2B-23-00599C?logo=cplusplus&logoColor=white" alt="C++23" /></a>
  <a href="#"><img src="https://img.shields.io/badge/Qt-6.12_LTS-41CD52?logo=qt&logoColor=white" alt="Qt 6.12 LTS" /></a>
  <a href="#"><img src="https://img.shields.io/badge/WebAssembly-654FF0?logo=webassembly&logoColor=white" alt="WebAssembly" /></a>
  <a href="#"><img src="https://img.shields.io/badge/PostgreSQL-4169E1?logo=postgresql&logoColor=white" alt="PostgreSQL" /></a>
  <a href="#"><img src="https://img.shields.io/badge/CMake-3.21-064F8C?logo=cmake&logoColor=white" alt="CMake 3.21" /></a>
  <a href="#"><img src="https://img.shields.io/badge/backend-cpp--httplib_%7C_libpqxx_%7C_Protobuf-555555" alt="cpp-httplib, libpqxx, Protobuf" /></a>
</p>

<p align="left">
  Guita explores a C++-only way of building applications: a single code base
  whose <strong>Qt Quick (QML)</strong> client runs in the browser compiled to
  <strong>WebAssembly</strong> and, unchanged, as a native desktop application,
  backed by a lean C++ server over <strong>PostgreSQL</strong> built from small
  libraries instead of a framework (<strong>cpp-httplib</strong>,
  <strong>libpqxx</strong>, <strong>Protobuf</strong>). On the desktop the backend can also be embedded in
  the client process, so the same program runs fully standalone — the kind of
  experience Electron gives JavaScript applications, without shipping a browser.
</p>

---

## Status

Proof of concept (v0.1.0). See [CHANGELOG.md](CHANGELOG.md) for what each version adds.

| Status | Feature |
|:---:|---|
| ✅ | Sample invoicing domain: create and list invoices, with exact money handling |
| ✅ | Typed client–backend contract with Protobuf, served as RPC (Connect protocol) |
| ✅ | Backend without framework or ORM (cpp-httplib, libpqxx) |
| ✅ | Automatic schema migrations on PostgreSQL |
| ✅ | Web client (Qt Quick compiled to WebAssembly) served by the backend |
| ✅ | Native desktop client connected to a remote backend |
| ✅ | Standalone desktop mode with the backend embedded in the client |
| ✅ | English and Spanish user interface |
| ⬜ | Embedded PostgreSQL for standalone mode |
| ⬜ | User authentication |
| ⬜ | Desktop packages |
| ✅ | Automated tests with an ephemeral PostgreSQL |
| ⬜ | Continuous integration |

## Branching and versioning

The default branch is `develop/vX.Y.Z` (currently `develop/v0.1.0`), the
version under development; there is no `main`/`master`. Releasing cuts
`release/vX.Y.Z` and tags `vX.Y.Z`, and hotfixes bump the patch digit. The
program version is declared once in `CMakeLists.txt` and matches the branch
name. Feature work happens on short-lived `change/is<n>-<slug>` branches that
PR into `develop/vX.Y.Z`, each tracked by an issue and an OpenSpec change.

## How it works

| Mode | Client | Backend |
|---|---|---|
| Web | Qt/QML in the browser (WASM) | server, which also serves the web client |
| Desktop connected | native Qt/QML | remote server |
| Desktop standalone | native Qt/QML | backend embedded in the client process, called in process (no network) |

The client always talks to the backend through a typed Protobuf contract and
never knows which transport carries the calls: HTTP (RPC compatible with the
Connect protocol) in the web and connected modes, a direct in-process call in
standalone mode, which therefore opens no network port. All business logic
lives in the backend. See
[docs/architecture.md](docs/architecture.md) for the technical details.

## Requirements

### To build

| Software | Version | Needed for |
|---|---|---|
| GCC | C++23 (13 or later) | everything |
| CMake and Ninja | CMake 3.21+ | everything |
| Qt, desktop kit (`gcc_64`) | 6.12 | native client; host tools of the WASM build |
| Qt, WebAssembly kit (`wasm_singlethread`) | 6.12 | web client |
| Emscripten (emsdk) | 5.0.5, the release Qt 6.12 requires | web client |
| Python | 3.10+ | Emscripten |
| libpqxx | 8.0 | server and standalone client |
| cpp-httplib (header-only, built with zlib) | 0.61 | server and standalone client |
| protobuf (C++ library and `protoc`), also built for WebAssembly | 36.2 | the contract, both sides |
| buf | 1.74 | contract style and compatibility checks |
| libpq and zlib development files | — | libpqxx, cpp-httplib |

Both Qt kits need Qt Quick, Qt Quick Controls, Qt Network, the Linguist tools
and the `qttasktree` module.

### To run

- **Server**: the libpq and zlib libraries, and a reachable PostgreSQL server
  (tested with PostgreSQL 17).
- **Desktop client**: the Qt 6.12 libraries; in standalone mode, libpq, zlib
  and a reachable PostgreSQL server as well.
- **Web client**: any current browser with WebAssembly and WebGL support.

### To test

- **Docker**, so that the database tests can start an ephemeral PostgreSQL
  container, or a PostgreSQL server of your own given in `GUITA_TEST_DB`
  (see [Testing](#testing)).

## Installing the dependencies

If something above is missing, install it before building.

**System packages** — compiler, build tools and the libraries the backend depends on:

```sh
# Fedora, RHEL and derivatives
sudo dnf install gcc-c++ cmake ninja-build git zlib-devel libpq-devel postgresql-server

# Debian and Ubuntu
sudo apt install build-essential cmake ninja-build git zlib1g-dev libpq-dev postgresql
```

**Qt, Emscripten and the backend libraries** — install them with your usual
tools (Qt online installer, emsdk, the libraries from source), or let the
repository scripts do it without root privileges under `$SOFTWARE_DIR`
(default `~/software`):

```sh
scripts/setup-qt.sh   # Qt 6.12 desktop + WASM kits (aqtinstall), emsdk 5.0.5, Python 3.12 via uv
scripts/build-deps.sh # libpqxx, cpp-httplib and protobuf (native and WASM) from source, and buf
```

Run them in this order: `build-deps.sh` builds protobuf for the web client with
the emsdk that `setup-qt.sh` installs (without it, that part is skipped with a
warning).

`setup-qt.sh` needs [uv](https://docs.astral.sh/uv/) to provide the Python
that Emscripten requires when the system one is older than 3.10. The CMake
presets look for Qt and the libraries where these scripts install them; when
they live elsewhere, pass `-DCMAKE_PREFIX_PATH="<qt-prefix>;<libpqxx-prefix>;..."` to the native
build, or add a `CMakeUserPresets.json` (for the web build it also sets the Qt
WASM toolchain file, `QT_HOST_PATH` and `EMSDK`).

## Database

Create the user and the database once:

```sh
sudo -u postgres psql -f scripts/create-db.sql
```

The tables are created and kept up to date automatically by the backend
migrations on every start. The default connection string is
`host=127.0.0.1 dbname=guita user=guita password=guita`; override it with
`DB_CONN` (server and standalone client) or `--db` (standalone client).

## Building

```sh
# Native: server and desktop client (build/native/bin)
cmake --preset native
cmake --build --preset native

# Web client (build/web)
scripts/build-wasm.sh
```

## Running

```sh
# Server: API and web client → http://localhost:8090/
scripts/run-server.sh

# Desktop client connected to the server (API_URL, default http://127.0.0.1:8090/api/)
build/native/bin/client

# Desktop client in standalone mode: no server needed
build/native/bin/client --standalone
```

The API speaks binary Protobuf. To call it by hand, `buf curl --schema
src/proto --data '{}' http://127.0.0.1:8090/api/guita.v1.InvoiceService/ListInvoices`
takes the request in JSON and prints the response in JSON, and `protoc
--encode`/`--decode` convert between Protobuf's text format and binary (see
[docs/architecture.md](docs/architecture.md), §4.3).

The server reads `DB_CONN`, `HTTP_ADDRESS`, `HTTP_PORT`, `DOCROOT` and
`API_TOKEN` from the environment. Until user authentication exists, `API_TOKEN`
is an optional shared Bearer token for development (leave it empty for the web
client); the standalone client needs none, since it opens no network port.

## Testing

The automated tests (Qt Test and Qt Quick Test, run by CTest) cover the
backend, the RPC API and the client. They build with the native preset and
run headless:

```sh
ctest --preset native              # every suite
ctest --preset native -L unit      # only the suites that need no database
ctest --preset native -j8          # in parallel
```

Every suite is labeled by what it needs: `unit`, `db`, `api`, `client` or
`qml`. The database suites never touch your development database: CTest starts
an ephemeral `postgres:18.6` container on a random port before them and removes
it afterwards, and every suite works in a database of its own. To use an
existing server instead (as the CI does), give it a key=value libpq connection
string with permission to create databases:

```sh
GUITA_TEST_DB="host=127.0.0.1 port=5432 user=postgres password=secret dbname=postgres" ctest --preset native
```

## Internationalization

English is the source language and the interface follows the system language
(the browser language on the web), with a Spanish translation. After changing
interface texts, refresh the catalogs in `src/client/translations/`:

```sh
cmake --build --preset native --target update_translations
```

## Project layout

```text
src/proto/     client-backend contract (Protobuf)
src/backend/   backend: core (domain, persistence), api (RPC router, HTTP), server
src/client/    Qt/QML client, native and WebAssembly
tests/         automated tests (Qt Test, CTest)
scripts/       dependency setup, web build, server launcher, database SQL
docs/          technical documentation
openspec/      specs and change proposals: how the project is developed with OpenSpec
```

## License

Released under the **MIT License**. You are free to use, modify, and distribute
this software, including in proprietary projects, provided you retain the
original copyright notice. See [LICENSE.md](LICENSE.md) for details.

Third-party components keep their own licenses: the backend libraries are
permissive (cpp-httplib MIT, libpqxx and protobuf BSD, abseil Apache 2.0, libpq
PostgreSQL License),
and the Qt modules the client uses are available under the LGPLv3 (or GPL, or
a commercial license). Distributing builds of the client therefore carries the
LGPL obligations for Qt.
