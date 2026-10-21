# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

First version: a proof of concept of the architecture, to be released as 0.1.0.

### Added

- Qt Quick (QML) client that runs in the browser compiled to WebAssembly and
  as a native desktop application from the same code.
- Three run modes: web, desktop connected to a remote server, and desktop
  standalone with the backend embedded in the client process and called in
  process, so that it opens no network port and needs no token.
- C++ backend without framework or ORM: embedded HTTP server (cpp-httplib),
  PostgreSQL access through prepared statements (libpqxx) and the contract's
  binary encoding (protobuf).
- Sample invoicing domain: create and list invoices (customer, optional tax ID
  and total).
- Typed client-backend contract in Protobuf (`src/proto`, package `guita.v1`),
  the single source of the code of both sides, with style and compatibility
  checks (buf).
- RPC API compatible with the Connect protocol: `POST
  /api/guita.v1.InvoiceService/<Method>` in binary Protobuf, with Connect
  errors and gzip-compressed responses; `buf curl` and `protoc
  --encode`/`--decode` write and read its messages by hand.
- Exact money handling: integer cents in C++ and in the API, `numeric(12,2)` in
  PostgreSQL.
- Persistent channel over WebSocket at `/api/channel`: calls to the
  procedures of the contract and server events over one long-lived
  connection, authenticated in its first message, with several calls in
  flight at once. `WS_MAX_CHANNELS` limits the channels open at once (64 by
  default).
- Server events: `InvoiceService.WatchInvoices` announces every invoice
  created, whoever creates it, over the channel and in process.
- Idempotent invoice creation: `CreateInvoice` takes an optional `request_id`
  (a UUID), and a request repeated with the same key returns the invoice
  created the first time instead of creating another.
- Automatic PostgreSQL schema migrations on every start.
- The server also serves the web client.
- Optional shared Bearer token (`API_TOKEN`) for development.
- English and Spanish user interface, following the system or browser language.
- Scripts to install the toolchain and libraries without root privileges, build
  the web client, run the server and create the database.
- README and architecture documentation.
- Automated test suite run with `ctest --preset native`: backend, RPC API,
  client and QML amount logic, with an ephemeral PostgreSQL container started
  and removed by the test run (or an existing server in `GUITA_TEST_DB`), and
  labels to run subsets such as the suites that need no database.
- Measurement programs for message encodings and transport latency, native
  and WebAssembly (`bench` and `wasm-bench` presets), with their results in
  the architecture document.
- Continuous integration on GitHub Actions: every push to a development or
  release branch and every pull request toward one builds the project, runs
  the whole test suite against PostgreSQL 18.6, checks that the contract stays
  compatible with the development branch and that the version in
  `CMakeLists.txt` matches the branch name. A status badge in the README shows
  the result.
- `scripts/setup-qt.sh --desktop-only` installs only the desktop Qt kit, enough
  for the server, the desktop client and the tests.
- `scripts/check-version.sh <branch>` checks the project version against a
  `develop/vX.Y.Z` or `release/vX.Y.Z` branch.
