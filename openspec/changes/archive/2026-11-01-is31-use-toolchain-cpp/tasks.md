# Tasks

## 1. guita sobre toolchain-cpp

- [x] 1.1 Cambiar los presets `native`, `wasm` y `static` a `${sourceParentDir}/toolchain-cpp/software/...` (decisión 2), y los scripts `build-wasm.sh`, `build-static.sh` y `check-contract.sh` al directorio de instalación de `toolchain-cpp`, normalizado (decisión 3). Borrar los cinco scripts del toolchain (decisión 4). Verificar:
  - que `cmake --preset native`, la compilación y `ctest --preset native` pasan, que los `CMakeCache.txt` solo nombran rutas de `~/src/toolchain-cpp` y que `readelf -d build/native/bin/server` muestra el run path a su GCC;
  - que `scripts/build-wasm.sh` genera el cliente web;
  - que `scripts/build-static.sh` genera y comprueba `build/dist/client`;
  - que `scripts/build-static.sh --check build/native/bin/client` falla y nombra las librerías de `toolchain-cpp`;
  - que `scripts/check-contract.sh` pasa.
- [x] 1.2 Adaptar `.github/workflows/ci.yml` (decisión 5). Verificar en local, en el contenedor `gcc:16.2.0-trixie`, que los pasos de checkout, enlace, instalación y caché dejan los dos repositorios uno al lado del otro y que `toolchain-cpp/install.sh` con los componentes del CI termina. La prueba completa es la tarea 2.1.
- [x] 1.3 Documentar (decisión 6): el README, `docs/architecture.md` (§8, §9.2, §10.4), AGENTS.md y el CHANGELOG. Verificar:
  - que ningún fichero versionado, salvo los cambios archivados, nombra `~/software`, `$HOME/software` ni los scripts borrados;
  - que los pasos de cómo empezar del README funcionan tal cual;
  - que `ctest --preset native` pasa.

## 2. Verificación

- [x] 2.1 El usuario hace push de la rama y lanza el CI a mano (`workflow_dispatch`), antes del PR, y el CI pasa: es la primera ejecución en limpio, con la caché nueva.
- [x] 2.2 El usuario revisa el cambio y da el visto bueno.
