# Design

## Context

Ver proposal.md (Why) y la spec `integracion-continua`. Lo que condiciona el diseño:

- **La batería ya está preparada para un CI** (#1): con `GUITA_TEST_DB` definida, `tests/postgres-fixture.sh` solo escribe la cadena de conexión y no arranca ningún contenedor; cada suite crea y borra su propia base de datos, y las pruebas corren sin pantalla (`QT_QPA_PLATFORM=offscreen`) y en paralelo.
- **Las suites del contrato ya están en CTest:** `contract_lint`, `contract_format` (`buf`) y `contract`. Solo falta `scripts/check-contract.sh`, que necesita la rama `origin/develop/vX.Y.Z` en el clon.
- **Los presets buscan todo en `$HOME/software`** (`CMAKE_PREFIX_PATH` del preset `native`), donde lo instalan los scripts.
- **`scripts/setup-qt.sh` instala siempre los dos kits de Qt, emsdk y Python 3.12 con uv.** El CI solo necesita el kit de escritorio: el resto son unos 2 GB y varios minutos sin uso, y uv no está en el runner.
- **`scripts/build-deps.sh` recompila siempre** libpqxx y protobuf con abseil, aunque ya estén instalados. Solo se salta lo que ya está clonado y buf si ya existe. Sin emsdk omite la protobuf de WebAssembly con un aviso. Protobuf con abseil es, con diferencia, lo más lento.
- **En local se compila con GCC 14** (gcc-toolset-14 en EL9). protobuf y abseil se compilan en C++23 para que coincidan con el proyecto en los tipos `std` que abseil reexporta, así que el compilador forma parte de lo que hay que cachear.
- **Qt 6.12 de escritorio necesita en tiempo de ejecución** librerías del sistema: EGL/GL, fontconfig, freetype, glib, xkbcommon, X11, krb5, zstd y brotli. El plugin `offscreen` no necesita un servidor X, pero `libQt6Gui` enlaza esas librerías de todos modos.
- **El repositorio es público**, así que los minutos de los runners Linux estándar son gratuitos y los rulesets de rama están disponibles.

## Goals / Non-Goals

**Goals:**
- Un único job que reproduce lo que hace el desarrollador en local: los mismos scripts, presets y orden de CTest.
- Una ejecución en caliente de pocos minutos. Cuando cambian los scripts, una reinstalación completa que no se repite si las pruebas fallan.
- Comprobaciones que también se pueden ejecutar en local con los mismos scripts.

**Non-Goals:**
- El build WASM y las mediciones de `tests/bench` (Release, fuera de `ctest`), como dice el issue. El build WASM queda como posible ampliación.
- Otras plataformas (Windows, macOS) y una matriz de compiladores o de versiones de Qt: el proyecto fija Qt 6.12 y GCC.
- Publicar artefactos (binarios, el directorio web) o desplegar.
- Ejecutar las pruebas de GUI en Xvfb: la batería no tiene pruebas de GUI interactivas.
- Que el agente configure el ruleset de la rama en GitHub: lo configura el propietario del repositorio.

## Decisions

### 1. Un job en `ubuntu-24.04` con GCC 14

El workflow `.github/workflows/ci.yml` tiene un solo job en el runner `ubuntu-24.04`, con `CC=gcc-14` y `CXX=g++-14`, que vienen en la imagen del runner. Instala con `apt` lo que el proyecto toma del sistema:
- `ninja-build`, `libpq-dev` y `zlib1g-dev`;
- las librerías que Qt necesita para enlazar y ejecutarse sin pantalla (los `-dev` de GL/EGL y xkbcommon que pide su configuración de CMake, y las de ejecución que falten en la imagen).

La lista exacta se fija al implementar, comprobando con `ldd` y con la configuración de CMake en el runner.

- *Alternativas*:
  - **Un contenedor EL9** (como la máquina de desarrollo): reproduce mejor el entorno local, pero el job dentro de un contenedor alcanza al PostgreSQL de servicio por otro nombre de host, la imagen hay que prepararla y la caché depende de ella. Además, compilar en otra familia de distribuciones detecta dependencias implícitas del entorno local.
  - **El GCC 13 por defecto de Ubuntu 24.04:** cumple el mínimo documentado (13), pero no es lo que se usa en local. Comprobar el mínimo sería una matriz, que queda fuera.

### 2. La toolchain con los scripts del repositorio, con un modo solo escritorio en `setup-qt.sh`

- **`setup-qt.sh --desktop-only`** instala solo el kit `linux_gcc_64` con sus módulos (`qttasktree`). No instala el kit WASM, emsdk ni Python con uv, así que no necesita uv. Sin la opción, el script hace lo mismo que hoy.
- **`build-deps.sh` se usa tal cual:** sin emsdk ya omite la protobuf de WebAssembly. El aviso que imprime es esperado en el CI.
- **`SOFTWARE_DIR` no se cambia:** el valor por defecto `~/software` es justo donde miran los presets.

- *Alternativas*:
  - **Instalar Qt con una acción de terceros** (`jurplel/install-qt-action`): duplica lo que hace `setup-qt.sh` con aqtinstall y añade una dependencia externa al CI. Además, el CI dejaría de probar los scripts que se documentan a los desarrolladores.
  - **Una variable de entorno en lugar de la opción:** el script no tiene más opciones y una opción se ve mejor en `--help`/la cabecera. Se documenta en su cabecera junto a las variables.

### 3. Caché de `~/software` guardada justo después de instalar

- **Restauración:** `actions/cache/restore` con la clave `toolchain-<sistema del runner>-gcc14-<hashFiles(scripts/setup-qt.sh, scripts/build-deps.sh)>`, sin claves parciales. Con una caché parcial, `build-deps.sh` recompilaría todo igualmente y `setup-qt.sh` podría dar por bueno un kit de otra versión.
- **Si no hay acierto**, se ejecutan `setup-qt.sh --desktop-only` y `build-deps.sh`, se borran `~/software/src` (los fuentes y los directorios de compilación, que no hacen falta) y el entorno virtual de aqtinstall, y `actions/cache/save` guarda la caché en ese mismo momento. Así, una ejecución con pruebas fallidas no pierde la instalación.
- **Si hay acierto**, no se ejecuta ningún script de instalación.

- *Alternativas*:
  - **`actions/cache` en un solo paso:** guarda al final del job y solo si el job termina bien. Un PR con pruebas en rojo recompilaría protobuf en cada ejecución.
  - **Clave por variables de versión en lugar del hash de los scripts:** sería más precisa con los cambios de comentarios, pero no detectaría cambios en las opciones de compilación (por ejemplo, de abseil), que también obligan a reinstalar.
  - **Cachear el directorio `build/native`** (ccache o el propio árbol de compilación): el proyecto compila en poco tiempo comparado con la toolchain, y cachear el árbol de compilación puede ocultar problemas de una compilación limpia.

### 4. PostgreSQL como contenedor de servicio

El job declara el servicio `postgres:18.6`:
- contraseña fija del runner en `POSTGRES_PASSWORD`;
- puerto 5432 publicado en el runner;
- *health check* con `pg_isready`, para que el job no empiece hasta que acepte conexiones.

`GUITA_TEST_DB=host=127.0.0.1 port=5432 user=postgres password=… dbname=postgres` se define en el job. La contraseña no es un secreto: el servidor solo existe durante el job y solo es accesible desde el runner. Es lo previsto en #1 (`docs/architecture.md` §10.4).
- *Alternativa*: dejar que la fixture arranque el contenedor con Docker, que también existe en el runner. Funcionaría, pero no ejercita el camino de `GUITA_TEST_DB`, y el contenedor de servicio deja su registro en la interfaz de Actions.

### 5. Pasos del job y su orden

1. `actions/checkout` con `fetch-depth: 0`, para tener `origin/develop/vX.Y.Z` y el historial que necesita `buf breaking`. En un PR, GitHub hace el checkout del commit de fusión.
2. `scripts/check-version.sh` contra la rama de referencia: `github.base_ref` en un PR y `github.ref_name` en un push. En una ejecución manual no hay rama de referencia y el paso no se ejecuta. Va primero porque es instantáneo.
3. Paquetes del sistema y toolchain (decisiones 1 a 3).
4. `cmake --preset native`, `cmake --build --preset native` y `ctest --preset native -j"$(nproc)"`. El preset ya muestra la salida de las suites que fallan.
5. `scripts/check-contract.sh` sin argumentos: el propio script deduce `origin/develop/vX.Y.Z` de la versión de `CMakeLists.txt`.

`check-contract.sh` se ejecuta siempre, no solo cuando el PR toca `src/proto/`. Tarda un segundo, y filtrar por rutas exigiría una acción de terceros o lógica propia con `git diff`. En un push a la propia `develop/vX.Y.Z` compara el contrato consigo mismo y pasa; en un PR o en `release/vX.Y.Z` compara con la rama de desarrollo, que es lo que importa.

Además:
- `concurrency` agrupa por workflow y rama (o PR) con `cancel-in-progress`;
- `permissions: contents: read`;
- `timeout-minutes` cubre la ejecución en frío, con la compilación de protobuf.

Las acciones oficiales se fijan por versión mayor.

### 6. `scripts/check-version.sh`

`scripts/check-version.sh <rama>`:
- lee la versión de `project(... VERSION ...)` con la misma expresión que `check-contract.sh`;
- si la rama es `develop/vX.Y.Z` o `release/vX.Y.Z`, falla con un mensaje que muestra las dos versiones cuando no coinciden;
- con cualquier otra rama no tiene con qué comparar, lo dice y termina bien.

Se prueba en CTest con las pruebas `unit` `version_check_*`:
- con las ramas `develop/` y `release/` de la versión actual pasa;
- con otra versión falla, y la prueba exige su mensaje (`PASS_REGULAR_EXPRESSION`) para no darse por buena si el script falla por otro motivo;
- con una rama de cambio pasa.

Las versiones se derivan de `PROJECT_VERSION` para que las pruebas no haya que tocarlas en cada ciclo.
- *Alternativa*: escribir la comprobación en línea en el YAML. No se podría ejecutar en local ni probar.

### 7. Exigir el CI para fusionar

Que un PR no se pueda fusionar en rojo es una regla de GitHub, no del workflow: un ruleset sobre `develop/**` y `release/**` que exige el check del job. La rama de un PR debe además estar al día con la de destino. El nombre del check debe ser estable, así que el job tiene un nombre fijo.

`docs/architecture.md` §10.4 documenta la regla y la configura el propietario del repositorio durante la verificación, una vez que el check ha aparecido al menos una vez, porque GitHub solo ofrece checks que ya conoce.

### 8. Badge y documentación

El README muestra el badge `actions/workflows/ci.yml/badge.svg` sin parámetro de rama, que muestra el estado de la rama por defecto (`develop/vX.Y.Z`), así que no hay que cambiarlo en cada ciclo. Además:
- la fila "Continuous integration" del README pasa a ✅;
- §10.4 de la arquitectura describe el workflow real;
- §11 deja de listarlo como pendiente;
- §3 y AGENTS.md pasan del futuro al presente.

## Risks / Trade-offs

- **[Faltan librerías del sistema que Qt necesita en el runner]** → Se detectan en la primera ejecución (CMake al configurar o el cargador al lanzar una prueba) y se añaden a la lista de `apt`. La lista queda en el workflow, no en la documentación.
- **[La primera ejecución, o cualquiera tras cambiar los scripts, tarda bastante (Qt y protobuf con abseil)]** → Solo ocurre cuando cambian los scripts y no se repite aunque fallen las pruebas (decisión 3). El `timeout-minutes` la cubre.
- **[La caché ocupa espacio del límite de 10 GB del repositorio]** → Solo el kit de escritorio y las librerías instaladas, sin fuentes; GitHub expulsa las entradas antiguas. Cada versión de los scripts es una entrada.
- **[Las mirrors de Qt o GitHub fallan durante una instalación en frío]** → La ejecución se repite; con la caché ya guardada, las siguientes no descargan nada.
- **[Una prueba depende del tiempo o del orden y falla en el runner, más lento y con menos núcleos]** → Se investiga como un fallo real; el CI existe precisamente para encontrarlos. No se añaden reintentos automáticos.
- **[El job compila en Ubuntu y en local se compila en EL9]** → Es intencionado (decisión 1): detecta dependencias implícitas del entorno local. Un fallo que solo ocurre en uno de los dos se corrige en el proyecto, no en el workflow.
- **[El check de contrato en un push a `develop/vX.Y.Z` se compara consigo mismo]** → Esa rama solo recibe merges de PR, que ya se comprobaron contra ella.

## Migration Plan

No hay migración de datos ni de API. El despliegue es:
1. fusionar el PR;
2. que el propietario cree el ruleset (decisión 7).

Revertir es borrar el workflow y el ruleset. El modo `--desktop-only` y `check-version.sh` no cambian nada de lo que ya existe.
