# Design

## Context

La motivación está en proposal.md y los requisitos en la delta de `cliente-windows`. Lo que condiciona el diseño:

- **toolchain-cpp v0.2.0** (publicada el 2026-11-02) trae, compilado y comprobado en su change `windows-components` (toolchain-cpp#2):
  - **`gcc-mingw`:** GCC 16.2.0 para `x86_64-w64-mingw32`, con UCRT, Windows 10, hilos POSIX, TLS nativo y solo el runtime estático, en `software/gcc-16.2.0-mingw`. Trae su `toolchain.cmake`, con rutas relativas y la búsqueda limitada a las raíces que se le pasan (`CMAKE_FIND_ROOT_PATH`).
    - Lleva un parche de las cabeceras de libstdc++ para un bug de GCC (125151). Sin él, un programa en C++26 que copia o lanza `std::runtime_error` y sus derivadas no enlaza en Windows. guita lanza `std::invalid_argument` en el backend, así que lo necesita.
    - Su `--check <ejecutable>` solo admite DLLs del núcleo y del UCRT: sirve para el compilador, no para un cliente Qt.
  - **`qt-mingw`:** Qt 6.12.0 estático en `software/Qt/6.12.0-mingw`, con qtbase (SQL y SQLite), qtshadertools, qtsvg, qtdeclarative y qtwebsockets:
    - el TLS de Windows (Schannel, `plugins/tls/libqschannelbackend.a`), sin OpenSSL;
    - Direct3D 11 y 12 y OpenGL de escritorio;
    - su host es el kit de escritorio (`qt`), que en v0.2.0 trae qtshadertools.

    Se usa con su `lib/cmake/Qt6/qt.toolchain.cmake`, que encadena el del compilador, y `QT_HOST_PATH` sin `..`. Una aplicación Qt Quick mínima pesa 46 MB, solo importa DLLs de Windows y en Wine se dibuja con Direct3D 11.
  - **`openssl-mingw` 3.5.9 y `libpq-mingw` 18.6:** una `libpq.a` estática de verdad, con TLS. Hay que enlazar después de ella `libpgcommon_shlib.a`, `libpgport_shlib.a`, `libssl.a`, `libcrypto.a` y las librerías de Windows `ws2_32`, `secur32` y `crypt32`; `advapi32`, `shell32` y `user32` ya las enlaza el GCC de mingw.
  - **`protobuf-mingw`** (sin `protoc`), **`libpqxx-mingw`** y **`cpp-httplib-mingw`** (sin zlib ni TLS), en C++26.

  En toolchain-cpp se probaron en Wine un programa con las cinco librerías (protobuf, libpqxx con `sslmode=require`, un `httplib::Server`) y una aplicación Qt Quick. El `wss://` de extremo a extremo quedó para este change (comentario en #29).
- **Wine 10.6** no implementa `mscms.dll.WcsCreateIccProfile`, que Qt llama al leer el perfil de color: una aplicación Qt se aborta al arrancar. En un prefijo temporal se evita quitando el perfil sRGB (`windows/system32/spool/drivers/color`).
- **El código de guita:**
  - lo único que usa POSIX es `src/backend/server/main.cpp` (`pthread`, `sigwait`); el servidor no se compila para Windows;
  - el cliente y el backend embebido usan Qt, libpqxx, cpp-httplib (`api` lo enlaza por `HttpRpcTransport` y `WebSocketRpcTransport`) y protobuf;
  - la cadena de conexión por defecto del modo autónomo usa TCP (`host=127.0.0.1`), no sockets Unix.
- **El build de guita:**
  - `src/proto/CMakeLists.txt` solo usa el `protoc` del host con Emscripten;
  - `src/client/CMakeLists.txt` fija bajo `GUITA_STATIC` los plugins de Qt de Linux (xcb, Wayland, el TLS de OpenSSL);
  - `src/backend/CMakeLists.txt` añade a la libpq estática sus librerías de PostgreSQL, OpenSSL y `m`;
  - `CMakeLists.txt` exige con `GUITA_STATIC` un Qt estático y nombra `qt-static` si no lo es.
  - Nadie fija el estilo de Qt Quick Controls: cada plataforma usa el suyo por defecto.
- **guita fija toolchain-cpp v0.1.0** en el CI (`TOOLCHAIN_CPP_REF`), el README, AGENTS.md y `docs/architecture.md` §8. La caché del CI va por esa versión y la lista de componentes.
- **El TLS del servidor** lo pone un proxy inverso (`docs/architecture.md` §9.1, con un ejemplo de HAProxy): `wss://` fuera y `ws://` hacia el servidor.

## Goals / Non-Goals

**Goals:**
- El `client.exe` de la spec, generado en esta máquina con un script y comprobado.
- Los dos modos del cliente en Windows: el autónomo también con TLS hacia PostgreSQL, y el conectado también por `wss://`.
- guita sobre toolchain-cpp v0.2.0, sin ningún script propio que compile dependencias.
- El build nativo, el WebAssembly, las pruebas y el CI, sin cambios de comportamiento.

**Non-Goals:**
- **Compilarlo en el CI.** Es local, como el ejecutable estático de Linux (#22 trata el de Linux).
- **Instalador, firma de código (Authenticode), icono y recursos de versión** del ejecutable: es el empaquetado (#21).
- **El servidor para Windows**, y ejecutar la batería de pruebas para Windows o con Wine.
- **Windows de 32 bits o ARM64, un Qt compartido para Windows, MSVC.**
- **Cambiar el estilo de los controles** (por ejemplo a Fusion). El cliente usa el estilo de Windows por defecto (decisión 5).
- **PostgreSQL embebido** (§11 de la arquitectura).

## Decisions

### 1. guita fija toolchain-cpp v0.2.0

Cambian a la vez, como pide AGENTS.md para una versión nueva de toolchain-cpp:
- `TOOLCHAIN_CPP_REF: v0.2.0` en `.github/workflows/ci.yml`. La lista de componentes del CI no cambia: no instala nada de Windows;
- el README (el `git clone --branch`, la tabla de componentes y la sección de Windows);
- AGENTS.md (la versión fijada y los componentes para Windows);
- `docs/architecture.md` §8 (versión, enlaces y tabla) y §10.4.

Los prefijos no cambian (v0.2.0 es MINOR), así que los presets `native`, `wasm` y `static` siguen igual. El kit de escritorio trae qtshadertools: en local, `install.sh qt` lo reinstala una vez; en el CI, la clave de caché cambia con la versión y la caché se rellena una vez.

### 2. Un solo script: `scripts/build-windows.sh`

Como `build-static.sh`, con la misma forma (`SOFTWARE_DIR` de toolchain-cpp, `--check`):
1. Comprueba que están el kit `qt-mingw` y los cinco prefijos `-mingw`. Si falta alguno, falla nombrándolo y con la orden que lo instala: `../toolchain-cpp/install.sh qt-mingw libpq-mingw libpqxx-mingw cpp-httplib-mingw protobuf-mingw` (`qt-mingw` arrastra `gcc-mingw` y `qt`, y `libpq-mingw` arrastra `openssl-mingw`).
2. `cmake --preset windows` y `cmake --build --preset windows`.
3. Copia `build/windows/bin/client.exe` a `build/dist/client.exe` y le quita los símbolos con el `strip` cruzado.
4. Comprueba las DLLs que importa (decisión 6) y muestra la lista y el tamaño.

`--check <ejecutable>` hace solo el paso 4.

Alternativa descartada: **ampliar `build-static.sh` con `--windows`.** La comprobación es otra (`objdump -p` contra una lista blanca, frente a `readelf` y `ldd`), y el preset y las rutas también. Mezclarlas complicaría un script que funciona.

### 3. El preset `windows`

- **Preset de configuración `windows`** (`build/windows`, Release):
  - `toolchainFile`: `${sourceParentDir}/toolchain-cpp/software/Qt/6.12.0-mingw/lib/cmake/Qt6/qt.toolchain.cmake`;
  - `QT_HOST_PATH`: el kit de escritorio, con `${sourceParentDir}` (sin `..`);
  - `CMAKE_FIND_ROOT_PATH` y `CMAKE_PREFIX_PATH`: `libpq-18.6-mingw`, `openssl-3.5.9-mingw`, `libpqxx-8.0.2-mingw`, `cpp-httplib-0.61.0-mingw` y `protobuf-36.2-mingw`;
  - `GUITA_HOST_PROTOBUF_DIR`: el protobuf nativo, para su `protoc`;
  - `GUITA_STATIC=ON` y `BUILD_TESTING=OFF`.
- **Preset de compilación `windows`:** solo el target `client`.

### 4. Los ajustes de CMake, sin cambiar el build nativo

- **`src/proto`:** el `protoc` del host en cualquier build cruzado (`CMAKE_CROSSCOMPILING`), no solo con Emscripten.
- **`src/backend`:**
  - con `GUITA_STATIC` y `WIN32`, la libpq estática lleva `libpgcommon_shlib.a`, `libpgport_shlib.a`, OpenSSL estático (`OPENSSL_USE_STATIC_LIBS`) y `ws2_32`, `secur32` y `crypt32`, en lugar de `m`;
  - el servidor solo se define fuera de Windows.
- **`src/client`:** bajo `GUITA_STATIC`, una rama para Windows aparte de la de Linux en `qt_import_plugins`:
  - la plataforma `qwindows` (`Qt6::QWindowsIntegrationPlugin`);
  - el TLS de Schannel (`Qt6::QSchannelBackendPlugin`), para `wss://`;
  - sin formatos de imagen ni los demás tipos que ya se excluyen en Linux.
- **`CMakeLists.txt`:** el mensaje del Qt que no es estático nombra el kit `qt-mingw` y `install.sh qt-mingw` cuando el build es para Windows.
- **El ejecutable** es de subsistema gráfico (`WIN32_EXECUTABLE`): no abre consola. `qt_add_executable` no lo activa por sí solo (el primer build salió de consola), así que se pone en la rama de Windows.

### 5. El estilo de controles por defecto de Windows

El estilo de Qt Quick Controls es el que Qt usa por defecto en Windows, sin código del proyecto: el ejecutable se ve como una aplicación de Windows, no con el Fusion de Linux. Cambiarlo, por ejemplo a Fusion en todas las plataformas, sería otro change. Si `qmlimportscanner` no enlazara el plugin del estilo, se añade a mano.

### 6. Una lista de DLLs de Windows permitidas

`build-windows.sh` lista las DLLs que importa el ejecutable con el `objdump -p` cruzado y falla nombrando cada una que no esté en **su lista de DLLs de Windows**. Son solo DLLs que trae Windows 10 1809 en `System32`:
- las del núcleo y de la interfaz: `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `ADVAPI32.dll`, `SHELL32.dll`, `ole32.dll`, ...;
- las gráficas: `d3d9.dll`, `d3d11.dll`, `d3d12.dll`, `dxgi.dll`, `dwrite.dll`, `opengl32.dll`, ...;
- las de red y TLS: `WS2_32.dll`, `Secur32.dll`, `CRYPT32.dll`, ...;
- los conjuntos de API del UCRT y de WinRT (`api-ms-win-*.dll`) y `ucrtbase.dll`.

La lista exacta se fija al implementar, con lo que importe el cliente, y cada DLL se contrasta con la documentación de Microsoft. Una DLL nueva hace fallar la comprobación hasta que se revisa y se añade.

Alternativa descartada: **una lista negra** (Qt, runtime de GCC, OpenSSL, PostgreSQL, ...), como `build-static.sh`. En Linux `ldd` resuelve las rutas y delata lo que viene de toolchain-cpp; aquí no hay `ldd`, y una lista negra dejaría pasar una DLL desconocida.

### 7. Verificación a mano, fuera de `ctest`

La spec exige que nada de esto lo necesiten las pruebas ni el CI.

- **La comprobación de `build-windows.sh`,** en positivo, con un ejecutable que importa una DLL que no está en la lista, y sin los componentes para Windows.
- **Con Wine, en el Xvfb `:99`:** el ejecutable copiado a un directorio temporal, con un prefijo de Wine temporal sin el perfil sRGB (Context). Se comparan capturas con el cliente nativo: las mismas vistas y textos, con el estilo de controles de Windows.
  - **Modo conectado**, contra `scripts/run-server.sh`.
  - **Modo autónomo**, contra un `postgres:18.6` efímero vacío: aplica las migraciones, y no abre ningún puerto (`ss -ltnp`).
  - **Modo autónomo con TLS:** el mismo contenedor con TLS (un certificado autofirmado) y `sslmode=require`. `pg_stat_ssl` confirma la conexión cifrada.
  - **`wss://`:** un HAProxy efímero (Docker) delante de `scripts/run-server.sh`, con la configuración de §9.1 y un certificado autofirmado generado en `tmp/`.
    - Sin confiar en el certificado, el cliente no se conecta y muestra que el servidor no está disponible, sin cerrarse.
    - Confiando en él: se importa en el almacén Root de usuario del prefijo de Wine. Si Wine no lo permite, esta parte se prueba solo en el Windows real, importándolo con `certutil -user -addstore Root`, y se anota.
  - **Traducciones,** con `LANG=es_ES.UTF-8` y `LANG=C`.

  Si Qt Quick no dibuja con Direct3D 11 en Wine, se prueba con `QSG_RHI_BACKEND=opengl` y se anota.
- **En un Windows 10 u 11 real,** el usuario copia solo `client.exe` y prueba los dos modos, y `wss://` contra el HAProxy de la prueba.

### 8. Documentación

- **`docs/architecture.md`:**
  - §8: toolchain-cpp v0.2.0, los componentes para Windows en la tabla y el preset `windows`;
  - §9.2: el ejecutable para Windows, su comprobación y lo verificado (Wine, el apaño del perfil de color, `wss://`);
  - §10.4: la versión de toolchain-cpp del CI;
  - §12: licencias. El Qt estático (LGPL), igual que en Linux; OpenSSL (Apache 2.0), winpthreads (MIT) y el runtime de mingw-w64 en un ejecutable distribuido; libgcc y libstdc++, bajo la excepción del runtime de GCC.
- **AGENTS.md:** la versión fijada, los componentes para Windows del entorno local, y que el build para Windows no forma parte de cada cambio, salvo que lo toque.
- **README:** la versión de toolchain-cpp, la fila de Windows como hecha y cómo obtener el ejecutable.
- **CHANGELOG:** `Added`, el ejecutable para Windows. guita no se ha publicado todavía, así que la versión de toolchain-cpp se corrige en la entrada que ya la nombra, sin una entrada `Changed`.

## Risks / Trade-offs

- [El código de guita no compila tal cual para Windows] → toolchain-cpp ya probó las librerías y Qt, pero no el código de guita. Se cambia lo mínimo, sin tocar el contrato ni la base de datos, y cada cambio se anota aquí.
- [Qt Quick en Wine puede comportarse distinto que en Windows] → Wine solo sirve para la prueba local; la referencia es el Windows real.
- [Confiar en un certificado autofirmado dentro de Wine] → Si no se puede, el caso positivo de `wss://` queda solo para el Windows real (decisión 7). El rechazo sí se prueba en Wine.
- [La lista de DLLs puede rechazar una DLL legítima de Windows] → Se revisa y se añade con su referencia. Es preferible a dejar pasar una que no lo es.
- [Un ejecutable grande] → Del orden de 50 a 70 MB, con Qt Quick y el backend. Reducirlo es #24.
- [El CI rellena su caché una vez por el cambio de versión de toolchain-cpp] → Solo esa vez.

## Migration Plan

No hay migración: todo es nuevo y opcional, salvo la versión de toolchain-cpp. Quien compila guita tiene que poner su clon de toolchain-cpp en v0.2.0 (`git fetch --tags && git checkout v0.2.0`) e instalar de nuevo el kit de escritorio (`install.sh qt`, por qtshadertools). Para deshacer el build de Windows basta con borrar el preset, el script y los ajustes de CMake. Volver a v0.1.0 es cambiar de nuevo la versión en los mismos sitios.
