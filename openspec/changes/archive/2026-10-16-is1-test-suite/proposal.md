# Proposal

## Why

Guita no tiene ninguna prueba automática: todo lo verificado hasta ahora (códigos de error del API, importes exactos, concurrencia del pool, los tres modos) se ha comprobado a mano. El backend no usa ORM, así que su SQL solo queda protegido por pruebas contra un PostgreSQL real, y el siguiente cambio (contrato Protobuf, #2) va a reescribir el API de punta a punta. Hace falta una batería que se ejecute con una sola orden, sin pantalla y sin tocar la base de datos del desarrollador, preparada para ejecutarse en un CI. Es el issue #1.

## What Changes

- Nuevo directorio `tests/` con pruebas escritas con Qt Test (y Qt Quick Test para la lógica QML), registradas en CTest y ejecutables sin pantalla.
- PostgreSQL efímero para las pruebas de integración, al estilo Testcontainers: un contenedor desechable que la propia ejecución de las pruebas levanta y destruye, o un servidor indicado expresamente por `GUITA_TEST_DB` (el que usará el CI). Cada suite trabaja en su propia base de datos, que se elimina al terminar.
- Pruebas por capas:
  - backend sin base de datos: validaciones de los servicios y autorización;
  - backend con base de datos: migraciones, persistencia de facturas (céntimos exactos, NIF nulo, restricción de importe), pool de conexiones en concurrencia y sentencias que no casan con el esquema;
  - API REST: contrato completo (códigos 400, 401, 405, 415, formas JSON, campos desconocidos, gzip) contra un backend en proceso;
  - cliente: `ApiConnection` e `InvoiceRepository` contra un backend embebido, como en el modo standalone;
  - QML: análisis y formato de importes.
- La lógica de importes de `Main.qml` pasa a un fichero propio del módulo QML para poder probarla, sin cambiar su comportamiento.
- Categorías de pruebas para ejecutar subconjuntos (por ejemplo, solo las que no necesitan base de datos).
- Documentación: cómo ejecutar las pruebas (README), la estrategia de pruebas (`docs/architecture.md`) y las reglas para cambios futuros (AGENTS.md).

## Capabilities

### New Capabilities
- `suite-de-pruebas`: batería de pruebas automáticas del proyecto: cómo se ejecuta, qué base de datos usa y garantiza no usar, qué cubre y cómo se seleccionan subconjuntos.

### Modified Capabilities
<!-- Ninguna: todavía no hay specs en openspec/specs/. -->

## Impact

- Código nuevo: `tests/` (CMake, utilidades comunes, suites), integración en el `CMakeLists.txt` raíz (`BUILD_TESTING`) y presets de prueba en `CMakePresets.json`.
- `src/client`: la lógica de importes sale de `Main.qml` a un fichero del módulo `Guita`; sin cambios de comportamiento.
- Sin dependencias nuevas de producción: Qt Test y Qt Quick Test vienen con Qt 6.12, y el cliente HTTP de las pruebas del API es el de cpp-httplib, ya presente. Para la base de datos efímera hace falta Docker en local, o `GUITA_TEST_DB`.
- Sin cambios en el API, en el esquema ni en las migraciones.
- Fuera de alcance: el workflow de GitHub Actions que ejecute la batería (issue posterior).
