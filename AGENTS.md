# AGENTS.md — Guita

## Project

Guita is a proof of concept for a C++-only architecture where a single code
base produces a Qt Quick (QML) client that runs both as a WebAssembly web app
and as a native desktop application, talking to a C++ backend over
PostgreSQL built from small libraries instead of a framework: cpp-httplib
(embedded HTTP server), libpqxx (PostgreSQL, no ORM) and protobuf (the
contract, served as RPC). The technical design is described in
`docs/architecture.md`; consult it before changing how the pieces fit together
and keep it up to date when they change.

| Mode | Client | Backend |
|---|---|---|
| Web | Qt/QML compiled to WebAssembly | remote `server`, which also serves the WASM files |
| Desktop connected | native Qt/QML | remote `server` (`API_URL`) |
| Desktop standalone | native Qt/QML (`--standalone`) | backend embedded in the client process, called in process: no network, no port, no token |

Principles that every change must keep:

- The client always talks to the backend through the RPC contract and never
  knows which transport carries the calls: HTTP in the web and connected modes,
  in process in standalone mode. No shortcut skips the contract.
- All business logic lives in the backend (`src/backend/`); the client only
  presents and validates for user experience. The backend repeats every
  critical validation: the WASM client is in the user's hands.
- The backend never depends on Qt and the client never accesses the database.
- Same engine (PostgreSQL), same SQL and same migrations in every mode.

Use C++23, Qt Quick (QML), cpp-httplib, libpqxx 8, Protobuf, PostgreSQL, CMake,
Ninja and GCC. The backend uses no framework and no ORM. Qt is
6.12 LTS everywhere (desktop kit for the native client, `wasm_singlethread` kit
for the web client, both from `~/software/Qt`; CMake requires 6.12): never add
code paths or workarounds for other Qt versions, and never build against the
system Qt. Do not introduce Qt Widgets or new production dependencies without
an approved OpenSpec change.

## Source layout

The repository root holds only project files (CMake, presets, AGENTS.md,
README), `scripts/`, `docs/`, `tests/`, `openspec/` (versioned and public, see Working
preferences), and the git-ignored `build/` and `tmp/` (the user's local
working material: never publish it nor reference it from published files). All
source code lives in `src/`, grouped by side of the architecture:

```text
src/
├── proto/            client-backend contract (Protobuf, package guita.v1, buf module)
├── backend/          backend, native only (never built for WASM)
│   ├── core/         plain C++ + libpqxx: entities, stores (SQL), pool, migrations, services
│   ├── api/          RPC router, its transports and Backend, shared by server and client
│   └── server/       server executable
└── client/           Qt/QML client (native + WASM): API connection, repositories, views
    └── translations/ Qt Linguist catalogs (English source)
tests/                automated tests (Qt Test, CTest) and their helpers in tests/support/
scripts/              dependency setup, WASM build, server launcher, database creation SQL
docs/                 technical documentation (architecture.md)
```

Dependencies flow `client -> api -> core`, and the client reaches the backend
only through `backend/api/Backend.h`, linking `api` solely for standalone mode.
The root `CMakeLists.txt` adds `src/backend` only when not building for
Emscripten.

CMake mirrors the layers: `src/backend/CMakeLists.txt` defines `core` (static,
libpqxx), `api` (static, the RPC router; cpp-httplib private) and the `server`
executable; `src/client/CMakeLists.txt` defines the `client` executable, which
backs the `Guita` QML module (QML views plus the C++ types they use) next to
its QML files, so the module needs no per-directory `qmldir` (Qt policy
QTP0004). Executables are written to `build/<preset>/bin`.

## Contract

- `src/proto/` is the single definition of the client-backend API: messages
  and services in Protobuf (package `guita.v1`), generated with `protoc` for
  the backend (`contract`, `guita::v1`, never Qt) and with Qt Protobuf for the
  client (`contract_qt`, `qtc::guita::v1`, native and WASM). Neither side
  defines API message shapes by hand.
- Evolution rules: never reuse or renumber a field; reserve the numbers and
  names of removed fields; add instead of changing types or meanings; one
  request and one response message per method (`<Method>Request`,
  `<Method>Response`). Before a version is released anything may change; after
  it, incompatible changes go to a new package (`guita.v2`).
- `buf lint` and `buf format` (CTest `contract_lint`, `contract_format`) keep
  the style. Every change to `src/proto` also runs `scripts/check-contract.sh`
  (`buf breaking` with the `WIRE_JSON` rules against the development branch,
  `origin/develop/vX.Y.Z`), which must pass; the CI will run it on every PR.
  `WIRE_JSON` protects deployed clients and servers of other versions in binary
  and JSON; `FILE` is not used because it forbids even reserved removals.
- `int64` fields are not accessible from QML (Qt Protobuf makes them
  non-scriptable): the client's repositories adapt messages for the views.

## Backend

- No ORM: all the SQL of an aggregate lives in its store (`InvoiceStore`),
  which works inside the caller's transaction and maps rows to plain structs.
  Services hold the business rules and never contain SQL; they open their
  transactions with `Database::inTransaction()`.
- Every statement is a prepared statement registered in the store's static
  `prepare()` and listed in `prepareStatements()` (`core/Statements.cpp`);
  the backend prepares them at startup so that SQL errors fail fast.
- `pqxx::connection` is not thread-safe and the HTTP server handles requests
  concurrently: connections come from the `Database` pool, one per transaction.
- Money is never floating point: amounts are integer cents (`Cents`,
  `std::int64_t`) in C++ and in the JSON API (`totalCents`), and
  `numeric(12,2)` in PostgreSQL; stores convert in SQL. The client parses typed
  amounts as text (`Amount.js`) and keeps cents in a `double` (QML `int` is
  32-bit).
- Optimistic locking is explicit: updates check and increment `version`
  (`WHERE id = $1 AND version = $2`) and treat zero affected rows as a conflict.
- Procedures are registered per service on `RpcRouter`
  (`addInvoiceProcedures()`) with `add<Request, Response>()`, which checks them
  against the contract at startup. Handlers receive decoded messages, convert
  them to and from `core`'s structs and throw `std::invalid_argument` for
  validation errors; the router turns exceptions into Connect codes. Neither
  handlers nor `core` know the transport: HTTP statuses exist only in
  `HttpRpcTransport`.
- `backend/api/Backend.h` must not include cpp-httplib, libpqxx or protobuf
  (pimpl): the client only needs that interface to embed the backend.
- Authentication is a shared Bearer token for now (random per launch in
  standalone mode, `API_TOKEN` on the server, for development). Authentication
  is pluggable through `Authorizer`: no identity provider is part of the
  architecture. User authentication owned by the backend is pending.

## QML and C++

C++ types reach QML by declarative registration in the `Guita` module, never by
context properties, `qmlRegister*` calls or objects injected from `main.cpp`:

- Per-view objects (repositories, controllers) use `QML_ELEMENT` and are
  instantiated by the view that owns them (`Main.qml` creates `InvoiceRepository`).
  Repositories call procedures through `ApiConnection::call<Response>()` with
  the contract's Qt Protobuf messages and hand the views typed value types
  (`InvoiceRow`, `QML_VALUE_TYPE`), never `QVariantMap`.
- `ApiConnection` delegates to an `ApiTransport`: `HttpTransport`
  (`QNetworkAccessManager`, binary Connect RPC) or `InProcTransport` (native
  only: `Backend::call()` on a pool thread, results back through the event
  queue; its destructor waits for the calls in flight). Neither is visible to QML.
- Application-wide services are `QML_SINGLETON` with a static `create()`
  (`ApiConnection`). Qt prefers a default constructor over `create()`, so such
  a class must not be default-constructible.
- Views declare typed properties (`ApiConnection *connection` as a `REQUIRED`
  property), never `var`, so `qmllint` checks every access. `client_qmllint`
  must stay clean.
- `main.cpp` only installs the translator, handles the command-line flags,
  starts the embedded backend in standalone mode, configures `ApiConnection`
  and loads `Main`; no wiring between C++ objects and views lives there.
- Everything in the client is asynchronous (signals, callbacks): WASM forbids
  blocking waits and nested `QEventLoop::exec()`.

## Tests

- Every change lands the tests of its own behavior in the same work, and
  `ctest --preset native` must pass before the change is considered done.
  Bugs get a test that reproduces them before the fix.
- Tests use Qt Test (Qt Quick Test for QML logic) and are registered with
  `guita_add_test()` in `tests/CMakeLists.txt`, one `test_<name>.cpp` per
  suite, with exactly the label of what they need: `unit`, `db`, `api`,
  `client` or `qml`. Backend tests may link Qt; `core`, `api` and `server`
  never do.
- Database suites are declared with `DATABASE` and always work through
  `TestDatabase` (`<support/TestDatabase.h>`): a database of their own on the
  ephemeral server, migrated by the suite. They never use the application's
  default connection string or the developer's `guita` database, and never
  depend on data left by another suite.
- The test server is the ephemeral `postgres:18.6` container started by the
  CTest fixture (Docker), or the one in `GUITA_TEST_DB`. Tests must pass in
  parallel (`ctest -j`).
- API responses are checked by parsing them with Qt (`QJsonDocument`), not with
  the library the code under test uses.
- Keep the suite table in `docs/architecture.md` (§10.3) up to date.

## OpenSpec

OpenSpec is the default workflow for changes. For each change: create
`proposal.md` and `design.md` (plus `tasks.md` and required `specs/` deltas),
then STOP until the user approves proposal and design. Do not start
implementing the tasks until then. Leave a compiling build for visual
verification and archive only on user confirmation.

This is not mandatory. If the programmer explicitly states in a change request
that OpenSpec should not be used (or that the change should be made directly),
skip the OpenSpec workflow and implement the change directly. The initial POC
(v0.1.0) was built this way, without an OpenSpec change.

Artifact language: prose in `proposal.md`, `design.md`, `tasks.md` and
`specs/*.md` (motivation, decisions, requirement/scenario descriptions, etc.)
is written in Spanish, except the fixed labels imposed by the OpenSpec skills,
which stay in English as-is (`Why`, `What Changes`, `Capabilities`, `Impact`,
`Context`, `Goals`/`Non-Goals`, `Decisions`, `Risks / Trade-offs`, `Migration
Plan`, `Open Questions`, `Purpose`, `ADDED`/`MODIFIED`/`REMOVED`/`RENAMED
Requirements`, `Requirement:`, `Scenario:`, `WHEN`/`THEN`/`AND`,
`SHALL`/`MUST`). Code identifiers (classes, methods, packages) also stay in
English inside the Spanish text. This is independent of the GitHub issues
language (always English, see below).

## Persistent compatibility

The PostgreSQL schema is owned and versioned by this project through numbered
migrations in `src/backend/core/Migrations.cpp`, applied at startup by both the server and
standalone mode inside one transaction guarded by `pg_advisory_xact_lock`.
Never edit a released migration; add a new one (one statement per migration).
Table and column names are English `snake_case`; every table has a
`bigserial` `id` and an integer `version` for optimistic locking. The contract's field names are
English `snake_case` (lowerCamelCase in its JSON encoding); treat the contract as public once released.

## Language and files

Use English for source, identifiers, comments, CMake, scripts, technical docs
(README, `docs/`), commits, and pull requests. GitHub issues and milestones:
titles and descriptions always in English. OpenSpec artifacts and user
conversation remain in Spanish.

UI text must use `qsTr()` in QML and `tr()`/`QCoreApplication::translate()` in
C++. English is the source language: `src/client/translations/guita_en.ts` is
the source-language catalog Qt generates (only needed for plural forms) and
`src/client/translations/guita_es.ts` must keep its current displayed text;
refresh both with the `update_translations` target. Backend error messages are
English and not translated yet.

Text uses LF; never line-ending-normalize binaries such as `.qm` or images.

## C++ includes

Every `#include` uses angle brackets, never double quotes: Qt, library and
system headers (`<QNetworkReply>`, `<pqxx/pqxx>`, `<httplib.h>`) and project headers resolved from
`src/` (`<backend/core/InvoiceService.h>`, `<backend/api/Backend.h>`,
`<client/ApiConnection.h>`). Never use relative include paths or bare project
file names. The only exception is the Qt AUTOMOC generated file for a
`Q_OBJECT` defined in a `.cpp`, which stays double-quoted. CMake exposes `src/`
as the include root (`GUITA_INCLUDE_ROOT`: `core` publicly, `client`
privately). The QML type registration Qt generates includes each registered
header by its bare file name, so `client` also adds `src/client/` as a private
include directory; project code never relies on it.

## Local environment

- Dependencies that do not come from the system are installed without sudo in
  `~/software` (`SOFTWARE_DIR` in the scripts; the CMake presets assume
  `~/software`): libpqxx 8.0.2, cpp-httplib 0.61.0, protobuf
  36.2 (with abseil, built with C++23) and buf 1.74.0 (`scripts/build-deps.sh`),
  Qt 6.12.0 desktop + WASM with the `qtgrpc` module (Qt Protobuf) and
  emsdk 5.0.5 (`scripts/setup-qt.sh`). The emsdk version is the one the Qt WASM
  kit recommends (`QT_EMCC_RECOMMENDED_VERSION`); upgrade both together.
  Anything managed by `uv` (the Python ≥ 3.10 that Emscripten needs,
  `~/.local/bin/python3.12`) stays in uv's standard location; `EMSDK_PYTHON`
  must point to it, also after sourcing `emsdk_env.sh`, which clears it.
- System packages: GCC ≥ 13 (C++23), CMake ≥ 3.21, zlib,
  `libpq5-devel`, PostgreSQL.
- Docker (the user may run it without sudo) for the ephemeral test PostgreSQL;
  the local image is `postgres:18.6`.
- Build with the `native` and `wasm` CMake presets (`scripts/build-wasm.sh`
  also copies the web files to `build/web`, the server docroot).
- GUI checks run on a virtual display (`Xvfb :99`), never on the user's desktop:
  input simulation (`xdotool`) must not reach the user's windows. In Chrome on
  Xvfb, WebGL needs `--enable-unsafe-swiftshader --use-angle=swiftshader`.

## Working preferences

- Every message addressed to the user is written in Spanish: questions,
  progress updates, explanations and final summaries alike, even when the
  code, docs and commits being discussed are in English.
- Prefer durable context in repo files (this AGENTS.md) over internal agent
  memory, so it survives clones/moves of the repository.
- This file is versioned in the repository as durable context.
- `openspec/` is versioned and public on purpose: it shows how the project is
  developed with OpenSpec (proposals, designs, specs and archived changes), so
  write its artifacts knowing anyone who downloads the project can read them.
- `.opencode/` and `.claude/` stay private working material: they are
  git-ignored and NOT published. Do not reference them (nor `tmp/`) from
  published files (README, docs, ...). The public documentation is the README, kept light
  (overview, getting started, running), and `docs/architecture.md`, the
  technical reference: update the README when the plan changes visibly and the
  architecture document whenever the design or the toolchain changes.
- `CHANGELOG.md` follows Keep a Changelog 1.1.0 and Semantic Versioning. Every
  change that is visible to users or to developers building on the project
  adds its entry under `[Unreleased]` (Added, Changed, Deprecated, Removed,
  Fixed, Security) as part of the same work, written in English from the
  reader's point of view, not as a commit log. Internal steps never released
  (such as a dependency swapped before any version) are not listed. On release,
  `[Unreleased]` becomes `[X.Y.Z] - YYYY-MM-DD`, matching the CMake version and
  the `vX.Y.Z` tag, and comparison links are added at the bottom once tags exist.

## GitHub workflow (issues, branches, PRs)

In force since the first commit, made directly on `develop/v0.1.0` with the
initial POC (built without OpenSpec changes or issues). From then on, every
change follows the cycle below.

There is no `main`/`master`. The long-lived, default branch is
`develop/vX.Y.Z` (for example `develop/v0.2.0`), which holds the version
currently under development. Releasing cuts `release/vX.Y.Z` from it and tags
`vX.Y.Z`; hotfixes bump the patch digit. When a new cycle starts,
`develop/vX.Y.Z` is created from the released tag and becomes the default
branch. The program version lives as the single source of truth in
`CMakeLists.txt` (`project(... VERSION ...)`) and must match the branch suffix;
a Linux CI workflow (to be added) will enforce it on `develop/**` and
`release/**` pushes.

Each OpenSpec change is tracked on GitHub with this cycle:

1. OpenSpec proposal approved by the user.
2. GitHub issue for the change, linking its `openspec/changes/<name>/`
   folder (plus its milestone once milestones exist; the Projects board stays
   light: Todo / In progress / Done). While active, the change folder is named
   `is<n>-<slug>` after its issue; the date prefix is added only when the
   change is archived.
3. Branch created from the issue (Development panel → "Create a branch";
   name like `change/is<n>-<slug>`) starting from `develop/vX.Y.Z`.
4. Implementation on the branch + push (pushes are done by the user; the
   agent has no SSH access to `origin` from its shell). Commits are partial,
   one per task group of `tasks.md`, so that no commit is huge and the branch
   history follows the tasks:
   - The agent commits a group as soon as all its tasks are done and
     `ctest --preset native` passes; the commit carries the group's code,
     tests, documentation and its `tasks.md` checkboxes. The planning
     artifacts (proposal, design, specs, tasks) go in the first commit.
   - The title starts with the issue as `[is<n>]`, so the commits of an
     issue are easy to follow later, then an English summary of the group
     and its number, for example
     `[is1] Add the test infrastructure with an ephemeral PostgreSQL (group 1)`.
     The body ends with `Refs #<n>` so GitHub links the commit to the issue.
   - Nothing outside the task groups is committed without the user's
     confirmation, and the change is not archived nor sent to a PR until the user
     validates it, including visual verification.
   - Commits and PRs carry no co-authorship or AI attribution: no
     `Co-Authored-By` trailers, no "Generated with" lines.
5. Once the user validates the change, and before any PR, the agent archives
   it on the same branch: syncs its delta specs into `openspec/specs/`, moves
   it to `openspec/changes/archive/YYYY-MM-DD-is<n>-<slug>/` and commits that as
   the last commit of the branch (`[is<n>] Archive the <slug> change`). Then it
   reviews the change's Non-Goals: for each line leaving useful pending work,
   it creates a follow-up issue explaining the pending scope, linking the
   archived change, and keeping the relation to the original Non-Goal.
6. The user pushes and opens the PR toward `develop/vX.Y.Z` with `Closes #<n>`
   in the description → the Linux CI validates the PR → the user reviews the
   diff.
7. Merge commit as the norm (no squash): the per-group commits land on the
   development branch as they are, so its history keeps following the tasks
   of each change, and the merge commit marks where the change ends. Merged PR
   = closed issue (automatic via `Closes`) = archived change.
