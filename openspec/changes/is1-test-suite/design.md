# Design

## Context

Ver proposal.md (Why) y la spec `suite-de-pruebas`. Condiciones que dan forma al diseño:

- El backend (`core`, `api`) no depende de Qt; las pruebas sí pueden usarlo, porque son ejecutables aparte que nunca forman parte de `server` ni del cliente.
- `Database` no abre conexiones hasta que se usa, así que las validaciones de los servicios pueden probarse sin PostgreSQL.
- `Backend` se puede arrancar dentro de un proceso en el puerto 0 (es lo que hace el modo standalone).
- La lógica de importes vive hoy como funciones dentro de `Main.qml`, que no se puede cargar en una prueba sin arrancar toda la ventana.
- En local hay Docker sin sudo y la imagen `postgres:18.6`; el CI de GitHub Actions tendrá Docker y contenedores de servicio.

## Goals / Non-Goals

**Goals:**
- Una sola orden (`ctest --preset native`) para todo, en local y en CI, con el mismo resultado.
- Pruebas de base de datos aisladas y paralelizables, sin depender de ningún servidor preexistente.
- Que añadir una prueba nueva sea crear un fichero y una línea de CMake.

**Non-Goals:**
- El workflow de GitHub Actions (issue posterior); aquí solo se deja preparado el mecanismo `GUITA_TEST_DB` que usará.
- Pruebas de la interfaz completa (simulación de clics sobre `Main.qml`); se prueban la lógica y el acceso al API, no el diseño de pantalla.
- Pruebas del cliente WebAssembly en navegador; el código del cliente es el mismo y se prueba en nativo.
- Medición de cobertura de código.

## Decisions

### 1. Qt Test para todo, también para el backend

Decidido con el usuario. Un único framework, como en gambasse, y sin dependencias nuevas. Los ejecutables de prueba del backend enlazan `Qt6::Test`, pero los targets `core`, `api` y `server` siguen sin Qt.
- *Alternativas*: GoogleTest o Catch2 para el backend (aislaría Qt también en las pruebas, a cambio de dos frameworks y una dependencia más).

### 2. Estructura y registro en CTest

- `include(CTest)` en el `CMakeLists.txt` raíz y `add_subdirectory(tests)` si `BUILD_TESTING` y no es Emscripten.
- `tests/CMakeLists.txt` define `guita_add_test(<nombre> LABELS ... LIBS ... [DATABASE])`: compila `test_<nombre>.cpp`, enlaza `Qt6::Test` y las librerías indicadas, añade `tests/` como raíz de includes (`<support/TestDatabase.h>`), fija `QT_QPA_PLATFORM=offscreen` y, con `DATABASE`, la dependencia de la fixture de PostgreSQL.
- Utilidades comunes en `tests/support/`.
- Un preset de prueba `native` en `CMakePresets.json` (`output-on-failure`), para que la orden sea `ctest --preset native`.

### 3. PostgreSQL efímero al estilo Testcontainers, con fixtures de CTest

En C++ no hay un equivalente maduro de Testcontainers (existe *testcontainers-native*, un envoltorio en C de testcontainers-go, todavía experimental). Lo idiomático es lo que hace Testcontainers por debajo, expresado con **fixtures de CTest**:

- `FIXTURES_SETUP`: un script (`tests/postgres-fixture.sh start`) que, si `GUITA_TEST_DB` no está definida, lanza `docker run --rm -d` con la imagen `postgres:18.6` (configurable con `GUITA_TEST_PG_IMAGE`), un puerto aleatorio en `127.0.0.1` y una contraseña aleatoria, espera a `pg_isready` y escribe la cadena de conexión en un fichero del directorio de build. Si `GUITA_TEST_DB` está definida, no hace nada.
- `FIXTURES_CLEANUP`: `tests/postgres-fixture.sh stop` elimina el contenedor. CTest lo ejecuta aunque fallen pruebas.
- Las suites con `DATABASE` declaran `FIXTURES_REQUIRED`, así que CTest arranca el contenedor una sola vez por ejecución, solo si alguna prueba seleccionada lo necesita.
- Sin Docker ni `GUITA_TEST_DB`, el `start` falla con un mensaje que explica ambas opciones, y CTest marca como no ejecutadas (es decir, fallidas) las suites dependientes, como exige la spec.
- *Alternativas*: instancia local con `initdb`/`pg_ctl` (no necesita Docker, pero depende de que estén instalados los binarios del servidor y de su versión); un servidor compartido fijo (rompe el aislamiento).

### 4. Una base de datos por suite

`TestDatabase` (`tests/support/`) obtiene la cadena de conexión del servidor (de `GUITA_TEST_DB` o del fichero de la fixture), crea `guita_test_<suite>_<aleatorio>` desde `template0` y la elimina en su destructor (`DROP DATABASE ... WITH (FORCE)`). Cada suite empieza con un esquema vacío y aplica las migraciones a través del propio backend. Esto permite `ctest -j` y garantiza que no quedan restos.
- *Alternativas*: transacción con rollback por prueba (no sirve para migraciones ni para el pool, que abre sus propias conexiones); `TRUNCATE` entre pruebas (acopla las suites y no permite paralelismo).

### 5. Suites y categorías

| Suite | Categoría (label) | Qué cubre |
|---|---|---|
| `invoiceservice` | `unit` | validaciones (cliente obligatorio, importe negativo y máximo, NIF vacío) sin conectar a la base de datos |
| `authorizer` | `unit` | `tokenAuthorizer`: token correcto, incorrecto, ausente y vacío |
| `migrations` | `db` | esquema desde cero, segunda ejecución sin cambios, `schema_version`, restricción `CHECK` |
| `invoicestore` | `db` | inserción y listado, céntimos exactos (0,05, 1234,50 y máximo), NIF nulo, orden |
| `database` | `db` | pool bajo concurrencia con más hilos que conexiones, rollback al lanzar excepción, fallo al arrancar con una sentencia incompatible |
| `restapi` | `api` | `Backend` en el puerto 0 y `httplib::Client`: 201, 200, 401, 400 (validación y JSON), 415, 405 con `Allow`, campos desconocidos, gzip |
| `invoicerepository` | `client` | `ApiConnection` + `InvoiceRepository` contra un `Backend` embebido con token, con `QSignalSpy`: listar, crear, error del backend |
| `amount` | `qml` | `toCents` y `formatCents` con Qt Quick Test |

Las respuestas del API se analizan en las pruebas con `QJsonDocument`, para que las pruebas no dependan de la misma librería JSON que el código probado.

### 6. Lógica de importes en un fichero propio

`toCents` y `formatCents` salen de `Main.qml` a `src/client/Amount.js` (`.pragma library`), incluido en el módulo `Guita` e importado por `Main.qml`. La prueba QML lo importa por ruta con Qt Quick Test (`QUICK_TEST_SOURCE_DIR`), sin cargar la ventana. El comportamiento no cambia.
- *Alternativas*: pasarlo a C++ (`Q_INVOKABLE` en un singleton): más código para una lógica de presentación; mantenerlo en `Main.qml`: no se puede probar aislado.

### 7. Cómo encaja en GitHub Actions (para el issue posterior)

El job definirá un contenedor de servicio `postgres:18.6` con *health check* y exportará `GUITA_TEST_DB=host=localhost port=5432 user=postgres password=... dbname=postgres`; con eso la fixture no lanza ningún contenedor y el resto es `cmake --preset native`, `cmake --build --preset native` y `ctest --preset native`. Las pruebas son las mismas en local y en CI; solo cambia quién proporciona el servidor.

## Risks / Trade-offs

- [El desarrollo usa PostgreSQL 17 y las pruebas 18.6] → El proyecto no usa nada específico de una versión; las pruebas fijan la versión del CI y una prueba que dependa de la versión se verá en ambos.
- [Arrancar el contenedor añade unos segundos] → Una sola vez por ejecución gracias a la fixture; `ctest -L unit` no lo necesita.
- [Docker ausente en algunas máquinas] → `GUITA_TEST_DB` permite usar cualquier servidor, y la categoría `unit` funciona sin base de datos.
- [Contenedores huérfanos si se interrumpe CTest a la fuerza] → `--rm` y un nombre con prefijo fijo (`guita-test-pg-`) que el `start` limpia antes de lanzar uno nuevo.
- [Pruebas del cliente con red real en loopback] → Puerto 0 y token aleatorio, igual que el modo standalone; sin dependencias externas.

## Migration Plan

No aplica: solo se añaden pruebas, documentación y el movimiento de la lógica de importes a `Amount.js`, sin cambios de comportamiento ni de datos. Revertir es eliminar `tests/` y deshacer el movimiento.
