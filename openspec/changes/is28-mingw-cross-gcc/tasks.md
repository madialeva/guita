# Tasks

## 1. El compilador cruzado

- [x] 1.1 Crear `scripts/build-gcc-mingw.sh` con lo que va antes de compilar, según las decisiones 1 a 3 del diseño y la marca de instalación de la decisión 9:
  - la cabecera documenta qué instala, la configuración del destino, las variables, los requisitos del sistema, el tiempo y el disco;
  - el error que remite a `scripts/build-gcc.sh` si falta el GCC nativo;
  - la salida inmediata si existe `x86_64-w64-mingw32/lib/libstdc++.a` en el prefijo;
  - la descarga y la comprobación de las firmas: GCC y binutils con el keyring de GNU, y mingw-w64 con la clave de JonY, descargada de keyserver.ubuntu.com por su huella y exigida en el `VALIDSIG` de `gpgv`.

  Verificar con un `SOFTWARE_DIR` en `tmp/`, con enlaces a los tarballs ya descargados para no repetir las descargas:
  - sin el GCC nativo, falla sin descargar nada y nombra `scripts/build-gcc.sh`;
  - con un enlace al GCC nativo y el tarball de mingw-w64 truncado, falla antes de compilar, nombra el fichero y no crea el prefijo;
  - con el keyring de GNU en lugar del de mingw-w64, falla igual.
- [x] 1.2 Añadir la compilación según las decisiones 4 a 9: binutils, las cabeceras y el enlace `mingw`, `make all-gcc`, el CRT, winpthreads estática, el resto de GCC con `install-strip` y la limpieza. Ejecutarlo sobre `~/software`, medir el tiempo y el disco, y anotar en el diseño las opciones que haya habido que ajustar. Verificar:
  - que `x86_64-w64-mingw32-gcc -dumpfullversion` da 16.2.0, como el `gcc` nativo, y que `-v` muestra el modelo de hilos `posix` y las opciones del diseño;
  - que el `g++` nativo no ha cambiado (su SHA-256 antes y después);
  - que `readelf -d` de `cc1plus`, `collect2` y `lto1` no muestra `libstdc++.so` ni `RUNPATH`;
  - que el `c++config.h` instalado define `_GLIBCXX_HAVE_TLS` y `_GLIBCXX_ATOMIC_WORD_BUILTINS` (Risks del diseño), y `_GLIBCXX_USE_CLOCK_MONOTONIC` (decisión 4);
  - que `mingw` es un enlace relativo a `x86_64-w64-mingw32`, que no hay ninguna DLL en el prefijo y que están `libstdc++.a` y `libwinpthread.a`;
  - que un programa mínimo en C++ compila y enlaza sin opciones, y que el `objdump -p` cruzado solo le muestra DLLs del sistema (la comprobación completa llega en el grupo 2);
  - que no quedan árboles de fuentes ni de compilación en `~/software/src`.
- [x] 1.3 Comprobar los otros escenarios de instalación de la spec:
  - una segunda ejecución sobre `~/software` termina al momento sin descargar nada, también con un proxy inaccesible en el entorno;
  - con un `SOFTWARE_DIR` en `tmp/` que tiene el GCC nativo enlazado y un prefijo a medias (solo el driver), el script comprueba las descargas, borra el prefijo y empieza por binutils. Se interrumpe ahí y se borra ese directorio.
- [x] 1.4 Documentar el compilador:
  - `docs/architecture.md` §8: la fila de la tabla de la toolchain y una nota con la configuración, la disposición, el tiempo y el disco medidos, y que ni el CI ni los demás scripts lo necesitan;
  - AGENTS.md: en el entorno local, la ruta, el script y que no forma parte de cada cambio; en la regla de las versiones de GCC, este script;
  - README: la fila pendiente del ejecutable autocontenido para Windows en el estado del proyecto, y el script junto al cliente autocontenido;
  - CHANGELOG: la entrada en `Added`.

  Verificar que los comandos y las rutas documentados coinciden con los del script, que `.github/` no cambia y que `ctest --preset native` pasa completo.

## 2. La comprobación del compilador

- [x] 2.1 Añadir la comprobación según la decisión 10: el programa de prueba, sus DLLs, el TLS y la ejecución con Wine, los modos `--check` y `--check <ejecutable>`, y la comprobación al final de la instalación. Verificar:
  - que `scripts/build-gcc-mingw.sh --check` termina sin errores, lista `KERNEL32.dll` y las DLLs del UCRT y muestra la salida esperada del programa ejecutado con Wine;
  - que `~/.wine` sigue sin existir, y que no quedan ni el prefijo temporal de Wine ni su `wineserver`.
- [x] 2.2 Comprobar los casos de error y de entorno:
  - un ejecutable enlazado en `tmp/` contra una DLL de prueba llamada `libstdc++-6.dll` hace fallar `--check <ejecutable>`, que nombra esa DLL;
  - un ejecutable con TLS emulado hace fallar `--check <ejecutable>`, que nombra los símbolos `__emutls`. GCC no tiene `-femulated-tls`, que es de Clang: el modelo de TLS se fija al configurarlo. Así que el ejecutable se enlaza con este compilador a partir de un objeto con una variable `__thread` compilado por el mingw 14 del sistema, que usa TLS emulado, y solo importa DLLs del sistema;
  - con un PATH sin Wine, `--check` avisa de que no ejecuta el programa y termina sin error;
  - lanzada con `DISPLAY=:99`, el Xvfb de las comprobaciones gráficas, `xwininfo -root -children` no muestra ninguna ventana nueva;
  - una copia del prefijo en `tmp/` pasa `--check` igual que el original, porque el directorio se puede mover. Después se borra la copia.
- [x] 2.3 Documentar la comprobación en `docs/architecture.md` §8: qué compila, qué DLLs admite, el TLS, Wine y los modos `--check`. Verificar que coincide con el script y que `ctest --preset native` pasa completo.
- [ ] 2.4 El usuario revisa el compilador y la comprobación y da el visto bueno. Opcional: ejecutar el programa de prueba, compilado en `tmp/` con el compilador instalado, en un Windows 10 u 11 de 64 bits.
