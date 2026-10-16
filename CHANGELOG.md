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
  standalone with the backend embedded in the client process on `127.0.0.1`,
  with a random port and a per-launch token.
- C++ backend without framework or ORM: embedded HTTP server (cpp-httplib),
  PostgreSQL access through prepared statements (libpqxx) and JSON with
  compile-time reflection (glaze).
- Sample invoicing domain: create and list invoices (customer, optional tax ID
  and total) through `GET /api/invoices` and `POST /api/invoices`, with JSON
  errors and gzip-compressed responses.
- Exact money handling: integer cents in C++ and JSON, `numeric(12,2)` in
  PostgreSQL.
- Automatic PostgreSQL schema migrations on every start.
- The server also serves the web client.
- Optional shared Bearer token (`API_TOKEN`) for development.
- English and Spanish user interface, following the system or browser language.
- Scripts to install the toolchain and libraries without root privileges, build
  the web client, run the server and create the database.
- README and architecture documentation.
- Automated test suite run with `ctest --preset native`: backend, REST API,
  client and QML amount logic, with an ephemeral PostgreSQL container started
  and removed by the test run (or an existing server in `GUITA_TEST_DB`), and
  labels to run subsets such as the suites that need no database.
