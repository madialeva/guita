# Design

## Context

La motivación está en proposal.md (Why) y los requisitos en la spec `cliente-estatico`. Lo que condiciona el diseño, comprobado en la máquina de desarrollo:

- **De qué depende hoy el cliente nativo** (`ldd build/native/bin/client`):
  - Once librerías `libQt6*.so` y la ICU 73 que trae el kit, desde `~/software/Qt/6.12.0/gcc_64/lib`.
  - `libstdc++` y `libgcc_s` de `~/software/gcc-16.2.0/lib64`, por el `CMAKE_BUILD_RPATH` del `CMakeLists.txt` raíz.
  - `libpq.so.5` de `/usr/pgsql-17/lib` (los paquetes de PGDG), que arrastra Kerberos, LDAP, SASL, OpenSSL y otras.
  - Ya son estáticas `core`, `api`, `contract`, libpqxx, protobuf con abseil y cpp-httplib (solo cabeceras, con la zlib del sistema).
  - Qt carga además en tiempo de ejecución los plugins de plataforma (xcb, wayland) y su integración con OpenGL, el de TLS y los módulos QML que importan las vistas: `QtQuick`, `QtQuick.Controls` y `QtQuick.Layouts`. Los busca en el kit y en el `qt.conf` que el build deja junto al ejecutable.
- **El kit de escritorio de aqt es solo compartido.** Los `.a` de `gcc_64/lib` son piezas internas (`BundledLibpng`, `QmlDom`, ...), no un Qt estático. Hay que compilarlo.
- **El grafo de CMake.** El cliente nativo siempre enlaza `api` (modo standalone), y de ahí `core` y libpqxx. libpqxx encuentra libpq con `find_dependency(PostgreSQL)` (el `FindPostgreSQL` de CMake) y enlaza `PostgreSQL::PostgreSQL`.
- **El `libstdc++` de GCC 16.** §8 de `docs/architecture.md` explica por qué hoy no se enlaza estático: quedarían dos librerías estándar en el proceso, la estática y la compartida que carga Qt. Con Qt estático ese problema desaparece.
- **Las fuentes de Qt 6.12.0.** download.qt.io publica un tarball por módulo en `official_releases/qt/6.12/6.12.0/submodules/`, sin firma. Los tarballs y el `md5sums.txt` del directorio redirigen a un mirror. En cambio, `<fichero>.sha256` lo sirve el propio download.qt.io (MirrorBrain), sin redirigir. Tamaños: qtbase 48 MB, qtdeclarative 37 MB, qttools 10 MB, qtshadertools 1 MB y qtwebsockets menos de 1 MB.
  - El `dependencies.yaml` de qtdeclarative 6.12.0 solo exige qtbase; qtshadertools, qtsvg, qtimageformats y qtlanguageserver son opcionales.
  - qttasktree no es dependencia de qtdeclarative. `setup-qt.sh` la instala porque el `QmlAssetDownloader` precompilado de aqt se compiló con ella.
- **Las fuentes de PostgreSQL 18.6** están en ftp.postgresql.org, con su `.sha256`, y siguen trayendo `configure` (autoconf) además de meson. Los paquetes de PGDG no traen `libpq.a`.
- **La máquina:**
  - Oracle Linux 9.7, glibc 2.34, sesión X11, 12 núcleos, 61 GB de RAM y 82 GB libres.
  - Ya tiene los `-devel` de OpenSSL 3.5, libxcb, libxkbcommon, fontconfig, freetype, Mesa (GL y EGL), libglvnd, Wayland, X11, D-Bus y zlib.
  - Le faltan los de xcb-util (`xcb-util`, `-wm`, `-image`, `-keysyms`, `-renderutil`, `-cursor`) y `libxkbcommon-x11`. Sus librerías de ejecución sí están, porque las usa el plugin xcb del kit actual.
- **El cliente** elige las traducciones por `QLocale()`, embebidas en `:/i18n`. El modo standalone se conecta al PostgreSQL de `--db` o `DB_CONN`.

## Goals / Non-Goals

**Goals:**
- Un ejecutable que arranque en otro Linux con glibc 2.34 o posterior y un escritorio X11, sin instalar nada del proyecto.
- Las dependencias estáticas, compiladas desde las fuentes con opciones fijadas en el script, de modo que el resultado no dependa de qué `-devel` haya instalados.
- Que el build habitual no note nada: ni el preset `native`, ni las pruebas, ni el CI, ni el trabajo de cada cambio.
- Que el código del cliente y del backend no cambie; solo la forma de enlazarlo.

**Non-Goals:**
- **Compilar el build estático en el CI.** Puede venir más adelante, con la caché de las dependencias como en §10.4.
- **Wayland nativo.** El ejecutable usa X11, que también funciona bajo XWayland. Wayland estático necesita tres familias de plugins más (integración de shell, decoraciones e integración gráfica).
- **Windows y macOS.**
- **Empaquetar:** AppImage, fichero `.desktop`, icono, ni los textos de licencia junto al ejecutable (§12 dice qué haría falta).
- **PostgreSQL embebido** para el modo standalone (§11).
- **OpenSSL o glibc estáticas**, y sistemas con una glibc anterior a la 2.34. Para eso habría que compilar en una distribución más antigua.
- **Ejecutar la batería de pruebas contra el Qt estático:** las pruebas siguen con el kit compartido.
- **Reducir el tamaño** con LTO, `--gc-sections` o recortando más funcionalidades de Qt. El tamaño se mide y se documenta.

## Decisions

### 1. Dos scripts aparte: `build-static-deps.sh` y `build-static.sh`

- **`scripts/build-static-deps.sh` compila las dependencias estáticas:** el Qt estático (decisiones 2 a 4) y la libpq estática (decisión 5). Tiene la misma forma que `build-deps.sh`: variables `SOFTWARE_DIR`, `GCC_VERSION`, `QT_VERSION` y `POSTGRESQL_VERSION`, sin root, fuentes en `$SOFTWARE_DIR/src`. Exige el GCC de `build-gcc.sh`, con el mismo mensaje de error, y no hace nada con lo que ya esté instalado.
- **`scripts/build-static.sh` genera el ejecutable** (decisiones 6 a 8), como `build-wasm.sh` genera el cliente web.

Alternativas descartadas:
- **Una opción más en `build-deps.sh`.** Ese script lo ejecutan el CI y cualquier instalación nueva. Esto es opcional y tarda media hora, así que mejor fuera.
- **Un único script.** Las dependencias se compilan una vez y el ejecutable se regenera cada vez que se quiere probar.

### 2. Qt desde los tarballs de cada módulo, comprobados con su SHA-256

Por cada módulo, el script:
- descarga `<módulo>-everywhere-src-6.12.0.tar.xz` de `download.qt.io/official_releases/...`, que redirige a un mirror;
- comprueba su SHA-256 con el `<tarball>.sha256` que sirve el propio download.qt.io;
- lo descomprime en `~/software/src/qt-6.12.0/`.

Los tarballs y las fuentes descomprimidas se quedan ahí para recompilar más adelante. Todos se comprueban antes de compilar nada.

La suma no es una firma: detecta una descarga corrupta o un mirror alterado, pero no protege si download.qt.io estuviera comprometido. Qt no firma estos ficheros. Para una prueba local es suficiente: es el mismo servidor del que aqt baja el kit actual. El diseño decía al principio MD5 con el `md5sums.txt` del directorio; al implementar se vio que ese fichero también redirige al mirror y que el `.sha256` no.

Alternativas descartadas:
- **`qt-everywhere-src-6.12.0`:** cerca de 1 GB con unos 40 módulos, de los que se usan cinco.
- **Clonar de code.qt.io o del mirror de GitHub:** devolvió 503 durante la exploración, y lo que Qt publica como release son los tarballs.

### 3. Cinco módulos, compilados en orden contra el prefijo instalado

1. Se compila e instala **qtbase** en `~/software/Qt/6.12.0-static` (`configure -static -release -prefix ...`, decisión 4).
2. Le siguen **qtshadertools, qtdeclarative, qtwebsockets y qttools**. Cada uno se configura con el `qt-configure-module` de ese prefijo, se compila y se instala.

Ninguno compila ejemplos ni pruebas. Los directorios de compilación se borran al acabar cada módulo.

Por qué cada módulo:
- **qtshadertools:** es opcional para qtdeclarative, pero Qt Quick la usa para sus shaders.
- **qttools:** solo para las herramientas de Linguist (`lupdate`, `lrelease`). El `CMakeLists.txt` raíz exige `LinguistTools` y las traducciones van embebidas en el ejecutable. Sus aplicaciones gráficas (Designer, Assistant, Linguist) necesitan Widgets y quedan fuera solas.
- **Fuera:** qttasktree, qtsvg, qtimageformats y qtlanguageserver, que el cliente no usa.

### 4. La configuración de qtbase, fijada en el script

Cada opción se fija de forma explícita, para que el resultado no cambie según los paquetes instalados. Es el mismo criterio que sigue `build-deps.sh` con cpp-httplib.

- **Compilador.** El GCC 16 del proyecto (`CMAKE_C_COMPILER`, `CMAKE_CXX_COMPILER`). Las herramientas de Qt que se ejecutan al compilar (moc, rcc, qmlcachegen, lrelease, ...) llevan un `-Wl,-rpath` al `libstdc++` de ese GCC, como `protoc`.
- **Estándar de C++.** El que use Qt por defecto, sin forzar C++26. El ABI de Qt no depende del estándar de la aplicación, y así se usa ya el kit compartido. Con abseil es distinto: reexporta tipos `std` según el estándar (§8).
- **Plataforma.** xcb con su integración GLX y EGL, sin Wayland.
- **Copias propias de Qt** de PCRE2, HarfBuzz, libpng, libjpeg, double-conversion y libb2. No están en todos los escritorios, o cambian de versión entre distribuciones (`libpcre2-16`, por ejemplo, falta a menudo).
- **Del sistema** (y por tanto dinámicas):
  - zlib, que cpp-httplib ya enlaza;
  - fontconfig y freetype. fontconfig carga la freetype del sistema, así que una copia propia dejaría dos en el proceso;
  - OpenGL/EGL, X11, xcb y xkbcommon.
- **Desactivados:**
  - ICU (`-no-icu`). Qt trae sus propios datos de CLDR para `QLocale`, el cliente no ordena texto con `QCollator`, y la ICU ocupa unos 30 MB;
  - GLib (`-no-glib`). Qt usa entonces su propio bucle de eventos;
  - Widgets (`-no-widgets`), que el proyecto no usa (AGENTS.md);
  - SQL y PrintSupport.
- **Cargados en tiempo de ejecución, sin dependencia directa:**
  - OpenSSL (`-openssl-runtime`): el plugin de TLS carga `libssl.so.3` para `wss://`;
  - D-Bus: accesibilidad y portales del escritorio.

Los nombres exactos de las opciones se comprueban con el `configure -list-features` de 6.12.0 al implementar. Si alguna funcionalidad desactivada resulta necesaria para un módulo de los cinco, se activa y se anota aquí. Por ejemplo, si qttools no compilara sin Widgets: Widgets se compilaría con Qt, pero el cliente no lo enlazaría.

Ajustes al implementar, contrastados con `config_help.txt`, las funcionalidades de los `configure.cmake` y el `config.summary` del build:
- **D-Bus.** `-dbus-runtime` no llega a CMake en 6.12.0 (el resumen seguía diciendo "directly linked to libdbus"). Se usa la funcionalidad directamente: `-no-feature-dbus-linked`.
- **Opciones que sobraban.** `-no-cups` y `-no-gtk` no tienen efecto (CMake avisa de que no usa esas variables), porque CUPS cuelga de PrintSupport y el tema GTK de Widgets, y los dos ya están desactivados.
- **Fijadas además**, porque si no dependen de lo instalado:
  - Wayland: en 6.12.0 su plugin de cliente está en qtbase, y se desactiva con `-no-feature-wayland`;
  - los backends empotrados (`-no-eglfs -no-linuxfb -no-kms -no-gbm`, VNC, `vkkhrdisplay`) y sus entradas (`libudev`, `libinput`, `mtdev`, `tslib`, `evdev`);
  - Vulkan y la gestión de sesión de X11 (`xcb-sm`, que enlazaría libSM e libICE);
  - XInput2 y md4c, con las copias de Qt;
  - libproxy y journald.
- **Widgets desactivado no ha dado problemas.** qttools compila sus herramientas de Linguist sin él, y desactiva solo sus aplicaciones gráficas. Las desactivadas de forma explícita son Assistant, Designer, qdoc, clang, qdbus, qtdiag, qtplugininfo, pixeltool, distancefieldgenerator, kmap2qmap y qev.

### 5. libpq de PostgreSQL 18.6, solo el cliente

- **Fuentes.** El script descarga `postgresql-18.6.tar.bz2` y su `.sha256` de ftp.postgresql.org, comprueba la suma y descomprime en `~/software/src/postgresql-18.6`.
- **Compilación.** Con `configure` y el GCC del proyecto, se compila e instala solo la parte cliente: `src/include`, `src/interfaces/libpq`, `src/common`, `src/port` y `src/bin/pg_config`. Es la instalación de solo cliente que describe la documentación de PostgreSQL.
  - Opciones: `--with-ssl=openssl --without-icu --without-readline --without-zlib`. GSSAPI y LDAP ya están desactivados por defecto.
  - El resultado va a `~/software/libpq-18.6-static`, y luego se borran las `libpq.so*` instaladas. Así `FindPostgreSQL` solo puede encontrar `libpq.a`.
- **Por qué 18.6:** es la versión del servidor de pruebas (`postgres:18.6`). libpq habla el mismo protocolo con el servidor 17 local, y libpqxx 8.0.2, compilada con las cabeceras de libpq 17, solo usa funciones que siguen en 18.
- **Qué necesita para enlazar.** `libpq.a` necesita además `libpgcommon_shlib.a` y `libpgport_shlib.a`, las variantes PIC con las que se compila la libpq compartida (el cliente es PIE), y OpenSSL (`libssl`, `libcrypto`).
- **Por qué meson no:** compila el árbol entero y su `install` no separa la parte cliente.

Alternativas descartadas:
- **Dejar la libpq del sistema.** Arrastra Kerberos, LDAP y SASL, y vive en `/usr/pgsql-17`, una ruta de PGDG que no existe en otras distribuciones.
- **Una `libpq.a` de los paquetes.** PGDG no la trae.

### 6. La opción `GUITA_STATIC` en CMake

Apagada por defecto, así que el build nativo no cambia. Con ella encendida:

- **Kit estático obligatorio.** Tras `find_package(Qt6)`, la configuración falla si `Qt6::Core` no es `STATIC_LIBRARY`. El mensaje dice qué `Qt6_DIR` encontró y que el Qt estático se espera en `~/software/Qt/6.12.0-static` (`scripts/build-static-deps.sh`).
- **Runtime de C++ estático.** Los ejecutables enlazan con `-static-libstdc++ -static-libgcc` y no se añade el directorio del `libstdc++` a `CMAKE_BUILD_RPATH`.
- **libpq estática.** En `src/backend/CMakeLists.txt`, tras `find_package(libpqxx)`:
  - se comprueba que `PostgreSQL_LIBRARY` es un `.a`;
  - se buscan `pgcommon_shlib` y `pgport_shlib` junto a él;
  - se añaden, con `OpenSSL::SSL` y `OpenSSL::Crypto`, a `INTERFACE_LINK_LIBRARIES` de `PostgreSQL::PostgreSQL`.

  `core` y libpqxx no cambian.
- **Plugins de Qt.** En `src/client/CMakeLists.txt`, `qt_import_plugins(client ...)` fija:
  - los de plataforma: xcb y sus integraciones GLX/EGL;
  - el de TLS: OpenSSL;
  - ninguno de formatos de imagen, que el cliente no usa.

  Los plugins de los módulos QML los enlaza `qmlimportscanner` al finalizar el ejecutable, el mismo mecanismo que usa el build WebAssembly. Los nombres exactos de los targets se comprueban en el kit instalado.

Alternativa descartada: **detectar el Qt estático sin opción.** El preset lo dice de forma explícita, y la opción permite fallar con un mensaje claro si se encuentra el kit equivocado.

### 7. Los presets `static`

- **El de configuración** (`build/static`, Release):
  - fija el GCC 16 como compilador;
  - pone `CMAKE_PREFIX_PATH` con el Qt estático y la libpq estática delante, y luego libpqxx, cpp-httplib y protobuf;
  - activa `GUITA_STATIC=ON` y desactiva `BUILD_TESTING`.

  No hereda de `native`: cambian el Qt y el tipo de build, y heredar pondría el kit compartido en la ruta de búsqueda.
- **El de compilación** solo compila `client`.

buf no hace falta, porque solo lo usan las pruebas del contrato.

### 8. `scripts/build-static.sh`: generar y comprobar

1. Falla si faltan el Qt estático o la libpq estática, con un mensaje que remite a `build-static-deps.sh`.
2. Ejecuta `cmake --preset static` y `cmake --build --preset static`.
3. Copia el ejecutable a `build/dist/client` (dentro de `build/`, ignorado por git) y le quita los símbolos con `strip`.
4. Comprueba el resultado, y falla nombrando lo que sobra si:
   - `readelf -d` muestra un `RUNPATH` o un `RPATH`;
   - alguna librería `NEEDED` es de Qt, ICU, `libstdc++`, `libgcc_s`, libpq, protobuf o abseil;
   - `ldd` resuelve alguna librería dentro de `$SOFTWARE_DIR` o del repositorio, o no encuentra alguna.
5. Muestra las librerías `NEEDED` y el tamaño del ejecutable.

`readelf` mira las dependencias directas sin ejecutar nada. `ldd` cubre las que se resuelven por transitividad.

### 9. Verificación a mano, fuera de `ctest`

La spec exige que el build nativo, las pruebas y el CI no necesiten el kit estático, así que no hay una suite para esto. Es el mismo caso que `tests/web/dead-keys.mjs` y las mediciones. La verificación, que queda descrita en §9.2, es:

- **La comprobación de `build-static.sh`** (decisión 8).
- **En esta máquina, sobre Xvfb,** con el ejecutable copiado a un directorio temporal y lanzado con `env -i` (solo `DISPLAY`, `HOME` y `LANG`):
  - en modo conectado contra `scripts/run-server.sh`;
  - en modo standalone contra la base de datos de desarrollo;
  - con `LANG=es_ES.UTF-8` y `LANG=C`.

  Las capturas se comparan con las del cliente nativo: mismo estilo de controles, mismas vistas y mismas traducciones.
- **En un contenedor `ubuntu:24.04`** (glibc 2.39, otra distribución, sin `~/software`), con Xvfb y solo los paquetes de ejecución de X11, OpenGL y fontconfig. El ejecutable se monta de solo lectura y se usa la red del host para llegar al servidor. La lista de paquetes que hizo falta queda en §9.2 como requisitos del equipo destino.

### 10. Documentación y licencias

- **`docs/architecture.md`:**
  - §8: el Qt estático y la libpq estática en la tabla, con sus opciones;
  - §9.2: el ejecutable estático, qué sigue siendo dinámico y cómo se comprueba;
  - §12: el escritorio estático tiene la misma obligación de la LGPL que el cliente web (permitir enlazarlo con otro Qt), que cubren las fuentes públicas del proyecto y sus scripts. Al distribuirlo hay que acompañarlo del texto de la LGPL y de los avisos de Qt.
- **AGENTS.md:**
  - el entorno local, con las dos rutas nuevas;
  - que este build no forma parte de cada cambio;
  - una regla ligera: un cambio que añade un módulo de Qt lo añade también a la lista de `build-static-deps.sh`, sin tener que compilarlo.
- **README:** cómo obtener el ejecutable.
- **CHANGELOG:** la entrada en `Added`.

## Risks / Trade-offs

- [Alguna opción de `configure` tiene otro nombre en 6.12.0, o qtdeclarative o qttools necesitan algo desactivado] → Se comprueba con `-list-features` en la tarea 1, se ajusta y se anota en la decisión 4. Widgets se volvería a activar sin que el cliente lo enlace.
- [El estilo de Qt Quick Controls que se elige en tiempo de ejecución no queda enlazado, porque en estático solo van los plugins que encuentra `qmlimportscanner`] → La verificación compara el aspecto con el cliente nativo. Si falta, se importa el estilo de forma explícita solo en el build estático. Al implementar no hizo falta: `qmlimportscanner` sigue los imports opcionales de `QtQuick.Controls` y enlaza todos los estilos, y las capturas coinciden con las del cliente nativo.
- [El equipo destino no tiene las librerías de xcb-util (por ejemplo `libxcb-cursor0` en Ubuntu)] → Son las mismas que necesita hoy el plugin xcb del kit compartido. El contenedor de la verificación dice cuáles hay que instalar, y §9.2 las lista.
- [El build estático se queda atrás sin que nadie lo note, porque no se compila en cada cambio] → Falla con un mensaje claro la próxima vez que se genera, y la regla de AGENTS.md cubre el caso más probable, un módulo de Qt nuevo.
- [glibc] → Compilado en Oracle Linux 9 (2.34), no arranca en sistemas con una glibc anterior, como Ubuntu 20.04 o Debian 11. Queda documentado.
- [Tiempo y disco] → Qt tardó 25 minutos con 12 núcleos (la estimación inicial era una hora), y sus directorios de compilación, que el script borra, ocupan varios GB. Quedan los tarballs y las fuentes (0,8 GB de Qt y 0,2 GB de PostgreSQL) y el Qt instalado (1,1 GB).
- [Las fuentes de Qt no van firmadas] → Lo explica la decisión 2.

## Migration Plan

No hay migración: todo es nuevo y opcional. Para deshacerlo basta con borrar `~/software/Qt/6.12.0-static`, `~/software/libpq-18.6-static` y `build/static`. Nada más depende de ellos.
