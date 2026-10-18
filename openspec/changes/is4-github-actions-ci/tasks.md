# Tasks

## 1. Scripts para el CI

- [x] 1.1 Añadir a `scripts/setup-qt.sh` la opción `--desktop-only`: solo el kit `linux_gcc_64` con `qttasktree`, sin kit WASM, emsdk ni Python con uv. Documentarla en su cabecera. Verificar que, con `SOFTWARE_DIR` apuntando a un directorio vacío en `tmp/`, la opción instala solo el kit de escritorio sin llamar a uv. Verificar también que sin la opción el script sigue sin hacer nada sobre la instalación existente.
- [x] 1.2 Crear `scripts/check-version.sh <rama>`: compara la versión de `CMakeLists.txt` con `develop/vX.Y.Z` o `release/vX.Y.Z`, falla mostrando las dos versiones si no coinciden y pasa con cualquier otra rama. Registrar en `tests/CMakeLists.txt` las pruebas `unit` `version_check_*`, derivadas de `PROJECT_VERSION`: las ramas `develop/` y `release/` de la versión actual pasan, otra versión falla con su mensaje (`PASS_REGULAR_EXPRESSION`) y una rama de cambio pasa. Verificar con `ctest --preset native -L unit`.
- [x] 1.3 Corregir la cabecera de `scripts/check-contract.sh`, que aún dice `FILE`: las reglas son `WIRE_JSON`, según `src/proto/buf.yaml`. Verificar que `scripts/check-contract.sh` sigue pasando en local.
- [x] 1.4 Documentar `--desktop-only` y `check-version.sh` en `docs/architecture.md` (§8 y la tabla de suites de §10.3) y en el README (instalación). Verificar que las órdenes documentadas funcionan tal cual y que `ctest --preset native` pasa completo.

## 2. Workflow de GitHub Actions

- [ ] 2.1 Crear `.github/workflows/ci.yml` según el diseño:
  - disparadores: push a `develop/**` y `release/**`, PR hacia ellas y `workflow_dispatch`;
  - `concurrency`, `permissions: contents: read` y `timeout-minutes`;
  - un job con nombre fijo en `ubuntu-24.04` con GCC 14 y el servicio `postgres:18.6` con *health check* y `GUITA_TEST_DB`;
  - checkout con `fetch-depth: 0`, `check-version.sh` contra `base_ref` o `ref_name` (salvo en `workflow_dispatch`), paquetes de `apt`, caché restore/save de `~/software` con instalación entre medias y borrado de `~/software/src` y `aqt-venv`, configuración, compilación, `ctest -j` y `check-contract.sh`.

  Verificar que `actionlint` (imagen Docker `rhysd/actionlint`, sin instalarla en el proyecto) no da avisos.
- [ ] 2.2 Reproducir en local los pasos del job en un contenedor `ubuntu:24.04` con un `HOME` vacío y GCC 14, y el PostgreSQL en `GUITA_TEST_DB`, para fijar la lista de paquetes de `apt` que necesitan Qt y el proyecto. Verificar que en el contenedor `ctest --preset native` pasa completo y `check-contract.sh` termina bien.
- [ ] 2.3 Documentar el CI:
  - `docs/architecture.md`: §10.4 con el workflow real, la caché y el ruleset que exige el check; §3 y §11;
  - AGENTS.md: la comprobación de versión y el contrato en presente;
  - README: badge, fila "Continuous integration" en ✅, mención del CI en Testing;
  - CHANGELOG: entrada en `Added`.

  Verificar que no queda ninguna mención del CI en futuro (`grep -n "will run\|to be added"` en los documentos) y que el badge apunta al workflow.

## 3. Verificación en GitHub

- [ ] 3.1 Con la rama subida por el usuario, lanzar el workflow con `workflow_dispatch` sobre `change/is4-github-actions-ci`. Verificar que la ejecución en frío instala la toolchain, guarda la caché y termina en verde.
- [ ] 3.2 Relanzarlo. Verificar que restaura la caché, no ejecuta los scripts de instalación y termina en verde en pocos minutos.
- [ ] 3.3 Comprobar el camino en rojo en una rama desechable con una prueba rota y un campo del contrato borrado sin reservar. Verificar que el job falla en `ctest` y en `check-contract.sh` con mensajes claros, que la caché no se pierde y que la rama desechable se borra después.
- [ ] 3.4 Abrir el PR hacia `develop/v0.1.0` (lo hace el usuario). Verificar que el check aparece en el PR y que `check-version.sh` compara con `develop/v0.1.0`.
- [ ] 3.5 El propietario crea el ruleset sobre `develop/**` y `release/**` que exige el check del job, siguiendo `docs/architecture.md` §10.4. Verificar que el PR muestra el check como obligatorio y que el badge del README se ve tras la fusión.
