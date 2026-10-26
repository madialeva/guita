# Tasks

## 1. GCC 16 y C++26 en local

- [x] 1.1 Crear `scripts/build-gcc.sh` según la decisión 1 del diseño. La cabecera documenta:
  - las variables `SOFTWARE_DIR` y `GCC_VERSION`;
  - los requisitos: un compilador de C++ del sistema para compilarlo, `make`, `m4`, `bzip2`, `xz`, `wget` y `gpg`;
  - por qué fija el `as` y el `ld`.

  Verificar:
  - que una instalación completa en un `SOFTWARE_DIR` vacío dentro de `tmp/` deja un `g++ -v` con las mismas opciones de configuración que `~/software/gcc-16.2.0`, salvo el prefijo;
  - que una segunda ejecución, y una sobre `~/software`, terminan sin compilar nada.
- [x] 1.2 Cambiar `scripts/build-deps.sh` según las decisiones 2, 5 y 6:
  - compilador `$SOFTWARE_DIR/gcc-$GCC_VERSION`, con un error que remite a `scripts/build-gcc.sh` si falta;
  - `-DCMAKE_CXX_STANDARD=26` en libpqxx, protobuf y abseil (nativo y WebAssembly);
  - `-Wl,-rpath` al directorio del `libstdc++` del compilador en los ejecutables nativos;
  - directorios de compilación limpios;
  - la cabecera actualizada.

  Ejecutarlo sobre `~/software`. Verificar:
  - que `readelf -d ~/software/protobuf-36.2/bin/protoc` muestra el `RUNPATH` a `gcc-16.2.0/lib64`;
  - que `protoc --version` arranca desde una shell sin `LD_LIBRARY_PATH`;
  - que la protobuf de WebAssembly queda instalada de nuevo.
- [x] 1.3 Cambiar el `CMakeLists.txt` raíz y `CMakePresets.json` según las decisiones 2 a 5:
  - `cmake_minimum_required(VERSION 3.25)` y `CMAKE_CXX_STANDARD 26`;
  - la comprobación del compilador nativo (GCC 16 o posterior, y `check_cxx_source_compiles` con `-std=c++26 -freflection`);
  - el directorio del `libstdc++` del compilador en `CMAKE_BUILD_RPATH`;
  - `CMAKE_CXX_COMPILER` en el preset `native`.

  Verificar:
  - que configurar en `tmp/` con el `g++` de gcc-toolset-14 falla antes de buscar Qt, con un mensaje que nombra GCC 14 y `scripts/build-gcc.sh`;
  - que `cmake --preset native` configura desde un `build/native` vacío;
  - que `build/native/bin/server` lleva el `RUNPATH` a `gcc-16.2.0/lib64` y arranca desde una shell sin `LD_LIBRARY_PATH`.
- [x] 1.4 Compilar el preset `native` completo y pasar `ctest --preset native` con GCC 16. Corregir los avisos nuevos del código propio y anotar en `docs/architecture.md` §8 los de terceros, si los hay. Verificar:
  - que `ctest --preset native` pasa completo;
  - que `cmake --build --preset bench` compila las mediciones;
  - que `compile_commands.json` muestra `-std=gnu++26` y el `g++` de `gcc-16.2.0`.
- [x] 1.5 Compilar el cliente web en C++26 con `scripts/build-wasm.sh` y el preset `wasm-bench`. Verificar:
  - que `build/wasm/build.ninja` usa `-std=gnu++26`;
  - que `bench_encoding` de `wasm-bench` comprueba los bytes de referencia del contrato;
  - que el cliente web, servido por `scripts/run-server.sh` y abierto en Chrome sobre Xvfb, lista y crea facturas.
- [x] 1.6 Documentar la toolchain:
  - AGENTS.md:
    - C++26, GCC 16.2 en nativo y el Clang de emsdk en WebAssembly;
    - la regla del subconjunto del cliente, y compilar `wasm` en local al tocar `src/client` hasta #12;
    - entorno local con `scripts/build-gcc.sh` y CMake 3.25;
    - protobuf y abseil en C++26.
  - README: badge de C++26, tabla de requisitos e instalación con `scripts/build-gcc.sh` antes de `scripts/build-deps.sh`.
  - `docs/architecture.md`:
    - §3: el proyecto es C++26;
    - §8: tabla de la toolchain, notas del compilador y del rpath, y la tabla de lo que admite el Clang de emsdk 5.0.5.
  - CHANGELOG: entrada en `Added`.

  Verificar:
  - que `grep -n "C++23\|GCC ≥ 13\|13 or later\|3\.21"` sobre AGENTS.md, README, `docs/` y `scripts/` solo encuentra las menciones del CI, que cambian en el grupo 2;
  - que los pasos de instalación del README funcionan tal cual en el orden documentado.

## 2. CI en el contenedor `gcc:16.2.0-trixie`

- [x] 2.1 Cambiar `.github/workflows/ci.yml` según la decisión 7:
  - `container: gcc:16.2.0-trixie` en el mismo job `Build and test`;
  - `GUITA_TEST_DB` con `host=postgres` y sin publicar el puerto;
  - `apt-get` sin `sudo`, sin `CC`/`CXX`;
  - el paso que enlaza `~/software/gcc-16.2.0` → `/usr/local`;
  - el paso que compara `g++ -dumpfullversion` con el `GCC_VERSION` de `scripts/build-gcc.sh`;
  - la clave de caché `toolchain-gcc-16.2.0-trixie-...`.

  Verificar que `actionlint` (imagen Docker `rhysd/actionlint`) no da avisos.
- [x] 2.2 Reproducir en local los pasos del job dentro de `gcc:16.2.0-trixie`, con un `HOME` vacío y un `postgres:18.6` alcanzable como `postgres` en una red de Docker, para fijar la lista de paquetes de `apt`. Verificar:
  - que dentro del contenedor `ctest --preset native` pasa completo y `scripts/check-contract.sh` termina bien;
  - que el paso de la versión falla si `GCC_VERSION` no coincide con el GCC de la imagen.

  Encontrado al verificar: en el contenedor, `channel` fallaba siempre en `closesAClientThatDoesNotRead` después de `limitsTheChannels`. La prueba publicaba sin esperar a que la suscripción del cliente que no lee estuviera registrada, y si el servidor tardaba un poco más, los eventos no llegaban a nadie y el canal no se cerraba. Corregido en `tests/test_channel.cpp`: la prueba lee antes el saludo `started` de la suscripción, como las demás. La pareja de pruebas pasa 12 de 12 veces en el contenedor.
- [x] 2.3 Documentar el CI:
  - `docs/architecture.md` §10.4: el contenedor, el enlace del compilador, la comprobación de versión, PostgreSQL por nombre y la clave de la caché;
  - AGENTS.md: el párrafo del CI, y que la versión de GCC se sube a la vez en `scripts/build-gcc.sh`, `scripts/build-deps.sh`, el preset y la imagen del CI.

  Verificar que no queda ninguna mención de `gcc-14`, `GCC 14` ni `ubuntu-24.04 with GCC` en AGENTS.md, README y `docs/`.

## 3. Verificación en GitHub

- [ ] 3.1 El usuario sube la rama y abre el PR hacia `develop/v0.1.0` (puede ser en borrador). Verificar:
  - que el check `Build and test` sigue apareciendo como obligatorio;
  - que el job se ejecuta dentro de `gcc:16.2.0-trixie`;
  - que la ejecución en frío instala la toolchain, guarda la caché con la clave nueva y termina en verde.

  Encontrado en la primera ejecución: la instalación (11 minutos), la caché, la compilación y la batería pasaron, pero `scripts/check-contract.sh` falló por "dubious ownership". El checkout pertenece al usuario del runner y los pasos se ejecutan como root dentro del contenedor, y `actions/checkout` solo lo marca como seguro durante su propio paso. Un paso nuevo, "Trust the checkout", añade el directorio y su `.git` (que clona buf) a `safe.directory`. La reproducción local de la tarea 2.2 no lo detectó porque los ficheros se pusieron a nombre de root.
- [ ] 3.2 Relanzar el job desde el PR. Verificar que restaura la caché, no ejecuta los scripts de instalación y termina en verde.
- [ ] 3.3 Con el visto bueno del usuario, comentar en #12 que su job WebAssembly tendrá que ejecutarse también en `gcc:16.2.0-trixie`, porque `scripts/build-deps.sh` compila con GCC 16 el `protoc` nativo. Verificar que el comentario enlaza este cambio.
