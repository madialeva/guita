# Proposal

## Why

El cliente de escritorio ya se puede generar como un único ejecutable autocontenido para Linux (`scripts/build-static.sh`), pero no para Windows, aunque Qt 6.12 lo admite (Windows 10 1809 o posterior, con MSVC 2022 o MinGW-w64). La idea es dar el mismo paso para Windows desde esta máquina Linux: compilar el cliente de forma estática, sin una máquina Windows y con la misma versión de GCC que el resto del proyecto. Para eso hace falta antes un compilador cruzado, un GCC que se ejecuta en Linux y genera ejecutables de Windows.

No hay uno ya hecho que sirva:
- las toolchains de MinGW-w64 que se distribuyen compiladas (WinLibs, MinGW-builds, que es la que usa Qt con GCC 15.1, y MSYS2) se ejecutan en Windows;
- llvm-mingw usa Clang, no GCC;
- el repositorio CodeReady Builder de Oracle Linux 9 trae uno (`mingw64-gcc`), pero es GCC 14.2.1, con binutils 2.43.1 y mingw-w64 12, no la versión 16 del proyecto.

Así que hay que compilarlo desde las fuentes, como ya se hace con el GCC nativo. Este cambio solo monta el compilador. Compilar para Windows Qt, libpq y las librerías del backend, y después el propio cliente, queda para un issue posterior. Es una herramienta local: el CI no la instala. Es el issue #28.

## What Changes

- **Un script nuevo, `scripts/build-gcc-mingw.sh`.** Compila sin root, desde las fuentes firmadas, un compilador cruzado de C y C++ para Windows de 64 bits (`x86_64-w64-mingw32`). Lo instala en `~/software/gcc-16.2.0-mingw`, junto a las demás dependencias locales:
  - GCC 16.2.0, la misma versión que el compilador nativo y que la imagen del CI;
  - binutils 2.47 para Windows: ensamblador, enlazador y demás herramientas de binarios;
  - mingw-w64 14.0.0: las cabeceras de Windows y del runtime de C, el runtime de C (CRT) y winpthreads.
- **El destino que fija el script:**
  - Windows 10 o posterior, el mínimo de Qt 6.12, con el runtime de C universal de Windows (UCRT);
  - excepciones SEH e hilos POSIX sobre winpthreads, como el MinGW de Qt y el de MSYS2;
  - TLS nativo de Windows para `thread_local`, nuevo en GCC 16.
- **Runtime estático por construcción.** El compilador solo trae las versiones estáticas de libgcc, libstdc++ y winpthreads. Todo lo que compila lleva dentro el runtime de C++ y solo necesita DLLs del sistema (`KERNEL32.dll` y las del UCRT), sin opciones de enlace adicionales. Es la base del cliente estático para Windows.
- **Una comprobación del compilador**, al final de la instalación y a demanda con `--check`:
  - compila un programa de prueba en C++26 que usa hilos, `thread_local`, excepciones y `shared_ptr`;
  - falla si el ejecutable importa alguna DLL que no sea del sistema, o si usa TLS emulado;
  - lo ejecuta con Wine si está instalado.
- **Fuera del flujo habitual**, como el build estático: ni el preset `native`, ni `ctest --preset native`, ni el CI lo necesitan, y un cambio cualquiera no tiene que compilarlo.

## Capabilities

### New Capabilities

Ninguna.

### Modified Capabilities

- `toolchain`: añade el compilador cruzado para Windows. Cubre cómo se instala, que usa la misma versión de GCC que el nativo, que lo que genera solo necesita DLLs del sistema, cómo se comprueba, y que no lo necesitan el build nativo, las pruebas ni el CI.

## Impact

- **Scripts:** `scripts/build-gcc-mingw.sh` (nuevo). No cambia ningún otro script, ni los presets, ni los `CMakeLists.txt`.
- **Dependencias:** ninguna de producción. Se descargan las fuentes de GCC 16.2.0 (102 MB, el mismo tarball que usa `build-gcc.sh`), binutils 2.47 (28 MB) y mingw-w64 14.0.0 (11 MB), y la clave pública con la que se firman las versiones de mingw-w64.
- **Comportamiento:** ninguno de la aplicación, el API ni la base de datos.
- **Entorno local:**
  - se compila con el GCC nativo de `scripts/build-gcc.sh`, que tiene que estar instalado;
  - del orden de media hora de compilación, con varios GB de disco temporal que el script borra al acabar;
  - Wine es opcional. Si está instalado (en esta máquina hay un Wine 10.6), la comprobación ejecuta el programa de prueba.
- **Documentación:**
  - `docs/architecture.md` §8: el compilador cruzado en la tabla de la toolchain, con su configuración;
  - AGENTS.md: el entorno local, y que este compilador no forma parte de cada cambio;
  - README: el ejecutable para Windows como trabajo pendiente en el estado del proyecto, y cómo instalar el compilador;
  - CHANGELOG: entrada en `Added`.
- **Otras issues:** la siguiente, compilar para Windows las dependencias y el cliente estático, depende de esta.
