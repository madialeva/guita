# Tasks

## 1. Infraestructura de pruebas

- [x] 1.1 Añadir `include(CTest)` y `add_subdirectory(tests)` (con `BUILD_TESTING` y fuera de Emscripten) al `CMakeLists.txt` raíz, y un preset de prueba `native` con `output-on-failure`; verificar que `cmake --preset native` configura sin avisos
- [x] 1.2 Crear `tests/CMakeLists.txt` con `guita_add_test(<nombre> LABELS ... LIBS ... [DATABASE])` (Qt Test, `offscreen`, raíz de includes `tests/`) y una suite mínima de humo; verificar que `ctest --preset native` la ejecuta y pasa
- [x] 1.3 Crear `tests/postgres-fixture.sh` (`start`/`stop`: `GUITA_TEST_DB` o contenedor `postgres:18.6` con puerto y contraseña aleatorios, `pg_isready`, fichero con la cadena de conexión, limpieza de huérfanos `guita-test-pg-*`) y registrarlo como fixture de CTest; verificar que arranca y destruye el contenedor (`docker ps` vacío al terminar) y que sin Docker ni variable falla con el mensaje explicativo
- [x] 1.4 Crear `tests/support/TestDatabase.h/.cpp` (base `guita_test_<suite>_<aleatorio>` desde `template0`, eliminada con `WITH (FORCE)`) con una prueba de humo que la cree y elimine; verificar que no quedan bases de prueba en el servidor
- [x] 1.5 Documentar cómo ejecutar las pruebas (README), la estrategia (`docs/architecture.md`) y las reglas de pruebas para cambios futuros (AGENTS.md); verificar que las órdenes documentadas funcionan tal cual

## 2. Pruebas del backend sin base de datos

- [x] 2.1 Suite `invoiceservice` (`unit`): cliente obligatorio, importe negativo, importe por encima del máximo, sin conexión a base de datos (la normalización del NIF vacío, que solo se observa en la base de datos, se verifica en 4.1); retirar la suite de humo de 1.2; verificar con `ctest --preset native -L unit`
- [x] 2.2 Suite `authorizer` (`unit`): token correcto, incorrecto, ausente y token vacío que acepta todo; verificar con `ctest --preset native -L unit`

## 3. Pruebas del backend con base de datos

- [x] 3.1 Suite `migrations` (`db`): esquema desde cero, segunda ejecución sin cambios, `schema_version` = 1, restricción `CHECK` contra importes negativos; verificar con `ctest --preset native -L db`
- [x] 3.2 Suite `invoicestore` (`db`): inserción y listado ordenado, céntimos exactos (5, 123450, 999999999999), NIF nulo; verificar con `ctest --preset native -L db`
- [x] 3.3 Suite `database` (`db`): pool con más hilos concurrentes que conexiones, rollback cuando la transacción lanza, fallo de `warmUp` con una sentencia incompatible con el esquema; verificar con `ctest --preset native -L db`

## 4. Pruebas del API REST

- [x] 4.1 Suite `restapi` (`api`): `Backend` en el puerto 0 con token y `httplib::Client`; 201 y 200 con sus formas JSON, 401, 400 por validación y por JSON inválido, 415, 405 con `Allow`, campo desconocido ignorado, NIF vacío guardado como `NULL` y respuesta gzip; verificar con `ctest --preset native -L api`

## 5. Pruebas del cliente

- [ ] 5.1 Suite `invoicerepository` (`client`): `ApiConnection` configurada contra un `Backend` embebido con token aleatorio, `InvoiceRepository` lista, crea y emite `failed` con el mensaje del backend, comprobado con `QSignalSpy`; verificar con `ctest --preset native -L client`
- [ ] 5.2 Mover `toCents` y `formatCents` de `Main.qml` a `src/client/Amount.js` (`.pragma library`) importado por `Main.qml`; verificar que `client_qmllint` sigue limpio y que el cliente guarda y lista facturas igual que antes (Xvfb)
- [ ] 5.3 Suite QML `amount` (`qml`) con Qt Quick Test: `1234,5`, `1234.50`, `0`, máximo, `1.234,50`, `-1`, texto vacío y formato con dos decimales; verificar con `ctest --preset native -L qml`

## 6. Integración

- [ ] 6.1 Ejecutar la batería completa en serie y en paralelo (`ctest --preset native -j8`) con el contenedor efímero y con `GUITA_TEST_DB`, y comprobar que ambos resultados coinciden, que no quedan contenedores ni bases de prueba y que la base `guita` de desarrollo no se ha tocado
- [ ] 6.2 Añadir la entrada correspondiente en `CHANGELOG.md` (`[Unreleased]` → Added) y verificar que describe lo que ve un desarrollador
