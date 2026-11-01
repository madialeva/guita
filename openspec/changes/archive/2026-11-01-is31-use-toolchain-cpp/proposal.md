# Proposal

## Why

guita construye su toolchain con cinco scripts propios que instalan en `~/software`:
- `build-gcc.sh` y `build-gcc-mingw.sh`, los compiladores nativo y cruzado;
- `setup-qt.sh`, los kits de Qt;
- `build-deps.sh`, las librerías del backend;
- `build-static-deps.sh`, el Qt y la libpq estáticos.

gambasse tiene copias de parte de esos scripts. Ahora los construye, como componentes, el repositorio [toolchain-cpp](https://github.com/madialeva/toolchain-cpp), publicado como `v0.1.0`, con las mismas versiones, opciones y prefijos. Su cambio inicial ya compiló guita con él: el build nativo con las pruebas, el WebAssembly y el estático.

guita pasa a usarlo y se queda solo con lo que es suyo: los presets, CMake y sus scripts de build. Es el issue #31.

## What Changes

- **guita usa `toolchain-cpp` v0.1.0**, clonado junto a él (`../toolchain-cpp`), con estos componentes:
  - `gcc qt libpqxx cpp-httplib protobuf buf` para el build nativo y las pruebas;
  - `qt-wasm protobuf-wasm` para el cliente web;
  - `qt-static libpq-static` para el cliente autocontenido.
- **Se borran los cinco scripts del toolchain:** `build-gcc.sh`, `build-gcc-mingw.sh`, `setup-qt.sh`, `build-deps.sh` y `build-static-deps.sh`.
- **Los presets** buscan el compilador, Qt y las librerías en `${sourceParentDir}/toolchain-cpp/software/...`, en lugar de `$env{HOME}/software/...`.
- **Los scripts que usan `SOFTWARE_DIR`** (`build-wasm.sh`, `build-static.sh`, `check-contract.sh`) toman por defecto el directorio de instalación de ese clon.
- **El CI:**
  - saca `toolchain-cpp` junto a guita, en la versión fijada;
  - lo instala con sus componentes, enlazando el GCC de la imagen;
  - guarda la caché por esa versión, en lugar de por el hash de los scripts.
- **La documentación** (README, `docs/architecture.md`, AGENTS.md, CHANGELOG) describe qué usa guita de `toolchain-cpp`, y remite a su documentación para cómo se construye.

## Capabilities

### New Capabilities

Ninguna.

### Modified Capabilities

- `toolchain`:
  - guita obtiene su toolchain de `toolchain-cpp`, en una versión fijada;
  - salen los requisitos del compilador cruzado, que ahora son de `toolchain-cpp`;
  - el del compilador nativo deja de exigir un script propio.
- `cliente-estatico`: sale el requisito de las dependencias estáticas locales, que ahora son los componentes `qt-static` y `libpq-static` de `toolchain-cpp`.
- `integracion-continua`: el CI instala la toolchain con `toolchain-cpp` en la versión que fija guita, y la caché va por esa versión.

## Impact

- **Scripts:** cinco borrados, y tres con otro directorio por defecto.
- **CMake:** `CMakePresets.json`, y los mensajes de error y los comentarios de los `CMakeLists.txt` que nombraban los scripts. Su comportamiento no cambia.
- **CI:** `.github/workflows/ci.yml`. La primera ejecución vuelve a llenar la caché.
- **Código, contrato y base de datos:** sin cambios.
- **Entorno local:**
  - hay que clonar `toolchain-cpp` en `~/src/toolchain-cpp`, que ya está, con todo instalado;
  - `~/software` deja de usarse. Se puede borrar cuando también haya pasado gambasse (gambasse#65).
- **#29**, el cliente para Windows, se hará sobre esto, con las dependencias de Windows en `toolchain-cpp` (toolchain-cpp#2).
- **Los artefactos locales de is29**, sin commit, quedan pendientes de rehacer; este cambio no los toca.
