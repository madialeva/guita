# Design

## Context

Ver proposal.md (Why) y las specs `toolchain` e `integracion-continua`. Lo que condiciona el diseño:

- **El estándar y el compilador:**
  - El `CMakeLists.txt` raíz fija `CMAKE_CXX_STANDARD 23` para todos los builds, con `cmake_minimum_required(VERSION 3.21)`.
  - Ningún preset elige compilador. En local se usa el `g++` del PATH, que es el de gcc-toolset-14 porque lo activa el `.bashrc`. En el CI se usa `CC=gcc-14`/`CXX=g++-14`.
- **`scripts/build-deps.sh`:**
  - Compila con ese mismo compilador por defecto. protobuf y abseil se compilan en C++23 (nativo y WebAssembly) para que coincidan con el proyecto en los tipos `std` que abseil reexporta. libpqxx se compila con el estándar por defecto del compilador.
  - Reutiliza los directorios de compilación `~/software/src/<nombre>/build`, cuya caché de CMake recuerda el compilador con el que se configuraron.
- **La biblioteca estándar del sistema es más antigua que la de GCC 16:**
  - El `libstdc++` de Oracle Linux 9 llega a `GLIBCXX_3.4.29` (GCC 11) y el de GCC 16 es `libstdc++.so.6.0.36`. Un ejecutable compilado con GCC 16 no arranca con el del sistema.
  - Lo mismo pasa con el `protoc` que compila `build-deps.sh`, y que el build ejecuta para generar el contrato.
  - El kit de escritorio de Qt viene precompilado y enlaza el `libstdc++` compartido. Sus herramientas de compilación (moc, qmlcachegen, ...) siguen ejecutándose con el del sistema.
- **GCC 16.2.0 ya está instalado en la máquina de desarrollo**, en `~/software/gcc-16.2.0`, configurado con:
  - `--disable-multilib --enable-languages=c,c++`;
  - el `as` y el `ld` de gcc-toolset-14 (binutils 2.41), fijados con `--with-as`/`--with-ld`.
- **La imagen oficial `gcc:16.2.0-trixie`:**
  - Parte de `buildpack-deps:trixie` y compila GCC 16.2.0 de las fuentes GNU, verificando su firma, con `--disable-multilib --enable-languages=c,c++,fortran,go`, bootstrap completo y `make install-strip` en `/usr/local`.
  - Registra `/usr/local/lib64` con `ldconfig` y trae `git`. Pesa unos 560 MB comprimida.
  - Comprobado: acepta `-std=c++26 -freflection` y compila y ejecuta un prototipo de contenedor DI con reflexión, anotaciones en parámetros y expansion statements. No activa por defecto PIE ni el stack protector, igual que un GCC compilado desde las fuentes en local, a diferencia del g++ de Ubuntu que usa hoy el CI.
- **El cliente WebAssembly en C++26** (comprobado con el Clang 23 de emsdk 5.0.5): el proyecto entero compila y enlaza con `CMAKE_CXX_STANDARD 26` (`-std=gnu++26`), sin avisos.
  - Ese Clang admite el indexado de packs, `= delete("motivo")`, el placeholder `_`, los structured bindings como condición y como pack, `#embed`, los friends variádicos, el placement new en `constexpr` y los mensajes propios de `static_assert`.
  - No admite la reflexión, los expansion statements, los contratos ni las excepciones en `constexpr`.
  - Su libc++ no trae `inplace_vector`, `views::concat`, `function_ref`, `copyable_function`, `optional` como rango, `text_encoding`, `std::simd` ni `<debugging>`. GCC 16 tiene todo eso.
- **El grafo de CMake:** `api` enlaza `core` como `PUBLIC` y el cliente enlaza `api` (modo standalone). Una opción de compilación `PUBLIC` de `core` llegaría al cliente.
- **El CI:**
  - Hoy es un único job, `Build and test`, directamente en la máquina virtual `ubuntu-24.04`. Usa `sudo apt-get`, PostgreSQL en `127.0.0.1` (contenedor de servicio con el puerto publicado) y una caché de `~/software` con la clave `toolchain-ubuntu-24.04-gcc14-<hash de los scripts>`.
  - Un ruleset exige ese job por su nombre. No compila WebAssembly: eso es el issue #12.

## Goals / Non-Goals

**Goals:**
- El mismo compilador en local y en el CI: la misma versión de las fuentes GNU, con las mismas opciones de configuración y los mismos valores por defecto.
- Un único sitio donde los presets y los scripts buscan el compilador, igual que el resto de la toolchain (`~/software`).
- Errores de toolchain al configurar, no a mitad de compilación ni al arrancar un ejecutable.
- Dejar el terreno listo para el cambio de inyección de dependencias sin usar todavía la reflexión.

**Non-Goals:**
- **El contenedor de inyección de dependencias y cualquier uso de la reflexión.** `-freflection` no se activa en ningún target: la configuración solo comprueba que el compilador lo admite. El cambio de DI decidirá en qué targets se activa, sin que llegue al cliente a través de `api` (ver Context).
- **El build WebAssembly en el CI** (#12). Su job tendrá que ejecutarse también en el contenedor de GCC 16, porque `build-deps.sh` compilará con él el `protoc` nativo.
- **Otros compiladores nativos:** gcc-toolset-16 de Red Hat (no ha llegado a Oracle Linux 9; cuando llegue será 16.2.1 con parches de Red Hat, no el compilador del CI) y Clang (sin reflexión fuera del fork de Bloomberg). La comprobación de configuración acepta cualquier GCC 16 con reflexión, pero solo se documenta y se prueba el del proyecto.
- **Cambiar el compilador del cliente web:** sigue siendo el Clang de la emsdk que pide Qt (`QT_EMCC_RECOMMENDED_VERSION`) y solo cambia cuando se actualiza Qt.
- **Empaquetar la aplicación de escritorio** con el `libstdc++` de GCC 16 (§9.2, pendiente). Los ejecutables de desarrollo llevan un rpath absoluto a `~/software` y no son reubicables.
- **El soporte de C++26 en el IDE** (clangd en VS Code): este cambio no usa nada que clangd no entienda.

## Decisions

### 1. GCC 16.2.0 de las fuentes GNU, con las opciones de la imagen oficial: `scripts/build-gcc.sh`

Un script nuevo, con la misma forma que `build-deps.sh` (variables `SOFTWARE_DIR` y `GCC_VERSION`, sin root), hace estos pasos:
- **Descarga** `gcc-$GCC_VERSION.tar.xz` y su firma de `ftpmirror.gnu.org` y **verifica la firma** con el keyring de GNU, como hace la imagen.
- **Añade al árbol de fuentes** GMP, MPFR, MPC e ISL con `contrib/download_prerequisites`, así que no necesita sus paquetes `-devel`.
- **Configura fuera del árbol** con `--prefix=$SOFTWARE_DIR/gcc-$GCC_VERSION --disable-multilib --enable-languages=c,c++`, más `--with-as`/`--with-ld` apuntando al `as` y al `ld` que haya en el PATH al compilarlo.
- **Compila** con bootstrap completo (`make -j$(nproc)`), **instala** con `make install-strip` y borra el directorio de compilación, de varios GB.
- **Si el compilador ya está instalado**, no hace nada, como hace ya con buf.

Solo hay dos diferencias con la imagen, y ninguna afecta al código que genera:
- **Sin `fortran` ni `go`:** no se usan.
- **El `as` y el `ld` quedan fijados:** la imagen usa siempre los de Debian, pero en local el PATH puede cambiar. En la máquina de desarrollo, el `.bashrc` activa gcc-toolset-14, y sin fijarlos GCC 16 pasaría a usar los binutils 2.35 del sistema si un día se quita esa línea.

El CI no ejecuta este script, porque la imagen ya trae ese compilador (decisión 7). La instalación que ya existe en la máquina de desarrollo tiene exactamente estas opciones, así que el script la da por buena.

- *Alternativas*:
  - **gcc-toolset-16:** todavía no está en Oracle Linux 9 (sí en CentOS Stream 9, que no se quiere como base porque no tiene versiones estables), y no sería el mismo compilador que el del CI.
  - **El PPA `ubuntu-toolchain-r/test`:** para 24.04 solo tiene un snapshot de marzo de 2026, anterior a 16.1.
  - **Compilar en un contenedor también en local:** complica ejecutar el cliente de escritorio, las pruebas de GUI en Xvfb y el IDE contra un compilador que no está en la máquina.

### 2. El compilador se busca en `~/software/gcc-16.2.0`, como el resto de la toolchain

- **El preset `native`** fija `CMAKE_CXX_COMPILER` en `$env{HOME}/software/gcc-16.2.0/bin/g++`, junto a las rutas de Qt y de las dependencias que ya tiene. `bench` lo hereda.
- **`build-deps.sh`** compila las dependencias nativas con `$SOFTWARE_DIR/gcc-$GCC_VERSION`. Si no lo encuentra, falla y dice que hay que ejecutar antes `scripts/build-gcc.sh`.
- **En el CI**, la imagen tiene GCC en `/usr/local`. El workflow enlaza `~/software/gcc-16.2.0` → `/usr/local`, y así los presets y los scripts funcionan sin cambios.

La versión de GCC aparece en varios sitios: `build-gcc.sh`, `build-deps.sh`, el preset, la etiqueta de la imagen del CI y la documentación, como ya pasa con Qt 6.12.0. Para que no se separen, el CI comprueba que el GCC de la imagen es el `GCC_VERSION` de `build-gcc.sh` (decisión 7).

- *Alternativas*:
  - **La variable de entorno `CXX`, como hoy en el CI:** depende de cada shell, solo cuenta en la primera configuración de cada directorio de build y se olvida con facilidad.
  - **Un preset propio para el CI:** duplica los presets.
  - **Poner GCC 16 en el PATH desde el `.bashrc`:** cambiaría el compilador de todos los demás proyectos de la máquina.

### 3. C++26 en todos los builds, y CMake 3.25 como mínimo

- **`CMAKE_CXX_STANDARD 26`** en el `CMakeLists.txt` raíz, para el build nativo y para el WebAssembly. Se mantienen las extensiones de GNU (`gnu++26`), como hoy con `gnu++23`.
- **CMake conoce el valor 26 desde la versión 3.25,** así que `cmake_minimum_required` sube de 3.21 a 3.25. La máquina de desarrollo tiene 3.31 y Debian trixie también.

- *Alternativa: C++26 solo en el backend y C++23 en el cliente.* El Clang de emsdk acepta C++26 sin problemas. Lo que limita al cliente no es el flag sino qué puede usar (decisión 8), y un estándar por target complica los targets que comparten código (`contract`, pruebas).

### 4. Comprobación del compilador al configurar

Para el build nativo (no Emscripten), justo después de `project()` y antes de buscar Qt o compilar nada del proyecto:
- **Si el compilador no es GCC 16 o posterior**, `FATAL_ERROR` con el compilador y la versión encontrados, y la indicación de usar el preset `native` con el GCC de `scripts/build-gcc.sh`.
- **Después, `check_cxx_source_compiles`** con `-std=c++26 -freflection` y un programa mínimo que refleja un tipo (`^^int`). Si no compila, `FATAL_ERROR`. Comprueba lo que de verdad se necesita, no un número de versión.

En el build WebAssembly no se comprueba nada: la versión de Emscripten la fija Qt.

- *Alternativa: dejar que falle la compilación.* El primer error sería un fallo de sintaxis en algún fichero, o un ejecutable que no arranca, en lugar de un mensaje que diga qué falta.

### 5. Rpath al `libstdc++` del compilador

- **En el proyecto:** el `CMakeLists.txt` raíz pregunta al compilador dónde está su biblioteca (`g++ -print-file-name=libstdc++.so`) y añade ese directorio a `CMAKE_BUILD_RPATH`. El proyecto no instala nada; sus ejecutables se usan desde `build/<preset>/bin`.
  - El ejecutable carga el `libstdc++` de GCC 16, y las bibliotecas de Qt usan ese mismo, que es compatible hacia atrás con el que esperan.
  - En el CI, el directorio es `/usr/local/lib64`, que ya está en `ldconfig`, así que el rpath no cambia nada.
- **En las dependencias:** `build-deps.sh` compila las nativas con `-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath,<ese directorio>`. Es una opción del enlazador, así que `cmake --install` no la quita de `protoc`, a diferencia del rpath de compilación. libpqxx no instala ejecutables.

- *Alternativas*:
  - **Enlazar `libstdc++` estáticamente** (`-static-libstdc++`): las bibliotecas de Qt cargarían además el `libstdc++` compartido del sistema, y habría dos bibliotecas estándar en el mismo proceso. Las excepciones y los tipos `std` que cruzan la frontera con Qt dejan de ser fiables.
  - **`LD_LIBRARY_PATH` en los presets y los scripts:** cada forma de lanzar un ejecutable (IDE, `scripts/run-server.sh`, CTest, a mano) lo necesitaría, y la spec pide que arranquen sin preparar el entorno.

### 6. Las dependencias, otra vez con GCC 16 y en C++26

- **En `build-deps.sh`:**
  - libpqxx, protobuf y abseil se compilan con `-DCMAKE_CXX_STANDARD=26` y con el compilador de la decisión 2. libpqxx pasa también a fijar el estándar, porque genera en su compilación su configuración de características del compilador, y conviene que coincida con la del proyecto.
  - protobuf para WebAssembly pasa a C++26.
  - cpp-httplib solo instala cabeceras.
- **Directorios de compilación limpios:** el script borra cada uno antes de configurar, porque su caché de CMake recuerda el compilador anterior. Las versiones de las dependencias no cambian y los prefijos de instalación tampoco (`libpqxx-8.0.2`, `protobuf-36.2`, `protobuf-36.2-wasm`); `cmake --install` sobrescribe su contenido.
- *Coste:* el script ya recompilaba todo en cada ejecución, y se ejecuta solo cuando cambia una versión o el compilador. Empezar de cero añade lo que antes ahorraba Ninja cuando nada había cambiado.

### 7. El CI en el contenedor `gcc:16.2.0-trixie`

El job `Build and test` mantiene su nombre (lo exige el ruleset) y su máquina `ubuntu-24.04`, pero sus pasos se ejecutan dentro de `container: gcc:16.2.0-trixie`:
- **PostgreSQL** se alcanza por el nombre del servicio: `GUITA_TEST_DB` pasa a `host=postgres` y sobra publicar el puerto.
- **Paquetes del sistema:** sin `sudo`, porque en el contenedor ya se es root. Se instalan con `apt-get`:
  - lo que la imagen no trae: `cmake`, `ninja-build`, `python3-venv` para el entorno de aqtinstall;
  - las librerías de desarrollo y de ejecución que necesitan el proyecto y Qt (libpq, zlib, GL/EGL, xkbcommon, fontconfig, ...).
  
  Como en #4, la lista exacta se fija al implementar, con la configuración de CMake y `ldd` dentro del contenedor.
- **Compilador:**
  - Desaparecen `CC`/`CXX`.
  - Un paso enlaza `~/software/gcc-16.2.0` → `/usr/local` después de restaurar la caché.
  - Otro paso comprueba que `g++ -dumpfullversion` coincide con el `GCC_VERSION` de `scripts/build-gcc.sh`. Si alguien sube la versión en un sitio y no en el otro, el CI falla.
- **Caché:** la clave pasa a `toolchain-gcc-16.2.0-trixie-<hash de setup-qt.sh y build-deps.sh>`, porque el compilador y el sistema forman parte de lo cacheado.
- **Docker Hub:** según la documentación de GitHub, no limita las descargas de imágenes públicas desde sus runners. Ya se descarga `postgres:18.6` en cada ejecución.

- *Alternativas*:
  - **Compilar GCC en el CI con `build-gcc.sh` y cachearlo:** es exactamente el mismo script, pero una ejecución en frío tarda unos 45 minutos más, y GitHub borra las cachés que no se usan en 7 días.
  - **CentOS Stream 9 con gcc-toolset-16:** 16.2.1 con parches de Red Hat, sobre una base sin versiones estables.
  - **El runner `ubuntu-26.04`:** llega como mucho a GCC 15.2.

### 8. El subconjunto de C++26 del cliente, como regla documentada

- **AGENTS.md y `docs/architecture.md` §8** recogen la regla: el código de `src/client` solo usa lo que admiten GCC 16 y el Clang de emsdk. Incluyen la tabla medida (Context) de lo que ese Clang admite y no admite.
- **Mientras el CI no compile WebAssembly (#12),** un cambio que toque `src/client` compila también el preset `wasm` en local (`scripts/build-wasm.sh`) antes de darse por terminado. AGENTS.md lo pide de forma explícita.
- **La reflexión no puede colarse en el cliente** ni siquiera en nativo, porque `-freflection` no está activado en ningún target. Los expansion statements y la biblioteca nueva sí compilan en el cliente nativo: solo los detecta el build WebAssembly.

- *Alternativa: un target de CMake que compile el cliente nativo sin las extensiones que Clang no tiene.* GCC no tiene un flag que limite C++26 al subconjunto de otro compilador. La comprobación real es compilar con Clang, que es lo que hará #12.

### 9. Documentación

- **AGENTS.md:**
  - estándar y compiladores;
  - la regla del cliente;
  - entorno local: GCC 16.2.0 con `scripts/build-gcc.sh`, con el compilador del sistema solo para compilarlo, y CMake 3.25;
  - el párrafo del CI;
  - protobuf y abseil en C++26.
- **README:** el badge de C++26, la tabla de requisitos y los pasos de instalación (`scripts/build-gcc.sh` antes de `build-deps.sh`).
- **`docs/architecture.md`:**
  - §3: el proyecto es C++26;
  - §8: tabla de la toolchain, notas sobre el compilador y el rpath, y la tabla del subconjunto del cliente;
  - §10.4: el contenedor, el enlace del compilador, la comprobación de versión y la clave de la caché.
- **CHANGELOG:** una entrada en `Added`, porque nada publicado usaba C++23 y la primera versión se describe tal como sale. Dirá que el código es C++26 y que `scripts/build-gcc.sh` compila GCC 16 sin root.

## Risks / Trade-offs

- **[El soporte de C++26 en GCC 16 es experimental, y la ABI de sus partes de C++20 y C++26 puede cambiar entre versiones]** → Todo lo que enlaza el proyecto se compila desde las fuentes con el mismo compilador. Al subir de versión de GCC se recompilan las dependencias, y en el CI cambia la clave de la caché.
- **[Qt 6.12 está precompilado con otro GCC]** → `libstdc++` es compatible hacia atrás y el rpath hace que todo el proceso use el de GCC 16. El primer grupo de tareas compila el proyecto entero y pasa `ctest` con GCC 16 antes de tocar el CI.
- **[Las cabeceras de Qt, protobuf o abseil en C++26 pueden dar avisos de deprecación]** → El proyecto no compila con `-Werror`. Se corrigen los avisos del código propio y se anotan los de terceros.
- **[Compilar GCC lleva entre media hora y una hora]** → Solo una vez por versión. La máquina de desarrollo ya lo tiene.
- **[La versión de GCC repetida en varios sitios]** → El CI compara la imagen con `build-gcc.sh`. AGENTS.md pide cambiarla en todos a la vez.
- **[Rpath absoluto a `~/software`]** → Solo afecta a los builds de desarrollo. El empaquetado (§9.2) tendrá que distribuir el `libstdc++` de GCC 16 o decidir otra cosa.
- **[El GCC local depende de los binutils de gcc-toolset-14]** → El script fija los que haya en el PATH y su cabecera lo explica. Si se desinstala gcc-toolset-14, hay que volver a compilar GCC.
- **[Un incidente de Docker Hub puede devolver 429 al descargar la imagen]** → Iniciar sesión con una cuenta gratuita (`container.credentials`) o copiar la imagen a `ghcr.io`, si llega a pasar.
- **[El CI no detecta que el cliente use algo que el Clang de emsdk no tiene]** → La regla de compilar `wasm` en local al tocar el cliente, hasta #12.
- **[Los nombres de los paquetes de Debian trixie no son los de Ubuntu 24.04]** → La lista se comprueba dentro del contenedor al implementar.

## Migration Plan

En la máquina de cada desarrollador:

1. `scripts/build-gcc.sh`: no hace nada si `~/software/gcc-16.2.0` ya está.
2. `scripts/build-deps.sh`: vuelve a compilar libpqxx, protobuf y abseil (nativo y WebAssembly) con GCC 16 y C++26.
3. Borrar `build/native`, `build/bench`, `build/wasm` y `build/wasm-bench`, y volver a configurar con los presets.
4. `ctest --preset native` y `scripts/build-wasm.sh`.

El CI cambia con el propio PR: la primera ejecución no encuentra caché con la clave nueva e instala la toolchain dentro del contenedor.

*Vuelta atrás:* revertir el cambio y volver a ejecutar el `build-deps.sh` anterior, que compila con el GCC del PATH.
