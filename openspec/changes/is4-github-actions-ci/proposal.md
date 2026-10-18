# Proposal

## Why

Hoy nada impide que un PR rompa la compilación, las pruebas o la compatibilidad del contrato: `ctest --preset native` y `scripts/check-contract.sh` solo se ejecutan cuando alguien se acuerda en su máquina. Ya hay tres cambios fusionados así, y la documentación (AGENTS.md, `docs/architecture.md` §3 y §10.4) da por hecho un CI que "ejecutará" esas comprobaciones en cada PR. Conviene tenerlo antes del canal WebSocket (#5), el cambio más grande hasta ahora y uno que toca `src/proto/`. Es el issue #4.

## What Changes

- **Workflow de GitHub Actions** en Linux que se ejecuta en cada push a `develop/**` y `release/**`, en cada PR hacia esas ramas y a mano (`workflow_dispatch`, para probarlo en una rama de cambio antes de abrir el PR).
- **Toolchain instalada con los scripts del repositorio**, igual que en local:
  - `scripts/setup-qt.sh` gana un modo que instala solo el kit de escritorio de Qt, sin el kit WASM, emsdk ni Python de uv, que el CI no necesita;
  - `scripts/build-deps.sh` se usa tal cual (sin emsdk ya omite protobuf para WebAssembly).
- **Caché de `~/software`** con una clave que depende de los scripts de instalación, el sistema y el compilador, guardada justo después de instalar, de modo que una ejecución con pruebas fallidas no obliga a recompilar protobuf con abseil la próxima vez.
- **PostgreSQL como contenedor de servicio** `postgres:18.6` con *health check*, entregado a las pruebas por `GUITA_TEST_DB`: la fixture de CTest no arranca ningún contenedor. Las pruebas son las mismas que en local.
- **`cmake --preset native`, `cmake --build --preset native` y `ctest --preset native`** en paralelo y sin pantalla, lo que ya incluye `contract_lint`, `contract_format` y `contract`.
- **Compatibilidad del contrato:** `scripts/check-contract.sh` (`buf breaking` con `WIRE_JSON` contra `origin/develop/vX.Y.Z`) en cada ejecución, con el historial completo para que tenga la rama de referencia. Se corrige su comentario de cabecera, que aún habla de las reglas `FILE`.
- **Versión frente a rama:** un script nuevo, `scripts/check-version.sh`, comprueba que la versión de `CMakeLists.txt` coincide con la rama `develop/vX.Y.Z` o `release/vX.Y.Z` (la del push o la de destino del PR). Tiene su propia prueba en CTest y se puede ejecutar en local.
- **Visibilidad:** badge del workflow en el README y el CI marcado como hecho en su tabla de estado.
- **Documentación de cómo exigirlo:** que un PR no se pueda fusionar con el CI en rojo es una regla de la rama en GitHub (ruleset con el check obligatorio), que configura el propietario del repositorio. Queda documentada y se aplica como parte de la verificación.

## Capabilities

### New Capabilities

- `integracion-continua`: validación automática en GitHub Actions de cada push a las ramas de desarrollo y de release y de cada PR hacia ellas (compilación, batería de pruebas con PostgreSQL, compatibilidad del contrato y versión frente a rama), con una toolchain instalada por los scripts del proyecto y cacheada.

### Modified Capabilities

Ninguna. La batería de pruebas (`suite-de-pruebas`) ya prevé ejecutarse en un CI con `GUITA_TEST_DB` y no cambia.

## Impact

- **Código y scripts:**
  - `.github/workflows/ci.yml` (nuevo);
  - `scripts/setup-qt.sh` (modo solo escritorio);
  - `scripts/check-version.sh` (nuevo) y su prueba en `tests/CMakeLists.txt`;
  - `scripts/check-contract.sh` (solo el comentario).
- **Dependencias:** ninguna de producción. El workflow usa acciones oficiales de GitHub (`actions/checkout`, `actions/cache`) y paquetes del sistema del runner (GCC 14, Ninja, libpq, zlib y las librerías que Qt necesita para ejecutarse sin pantalla).
- **Comportamiento:** ninguno de la aplicación, el API ni la base de datos.
- **GitHub:** un ruleset en `develop/**` y `release/**` que exige el check del CI, a configurar por el propietario del repositorio.
- **Documentación:**
  - `docs/architecture.md`: §3 (comprobación del contrato), §8 (toolchain), §10.4 (CI, ahora real) y §11 (deja de estar pendiente);
  - AGENTS.md: la comprobación de versión deja de estar "por añadir";
  - README: badge, estado y pruebas;
  - CHANGELOG: entrada en `Added`.
