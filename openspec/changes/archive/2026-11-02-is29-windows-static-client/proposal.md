# Proposal

## Why

El cliente de escritorio ya se genera como un único ejecutable autocontenido para Linux (`scripts/build-static.sh`). El siguiente paso es el mismo para Windows, desde esta máquina Linux: un `client.exe` que se copia a un Windows 10 u 11 y se ejecuta sin instalar nada, con los dos modos del cliente nativo. Es el issue #29.

Las piezas para compilarlo ya existen fuera de guita. toolchain-cpp v0.2.0 trae el compilador cruzado (`gcc-mingw`, GCC 16.2 para `x86_64-w64-mingw32`) con su fichero de toolchain de CMake, y los componentes estáticos para Windows: Qt 6.12.0 (`qt-mingw`), OpenSSL (`openssl-mingw`), libpq con TLS (`libpq-mingw`), libpqxx, cpp-httplib y protobuf con abseil (`*-mingw`). Ya se han comprobado en Wine: una aplicación Qt Quick, y un programa que se conecta a PostgreSQL con TLS mediante libpqxx. Falta lo que es de guita: fijar esa versión de toolchain-cpp, adaptar su build al build cruzado, generar el ejecutable, comprobarlo y probar el cliente de verdad, incluido el `wss://` que toolchain-cpp no pudo probar de extremo a extremo.

## What Changes

- **guita fija toolchain-cpp v0.2.0** en lugar de v0.1.0: en el CI (`TOOLCHAIN_CPP_REF`), el README, AGENTS.md y `docs/architecture.md`. Los componentes de siempre no cambian de prefijo. El kit de escritorio (`qt`) trae ahora qtshadertools, así que la caché del CI se rellena una vez.
- **El build del cliente para Windows:**
  - un preset `windows` que compila solo el cliente con el kit `qt-mingw` y los componentes `-mingw`;
  - los ajustes de CMake del build cruzado:
    - el `protoc` del host;
    - la libpq estática de Windows con lo que pide al enlazar;
    - el servidor, solo fuera de Windows;
    - los plugins de Qt para Windows, entre ellos el TLS de Schannel para `wss://`;
  - un script nuevo, `scripts/build-windows.sh`, que genera `build/dist/client.exe` y comprueba que solo importa DLLs de una lista de DLLs de Windows. Tiene un modo `--check <ejecutable>`.
- **Fuera del flujo habitual**, como el build estático de Linux: ni el preset `native`, ni `ctest --preset native`, ni el CI lo necesitan.

## Capabilities

### New Capabilities

- `cliente-windows`: el ejecutable autocontenido del cliente para Windows. Cubre:
  - sus dependencias, que vienen de toolchain-cpp;
  - qué lleva dentro y qué DLLs puede importar;
  - su comprobación;
  - los dos modos del cliente, con TLS hacia PostgreSQL y `wss://` hacia el servidor;
  - que solo se genera bajo demanda.

### Modified Capabilities

Ninguna. El requisito "Toolchain de toolchain-cpp" de `toolchain` ya cubre que las dependencias para Windows vengan de sus componentes.

## Impact

- **Scripts:** nuevo `scripts/build-windows.sh`.
- **CMake:**
  - `CMakePresets.json`: los presets `windows`;
  - `src/proto/CMakeLists.txt`: el `protoc` del host en cualquier build cruzado, no solo en WebAssembly;
  - `src/backend/CMakeLists.txt`: las librerías que pide la libpq estática en Windows, y el servidor solo fuera de Windows;
  - `src/client/CMakeLists.txt`: los plugins de Qt para Windows;
  - `CMakeLists.txt`: el mensaje de la comprobación del Qt estático, que nombra el kit de Windows.

  El build nativo y el WebAssembly no cambian.
- **Código:** no se espera ningún cambio de comportamiento. Si algo del cliente o del backend no compila para Windows, se cambia lo mínimo, sin tocar el contrato ni la base de datos.
- **CI:** no compila nada para Windows. Pasa a toolchain-cpp v0.2.0, y con él cambia la clave de su caché.
- **Entorno local:** los componentes `gcc-mingw qt-mingw openssl-mingw libpq-mingw libpqxx-mingw cpp-httplib-mingw protobuf-mingw` de toolchain-cpp, instalados una vez: unos 30 minutos, sobre todo Qt, con cerca de 40 GB de disco en el pico. Wine (opcional) permite probar el ejecutable en esta máquina.
- **Documentación:**
  - `docs/architecture.md`: §8 (toolchain), §9.2 (escritorio), §10.4 (CI) y §12 (licencias: Qt estático, OpenSSL, winpthreads y el runtime de mingw-w64 en un ejecutable distribuido);
  - AGENTS.md, el README y el CHANGELOG.
