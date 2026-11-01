# Design

## Context

La motivación está en proposal.md (Why) y los requisitos en la delta de la spec `toolchain`. Lo que condiciona el diseño, comprobado durante la exploración:

- **La máquina:**
  - Oracle Linux 9.7, 12 núcleos, 61 GB de RAM y 76 GB libres.
  - El GCC nativo 16.2.0 está en `~/software/gcc-16.2.0`, configurado con `--disable-multilib --enable-languages=c,c++` y el `as`/`ld` 2.41 de gcc-toolset-14. Se compiló antes de que existiera `build-gcc.sh`, así que su tarball no está en `~/software/src`.
  - Tiene make, m4, flex, bison, makeinfo, bzip2, xz, wget, curl y gpgv. Las fuentes publicadas de GCC, binutils y mingw-w64 traen el `configure` generado, así que no hace falta autotools.
  - Hay un Wine 10.6 instalado en el sistema (`/usr/local`, 32 y 64 bits) y no existe `~/.wine`. Con un `WINEPREFIX` temporal y sin `DISPLAY`, ejecutó `cmd /c echo` en unos 9 segundos, casi todos de crear el prefijo, sin abrir ventanas. Se presenta como Windows 10.0.19043.
- **No hay paquete que sirva.**
  - El repositorio CodeReady Builder de Oracle Linux 9 trae `mingw64-gcc` 14.2.1, con binutils 2.43.1 y mingw-w64 12.0.0. Está instalado en esta máquina: `/usr/bin/x86_64-w64-mingw32-*`, y los enlaces de ccache en `/usr/lib64/ccache`, que van antes en el PATH.
  - Fedora tiene `mingw64-gcc` 16.2.1 (Fedora 44 y 45), pero EPEL 9 no.
- **Las versiones publicadas:**
  - GCC 16.2.0 (2026-08-07, 102 MB) y binutils 2.47 (2026-07-26, 28 MB), con su firma `.sig` en ftp.gnu.org, como el GCC de `build-gcc.sh`.
  - mingw-w64 v14.0.0 (2026-03-27, 11 MB), la última, en SourceForge con su firma `.sig`. La firma es de la clave DSA de JonY (Jonathan Yong), huella `CAF5 641F 74F7 DFBA 88AE 2056 93BD B53C D4EB C740`, la misma que firmó la v13.0.0. La clave está en keyserver.ubuntu.com, no en keys.openpgp.org. La firma de la v14.0.0 se verificó con `gpgv`.
- **Qt 6.12 en Windows:** Windows 10 1809 o posterior y Windows 11, x86_64, con MSVC 2022 o MinGW-w64 15.1 (la toolchain de MinGW-builds). Es la última versión de Qt que admite Windows 10.
- **mingw-w64 14:**
  - Por defecto usa el UCRT y `_WIN32_WINNT=0x0A00` (Windows 10).
  - Las cabeceras de winpthreads solo declaran `dllimport` con `WINPTHREADS_USE_DLLIMPORT`, así que enlazar winpthreads estática no necesita ninguna definición.
  - El `configure` de winpthreads usa `AC_NO_EXECUTABLES`: se configura y se compila sin enlazar ningún programa.
- **GCC 16 y Windows:**
  - Trae TLS nativo de Windows, que se activa con `--enable-tls` al configurar y necesita binutils 2.44 o posterior (notas de la versión). MSYS2 lo activó en mayo de 2026, recompilando unos 300 paquetes, y Arch Linux también lo usa. Con TLS nativo, una variable TLS no se puede usar directamente desde otra DLL; enlazar estático lo evita (nota de MSYS2).
  - MSYS2, Arch y Fedora usan hilos POSIX (winpthreads).
  - La configuración de GCC para mingw fija dónde busca las cabeceras y las librerías del sistema dentro del sysroot: `/mingw/include` y `/mingw/lib` (`gcc/config/mingw/mingw32.h`). `--with-native-system-header-dir` no cambia lo que busca el compilador; MSYS2 lo parchea.
  - Sin `--with-sysroot`, el `configure` de GCC busca las cabeceras del destino en `<prefijo>/x86_64-w64-mingw32/sys-include`. Si no están ahí, compila libgcc sin libc (`inhibit_libc`) y su `limits.h` no encadena con el de mingw-w64.
  - El fallo PR 125312 de libstdc++ (los contadores de `shared_ptr` con un mutex global) afectaba a los GCC 16.1 nativos de Windows, no a los cruzados. Lo dice su corrección: en los compiladores cruzados la prueba de `configure` pasa.

## Goals / Non-Goals

**Goals:**
- El compilador cruzado de la spec, desde las fuentes firmadas, con un script de la misma forma que `build-gcc.sh` y sin más requisitos del sistema que los del GCC nativo.
- Que lo que genere sea autocontenido sin opciones, como base del cliente estático para Windows.
- Que se compruebe solo, ejecutando incluso el resultado, sin necesitar una máquina Windows.
- Que el resto del proyecto no cambie.

**Non-Goals:**
- **Compilar para Windows las dependencias y el cliente:** Qt estático, libpq (con su TLS), protobuf con abseil, libpqxx y cpp-httplib; un fichero de toolchain de CMake y un preset para Windows; y el ejecutable autocontenido del cliente. Es el siguiente issue.
- **Instalarlo en el CI.** Se pidió solo para la máquina local.
- **Windows de 32 bits o ARM64**, y multilib.
- **Las DLLs del runtime de GCC** (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`). La decisión 6 explica por qué.
- **Otros lenguajes** (Fortran, Ada, ...) y **otras toolchains para Windows** (MSVC, clang-cl, llvm-mingw).
- **Un compilador que se ejecute en Windows** (canadian cross).
- **Un depurador para Windows** (GDB para mingw).
- **Windows 7 y 8.1.** El UCRT y `_WIN32_WINNT=0x0A00` piden Windows 10, igual que Qt 6.12.
- **Instalar o configurar Wine.** Se usa el del sistema, si lo hay.

## Decisions

### 1. Desde las fuentes, con un script como `build-gcc.sh`

`scripts/build-gcc-mingw.sh` tiene la misma forma que `build-gcc.sh`:
- sin root;
- variables `SOFTWARE_DIR`, `GCC_VERSION`, `BINUTILS_VERSION` y `MINGW_VERSION`;
- fuentes en `$SOFTWARE_DIR/src`;
- no hace nada si el compilador ya está instalado.

Exige el GCC nativo de `build-gcc.sh` (decisión 7), con el mismo mensaje de error que `build-deps.sh` y `build-static-deps.sh`.

Alternativas descartadas:
- **Paquetes de la distribución.** El de Oracle Linux 9 es GCC 14.2.1, y sus binutils 2.43.1 no admitirían el TLS nativo. Los de Fedora serían otra compilación de otra versión (16.2.1), dentro de un contenedor y con root.
- **Toolchains ya compiladas.** Las de MinGW-w64 con GCC (WinLibs, MinGW-builds, MSYS2) se ejecutan en Windows, y llvm-mingw es Clang.
- **MXE o crosstool-ng.** Son frameworks que compilan su propia versión de GCC con su propia configuración, y MXE además su propio Qt. El proyecto prefiere scripts pequeños que fijan cada versión y cada opción.
- **El script `mingw-w64-build` de Zeranoe.** Compila ramas de git en lugar de releases firmadas, y usa msvcrt por defecto.

### 2. GCC 16.2.0, binutils 2.47 y mingw-w64 14.0.0

- **GCC 16.2.0**, la misma versión que el nativo y que la imagen del CI. El tarball es el mismo fichero de `$SOFTWARE_DIR/src` que usa `build-gcc.sh`: se descarga una vez para los dos.
- **binutils 2.47**, la última. El TLS nativo pide 2.44 o posterior.
- **mingw-w64 14.0.0**, la última, con el UCRT y Windows 10 por defecto.
- **GMP, MPFR, MPC e ISL** dentro del árbol de GCC con `contrib/download_prerequisites`, como en el nativo.

Las tres versiones son variables del script, como en los demás.

### 3. Descargas firmadas, comprobadas antes de compilar nada

- **GCC y binutils** vienen de ftpmirror.gnu.org con su `.sig`, y se comprueban con `gpgv` contra el keyring de GNU, como en `build-gcc.sh`.
- **mingw-w64** viene de downloads.sourceforge.net, que redirige a un mirror, con su `.sig`:
  - la clave pública de JonY se descarga una vez de keyserver.ubuntu.com por su huella, que fija el script, y se guarda como keyring en `$SOFTWARE_DIR/src`;
  - `gpgv --status-fd` tiene que dar `VALIDSIG` con esa misma huella. Una firma correcta de otra clave también falla.
- **Las tres se comprueban antes de compilar o borrar nada.** Si una no coincide, el script falla, nombra el fichero y pide borrarlo, como `build-gcc.sh`.

Alternativa descartada: el tarball de la etiqueta `v14.0.0` del mirror de GitHub. No va firmado, y la release oficial es la de SourceForge.

### 4. El destino: x86_64, UCRT, Windows 10, SEH e hilos POSIX

- **Solo `x86_64-w64-mingw32`**, sin multilib (`--disable-multilib`).
- **El UCRT** (`--with-default-msvcrt=ucrt`), el runtime de C que forma parte de Windows 10 y posteriores. Es el valor por defecto de mingw-w64 14, pero se fija igualmente, como las opciones de Qt en `build-static-deps.sh`, para que el resultado no cambie con la versión. `msvcrt.dll` es el runtime antiguo, y MSYS2 está retirando MINGW64, su entorno con msvcrt.
- **`_WIN32_WINNT=0x0A00`** (`--with-default-win32-winnt`): las APIs de Windows 10, el mínimo de Qt 6.12. También es el valor por defecto, y también se fija.
- **Excepciones SEH**, las de x86_64 por defecto, que usan las tablas de desenrollado del propio Windows. No hay nada que configurar.
- **Hilos POSIX** (`--enable-threads=posix`) sobre winpthreads. Son los del MinGW de Qt, MSYS2, Arch y Fedora, así que es la combinación más probada con Qt y con las librerías que vendrán. `std::thread`, `std::mutex` y el resto van sobre winpthreads.
- **Los relojes de libstdc++ sobre winpthreads** (`--enable-libstdcxx-time=yes`). Se añadió al implementar, al leer el `configure` de libstdc++ de GCC 16: con el valor por defecto, en mingw no usa `clock_gettime`, y `steady_clock::now()` devuelve el reloj del sistema, que salta si se cambia la hora. Con la opción, `steady_clock` usa el `CLOCK_MONOTONIC` de winpthreads, y las esperas, su `nanosleep`. MSYS2 usa la misma opción.

Alternativas descartadas para los hilos:
- **`win32`.** Desde GCC 13 también admite `std::thread`, pero lo usan pocas distribuciones y algunas librerías esperan pthreads.
- **`mcf`** (mcfgthread). Es otra librería más, y tiene menos uso.

### 5. TLS nativo de Windows (`--enable-tls`)

Desde GCC 16, `thread_local` y `__thread` pueden usar el TLS del propio Windows (la sección `.tls` del ejecutable y su índice en el bloque del hilo) en lugar del emulado, que llama a `__emutls_get_address` en cada acceso. El emulado es lo que usaban todas las versiones anteriores y sigue siendo el valor por defecto.

- Es más rápido, y es lo que usan ahora MSYS2 y Arch con GCC 16.
- Su limitación, no poder acceder a una variable TLS de otra DLL, no afecta: todo irá enlazado en un único ejecutable.
- Todo lo que se compile con este compilador usa el mismo modelo de TLS, así que no se mezclan objetos de los dos.
- La comprobación falla si el programa de prueba usa TLS emulado (algún símbolo `__emutls`). Así una configuración que no lo active no pasa desapercibida.

Alternativa descartada: **el TLS emulado**, el valor por defecto de GCC y el del MinGW 15.1 de Qt. Si el nativo diera problemas al compilar Qt en el siguiente issue, se recompilaría el compilador sin `--enable-tls`, y con él todo lo compilado con él (Risks).

### 6. Solo el runtime estático (`--disable-shared`)

GCC se configura con `--disable-shared`, y winpthreads con `--disable-shared --enable-static`. El compilador solo tiene `libgcc.a`, `libgcc_eh.a`, `libstdc++.a` y `libwinpthread.a`. Cualquier ejecutable o DLL que genere lleva el runtime dentro, sin `-static` ni `-static-libstdc++`, que es lo que pide la spec y lo que necesita el cliente estático.

También simplifica el orden de compilación (decisión 9): winpthreads se compila solo como librería estática, antes de que exista libgcc.

Alternativa descartada: **estático y compartido**, como MSYS2 o Fedora, enlazando con `-static` en el proyecto. Permitiría DLLs que compartan un runtime, por ejemplo un Qt compartido para Windows. Pero cada compilación tendría que acordarse de `-static`, y un olvido dejaría un ejecutable que pide `libstdc++-6.dll`. El proyecto no quiere un Qt compartido para Windows; si algún día hiciera falta, se recompila el compilador con `--enable-shared`.

### 7. El GCC nativo como compilador de construcción

binutils y GCC se compilan con el GCC 16.2.0 de `build-gcc.sh` (`CC` y `CXX`). La documentación de GCC recomienda construir un compilador cruzado con un compilador nativo de la misma versión.
- Cuando el compilador de construcción es GCC, el `configure` de GCC enlaza por defecto `libstdc++` y `libgcc` estáticas en los programas del compilador (`cc1plus`, `collect2`, `lto1`, ...): `--with-static-standard-libraries` vale `auto`. Así no necesitan run path al `libstdc++` de `gcc-16.2.0`. Se comprueba con `readelf`.
- Las librerías del destino (libgcc, libstdc++) las compila el propio compilador cruzado.

Alternativa descartada: el `g++` del sistema (GCC 11 en EL9) o el de gcc-toolset-14. El nativo ya es un requisito de los demás scripts.

### 8. El prefijo es también el sysroot

Todo va a `~/software/gcc-16.2.0-mingw`:
- **binutils y GCC**, configurados con `--prefix` y `--with-sysroot` en ese directorio: `bin/x86_64-w64-mingw32-gcc`, `-g++`, `-ld`, `-objdump`, ...;
- **las cabeceras, el CRT y winpthreads** de mingw-w64, en `x86_64-w64-mingw32/include` y `x86_64-w64-mingw32/lib`, donde GCC instala también libstdc++;
- **un enlace simbólico relativo `mingw` → `x86_64-w64-mingw32`**, porque GCC busca las cabeceras y las librerías del sistema en `<sysroot>/mingw/include` y `<sysroot>/mingw/lib` (Context).

Es la disposición que documenta la guía de mingw-w64 (`mingw-w64-doc/howto-build`). Como el sysroot está dentro del prefijo, GCC y ld lo calculan relativo a su ejecutable, así que el directorio se puede mover.

Las dependencias que se compilen para Windows en el siguiente issue irán en sus propios prefijos, como las estáticas de Linux (`Qt/6.12.0-static`, `libpq-18.6-static`), no dentro del sysroot. El compilador no depende de nada de este repositorio, como el nativo.

Alternativas descartadas:
- **Sin sysroot**, como Arch o el script de Zeranoe. El `configure` de GCC buscaría las cabeceras en `sys-include`, y sin ellas compilaría libgcc sin libc y no encadenaría `limits.h` (Context).
- **El sysroot de Fedora** (`sys-root/mingw/...`). Tiene rutas más largas y está pensado para instalar dentro todas las librerías cruzadas; aquí van aparte.
- **`--with-native-system-header-dir=/include`**, como MSYS2. Necesita su parche de GCC.

### 9. El orden de compilación, en un solo árbol de GCC

1. **binutils:** `make` y `make install-strip`.
2. **Las cabeceras de mingw-w64** y el enlace `mingw`.
3. **El compilador:** el `configure` de GCC con todas las opciones de las decisiones 4 a 6, `make all-gcc` y `make install-gcc`. Queda el compilador sin sus librerías.
4. **El CRT de mingw-w64** (`--enable-lib64 --disable-lib32`), compilado con ese compilador.
5. **winpthreads**, estática. Su `configure` no enlaza programas, así que todavía no necesita libgcc.
6. **El resto de GCC** (libgcc, libstdc++, ...): `make` y `make install-strip`, como el nativo.
7. **Limpieza:** se borran los árboles de fuentes y de compilación, y los tarballs se quedan en `$SOFTWARE_DIR/src`.

Todo se compila en un directorio de trabajo propio dentro de `$SOFTWARE_DIR/src`, distinto del de `build-gcc.sh`, para que los dos scripts no se pisen.

Durante la compilación, `$PREFIX/bin` va el primero en el PATH. Los `configure` de mingw-w64 buscan las herramientas cruzadas por su nombre (`x86_64-w64-mingw32-gcc`, ...), y sin eso encontrarían antes las del sistema (Context). El script también quita del entorno `CC`, `CXX` y sus opciones (`CFLAGS`, ...), que esos `configure` tomarían para compilar código de Windows.

**Cuándo está instalado.** `make install-gcc` instala el driver en el paso 3, así que su existencia no prueba nada. La marca de instalación completa es `x86_64-w64-mingw32/lib/libstdc++.a`, que se instala en el último paso. Si no está, el script borra el prefijo, después de comprobar las descargas, y empieza de cero.

Alternativa descartada: **dos compiladores**, como Arch: uno de arranque con hilos `win32` para compilar winpthreads, y luego el definitivo. El orden de arriba llega a lo mismo con un solo árbol, gracias a la decisión 6.

### 10. La comprobación, también con `--check`

- `scripts/build-gcc-mingw.sh` instala el compilador y termina comprobándolo. Si ya está instalado, no hace nada, como `build-gcc.sh`.
- `scripts/build-gcc-mingw.sh --check` solo comprueba el compilador instalado.
- `scripts/build-gcc-mingw.sh --check <ejecutable>` solo comprueba las DLLs y el TLS de `<ejecutable>`, como `build-static.sh --check`.

La comprobación completa:
1. **Compila un programa de prueba en C++26**, que el script lleva dentro, en un directorio temporal y sin opciones de enlace:
   - cuatro `std::jthread` que incrementan una variable `thread_local` y suman el resultado en un `std::atomic`;
   - un `std::shared_ptr` copiado en cada hilo;
   - una excepción lanzada y capturada dentro de un hilo;
   - una característica de C++26 (indexación de packs), y `std::format` para la salida.

   Al implementar se cambió `std::println` por `std::format` con `std::fputs`. En mingw, `std::print` escribe en la consola con `std::__open_terminal` y `std::__write_to_terminal`, que GCC 16 todavía deja en `libstdc++exp.a`: un programa que lo use tiene que enlazar con `-lstdc++exp`, y la prueba se compila sin opciones. Se documenta en `docs/architecture.md` para el siguiente issue.
2. **Comprueba sus DLLs** con el `objdump -p` de las binutils cruzadas, y falla nombrando cada una que no sea `KERNEL32.dll`, `ucrtbase.dll` o una `api-ms-win-crt-*.dll`.
3. **Comprueba el TLS:** falla si `nm` muestra algún símbolo `__emutls` (decisión 5).
4. **Lo ejecuta con Wine**, si `wine` está en el PATH:
   - con un `WINEPREFIX` temporal, que se borra al acabar, después de parar su `wineserver`;
   - sin `DISPLAY` ni `WAYLAND_DISPLAY`, para que no se abra ninguna ventana;
   - con `WINEDLLOVERRIDES`, para que Wine no ofrezca instalar Mono ni Gecko.

   Compara su salida y su código de salida con los esperados. Sin Wine, avisa de que no lo ejecuta.

Con `--check <ejecutable>` solo se hacen los pasos 2 y 3. Sirve para probar los casos de error y para cualquier otro ejecutable de prueba. El paso 3 necesita la tabla de símbolos: en un ejecutable sin símbolos no encuentra nada.

No hay una suite de `ctest`: la spec exige que las pruebas no necesiten el compilador cruzado. Es el mismo caso que `build-static.sh` y `tests/web/dead-keys.mjs`.

### 11. Documentación

- **`docs/architecture.md` §8:**
  - una fila en la tabla de la toolchain;
  - una nota con la configuración (decisiones 4 a 6), la disposición (decisión 8) y la comprobación, y con que ni el CI ni los demás scripts lo necesitan.
- **AGENTS.md:**
  - en el entorno local, la ruta, el script y que no forma parte de cada cambio;
  - en la regla de las versiones de GCC, este script en la lista de lo que cambia a la vez.
- **README:**
  - una fila pendiente en el estado del proyecto: el ejecutable autocontenido para Windows, compilado desde Linux;
  - una línea con el script, junto al cliente autocontenido.
- **CHANGELOG:** la entrada en `Added`.
- **Al archivar,** el `Purpose` de la spec `toolchain` añade el compilador cruzado.

## Risks / Trade-offs

- [El TLS nativo es nuevo en GCC 16 y puede tener fallos] → MSYS2 y Arch lo usan desde mayo de 2026, y la comprobación ejecuta `thread_local` en varios hilos. Si diera problemas con Qt en el siguiente issue, se recompila sin `--enable-tls` (decisión 5).
- [Alguna prueba del `configure` de libstdc++ falla en el compilador cruzado (TLS, atómicos) y libstdc++ queda con una implementación peor] → Se comprueba que el `c++config.h` instalado define `_GLIBCXX_HAVE_TLS` y `_GLIBCXX_ATOMIC_WORD_BUILTINS`. Si faltara alguna, se fuerzan sus variables de caché de `configure`, como hace MSYS2 (`gcc_cv_have_tls`, `glibcxx_cv_atomic_word`), y se anota aquí.
- [Wine no es Windows] → Ejecutar con Wine descubre los errores de carga y de runtime más comunes, no todos. El usuario puede probar el programa de prueba en un Windows real (tarea opcional). Wine 10.6 implementa el UCRT.
- [keyserver.ubuntu.com no responde la primera vez] → El script falla antes de compilar, con un mensaje claro. Después el keyring se queda en `$SOFTWARE_DIR/src`. Como la huella está fijada en el script, otra clave no sirve.
- [SourceForge cambia sus URLs o sus mirrors] → La descarga falla antes de compilar nada; la versión y la URL están en el script.
- [Solo hay runtime estático] → Una DLL compilada con él lleva su propia copia del runtime, así que no sirve para un Qt compartido para Windows (decisión 6).
- [Tiempo y disco] → Sin bootstrap, la estimación era del orden de media hora con 12 núcleos. Medido: 13 minutos, con un máximo de 9 GB de disco en los árboles de compilación, que el script borra. Quedan los tarballs (150 MB) y el compilador instalado (0,5 GB).
- [El GCC nativo cambia de versión] → Los dos compiladores van a la vez: si cambia `GCC_VERSION`, cambia en los dos scripts y se recompilan ambos. AGENTS.md lo recoge en la regla de las versiones de GCC.

## Migration Plan

No hay migración: todo es nuevo y opcional. Para deshacerlo basta con borrar `~/software/gcc-16.2.0-mingw` y, si se quiere, los tarballs y el keyring de mingw-w64 en `~/software/src`. Todavía no depende nada de él.
