# Design

## Context

- **Qué ofrece `toolchain-cpp` v0.1.0** (madialeva/toolchain-cpp#1):
  - once componentes que se instalan por nombre (`./install.sh <componente>...`) en `software/`, dentro de su clon, con los mismos prefijos que el `~/software` de hoy (`gcc-16.2.0`, `Qt/6.12.0/gcc_64`, `protobuf-36.2`, ...);
  - lo que compilan es lo mismo que compilaban los scripts de guita: se comparó prefijo a prefijo;
  - un `gcc-16.2.0` que es un enlace a un GCC de la misma versión cuenta como instalado, para el CI;
  - su README documenta cómo lo usa un proyecto clonado al lado: presets, scripts y CI.
- **Lo que ya se probó.** En su cambio inicial, guita compiló con sus componentes mediante un `CMakeUserPresets.json` local:
  - el build nativo, y `ctest` pasó;
  - el build WebAssembly;
  - el cliente estático, que pasó `scripts/build-static.sh --check`.

  También se vio que las rutas de los presets tienen que partir de `${sourceParentDir}`. Con `${sourceDir}/..`, el build WebAssembly no encuentra las herramientas de Linguist del kit del host.
- **Qué usa hoy `~/software` en guita:**
  - `CMakePresets.json`: los presets `native`, `wasm` y `static`, con `$env{HOME}/software/...`;
  - `scripts/build-wasm.sh` (el `emsdk_env.sh`), `scripts/build-static.sh` (comprueba que existen el Qt estático y la libpq estática, y que el ejecutable no carga librerías de `SOFTWARE_DIR`) y `scripts/check-contract.sh` (busca `buf`);
  - el CI: el enlace al GCC de la imagen en `~/software/gcc-16.2.0`, la caché de `~/software` por el hash de `setup-qt.sh` y `build-deps.sh`, y la comprobación del GCC de la imagen contra `build-gcc.sh`;
  - la documentación: el README, `docs/architecture.md` (§8, §9.2, §10.4), AGENTS.md y el CHANGELOG, este con entradas que nombran los scripts.
- **La versión 0.1.0 de guita no está publicada.** Su CHANGELOG describe aún lo que traerá, y según AGENTS.md los pasos internos nunca publicados no se listan.
- **En local**, `~/src/toolchain-cpp` ya está junto a `~/src/guita` con todos los componentes instalados.

## Goals / Non-Goals

**Goals:**
- Que guita no construya nada de su toolchain y lo tome todo de `toolchain-cpp`, en una versión fijada.
- Presets, scripts y CI que lo encuentren sin variables de entorno.
- La misma compilación y las mismas pruebas que hoy, en local y en el CI.

**Non-Goals:**
- **gambasse**, que pasa en su propio cambio (gambasse#65).
- **El cliente para Windows (#29).** Se rehará sobre esto, con sus dependencias en `toolchain-cpp` (toolchain-cpp#2).
- **Borrar `~/software`.** Es local, y gambasse aún lo usa.
- **Cambiar nada de `toolchain-cpp`.** Si hiciera falta algo, sería un cambio en ese repositorio.
- **El CMake de guita.** Las comprobaciones del compilador y del Qt estático no cambian, porque no dependen de dónde está la toolchain.

## Decisions

### 1. toolchain-cpp junto a guita, en una versión fijada

- **Dónde:** clonado al lado de guita (`~/src/toolchain-cpp` junto a `~/src/guita`), como documenta su README.
- **Qué versión:** guita fija `v0.1.0`. El CI la toma de una sola variable de su workflow (`TOOLCHAIN_CPP_REF`), y el README y AGENTS.md la nombran. En local, el clon puede estar en esa versión o en una posterior que conserve sus prefijos: lo que importa son los prefijos que nombran los presets.
- **Qué componentes,** según lo que se compila:

  | Para | Componentes |
  |---|---|
  | build nativo y pruebas (también el CI) | `gcc qt libpqxx cpp-httplib protobuf buf` |
  | cliente web | además `qt-wasm protobuf-wasm` |
  | cliente autocontenido | además `qt-static libpq-static` |

- **Subir de versión de `toolchain-cpp`** cambia a la vez `TOOLCHAIN_CPP_REF`, los prefijos de los presets si cambian (sería un MAJOR de `toolchain-cpp`), la imagen del CI si trae otro GCC, el README y AGENTS.md. AGENTS.md lo recoge, en lugar de la regla de las versiones de GCC.

Alternativas descartadas:
- **Un submódulo de git.** Habría una copia de los scripts dentro de cada proyecto, y la toolchain es de la máquina, no de cada clon.
- **Una variable de entorno con la ruta.** Los presets fallarían de forma poco clara cuando no está, y los IDE no siempre heredan el entorno de la shell.

### 2. Los presets, desde `${sourceParentDir}`

`native`, `wasm` y `static` cambian `$env{HOME}/software/` por `${sourceParentDir}/toolchain-cpp/software/` en el compilador, `CMAKE_PREFIX_PATH`, `CMAKE_FIND_ROOT_PATH`, el `toolchainFile` y el entorno de emsdk, y sus descripciones dejan de nombrar `~/software`. `bench` y `wasm-bench` heredan.

No `${sourceDir}/..`, por lo que se vio con el kit WebAssembly (Context). Quien tenga `toolchain-cpp` en otro sitio cambia las rutas en su `CMakeUserPresets.json`, que ya está ignorado.

### 3. Los scripts, con el directorio de instalación de toolchain-cpp por defecto

`build-wasm.sh`, `build-static.sh` y `check-contract.sh` toman `SOFTWARE_DIR="${SOFTWARE_DIR:-<raíz de guita>/../toolchain-cpp/software}"`. La ruta va normalizada (`cd ... && pwd`), sin `..`. `build-static.sh` compara las rutas que resuelve `ldd`, que son reales, con `$SOFTWARE_DIR/*`, y una ruta con `..` no coincidiría nunca: la comprobación dejaría pasar una librería de la toolchain.

Los mensajes que remitían a `build-static-deps.sh` o `setup-qt.sh` remiten a `../toolchain-cpp/install.sh` con sus componentes.

### 4. Fuera los cinco scripts del toolchain

Se borran `build-gcc.sh`, `build-gcc-mingw.sh`, `setup-qt.sh`, `build-deps.sh` y `build-static-deps.sh`. En `scripts/` quedan los de guita:
- `build-wasm.sh`, `build-static.sh` y `run-server.sh`;
- `check-contract.sh` y `check-version.sh`;
- `create-db.sql`.

El bloque de Qt estático compartido con gambasse desaparece de guita: ahora es el componente `qt-static`.

### 5. El CI con toolchain-cpp

En `.github/workflows/ci.yml`:
- **Checkouts:** guita en `guita/` (con todo el historial, para `check-contract.sh`) y `toolchain-cpp` en `toolchain-cpp/`, en `TOOLCHAIN_CPP_REF`, uno al lado del otro. Los pasos se ejecutan en `guita/` (`defaults.run.working-directory`), y la confianza de git se da a esa ruta.
- **Comprobaciones previas:**
  - la versión contra la rama, igual que hoy;
  - el GCC de la imagen contra `toolchain-cpp/versions.sh`, en lugar de contra `build-gcc.sh`.
- **Paquetes del sistema:** los mismos.
- **El GCC de la imagen:** `toolchain-cpp/software/gcc-16.2.0` es un enlace a `/usr/local`, que el componente `gcc` da por instalado.
- **La caché:**
  - `toolchain-cpp/software`, sin ese enlace;
  - su clave lleva `TOOLCHAIN_CPP_REF`, la imagen y los componentes, en lugar del hash de los scripts;
  - se guarda justo después de instalar, como hoy, y antes se quitan `src/` y el entorno de aqt, que solo sirven para instalar.
- **La instalación:** `toolchain-cpp/install.sh qt libpqxx cpp-httplib protobuf buf`.
- **Configurar, compilar, probar y comprobar el contrato,** como hoy.

### 6. Documentación

- **README:**
  - los requisitos de build pasan a ser `toolchain-cpp` v0.1.0 con sus componentes, más los paquetes del sistema;
  - cómo empezar (clonar `toolchain-cpp` al lado, instalar los componentes);
  - los apartados del cliente autocontenido y del compilador cruzado, con `install.sh`.
- **`docs/architecture.md`:**
  - §8 queda con lo que guita exige de su toolchain: los componentes y versiones, la comprobación del compilador al configurar, el run path al `libstdc++` de GCC 16, lo que puede usar el cliente, los presets y la relación de los kits Qt y emsdk. Cómo se construye cada componente pasa a ser un enlace a `docs/toolchain.md` de `toolchain-cpp`;
  - §9.2 y §10.4 se actualizan.
- **AGENTS.md:**
  - el párrafo de los dos compiladores;
  - el del CI;
  - la regla de versiones (decisión 1);
  - el entorno local, que pasa a nombrar `toolchain-cpp` y sus componentes;
  - la regla de un módulo de Qt nuevo, que se añade en `toolchain-cpp`.
- **CHANGELOG.** Como la 0.1.0 no se ha publicado, se reescriben las entradas de `[Unreleased]` que nombran los scripts, para describir lo que traerá: la toolchain viene de `toolchain-cpp`. La entrada del compilador cruzado sale, porque es de `toolchain-cpp`. No se añaden entradas `Removed`, porque nada de esto llegó a publicarse.
- **Al archivar,** el `Purpose` de la spec `toolchain` deja de nombrar el compilador cruzado.

### 7. Verificación

- **Sin rastros de `~/software`:** ningún fichero versionado nombra `~/software` ni `$HOME/software`, salvo los cambios archivados de OpenSpec, que son historia.
- **En local, con los scripts y presets del repositorio:**
  - `cmake --preset native`, la compilación y `ctest --preset native`;
  - `scripts/build-wasm.sh`;
  - `scripts/build-static.sh`, con su comprobación;
  - `scripts/check-contract.sh`.

  Los `CMakeCache.txt` solo nombran rutas de `../toolchain-cpp`, y los ejecutables nativos llevan el run path a su GCC.
- **`build-static.sh --check`** sigue fallando con el cliente nativo, que carga librerías de `toolchain-cpp`, y las nombra.
- **El CI** se prueba lanzándolo a mano sobre la rama, antes del PR: es la primera ejecución en limpio, sin `~/software`.

## Risks / Trade-offs

- [El CI tiene que volver a instalar la toolchain la primera vez] → Es una vez: protobuf con abseil unos minutos, y los kits de aqt. Después reutiliza la caché por la versión.
- [Un clon local de `toolchain-cpp` en otra versión] → Los presets nombran prefijos con versión. Si faltara alguno, la configuración falla nombrando la ruta. El README dice qué versión fijar.
- [La ruta de los checkouts en el CI cambia] → Se ajustan `working-directory`, la confianza de git y las rutas de la caché. `check-contract.sh` sigue teniendo el historial completo de guita.

## Migration Plan

1. Este cambio.
2. El usuario puede borrar de `~/software` lo de guita cuando también haya pasado gambasse (gambasse#65).

Deshacerlo es revertir el cambio: los scripts borrados vuelven, y `~/software` sigue ahí hasta que se borre.
