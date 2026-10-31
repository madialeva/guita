# Tasks

## 1. Dependencias estáticas locales

- [x] 1.1 El usuario instala con `sudo dnf` los paquetes de desarrollo que faltan para compilar Qt con X11 (Context del diseño): `xcb-util-devel`, `xcb-util-wm-devel`, `xcb-util-image-devel`, `xcb-util-keysyms-devel`, `xcb-util-renderutil-devel`, `xcb-util-cursor-devel` y `libxkbcommon-x11-devel`. Pueden venir de CodeReady Builder o de EPEL. Verificar con `rpm -q` que están los siete.
- [x] 1.2 Crear `scripts/build-static-deps.sh` con la parte de Qt, según las decisiones 1 a 4 del diseño:
  - la cabecera documenta variables, requisitos del sistema, tiempo y disco;
  - el error que remite a `scripts/build-gcc.sh` si falta el GCC;
  - la descarga y la comprobación SHA-256 de los cinco módulos en `~/software/src/qt-6.12.0/`;
  - qtbase con la configuración de la decisión 4, contrastada antes con `configure -list-features`;
  - los otros cuatro módulos con `qt-configure-module`;
  - el borrado de los directorios de compilación.

  Ejecutarlo sobre `~/software` y anotar en la decisión 4 las opciones que haya habido que ajustar. Verificar:
  - que `~/software/Qt/6.12.0-static/lib` tiene `libQt6Core.a`, `libQt6Quick.a` y `libQt6WebSockets.a`, y ninguna `libQt6*.so`;
  - que existe `lib/cmake/Qt6LinguistTools`;
  - que `readelf -d` de `bin/moc` muestra el `RUNPATH` al `libstdc++` de `gcc-16.2.0` y que `bin/qtpaths6 --qt-query QT_VERSION` da 6.12.0 desde una shell sin `LD_LIBRARY_PATH`;
  - que el kit `~/software/Qt/6.12.0/gcc_64` no ha cambiado.
- [x] 1.3 Añadir a `scripts/build-static-deps.sh` la libpq, según la decisión 5: descarga y SHA-256 de PostgreSQL 18.6, `configure` con el GCC del proyecto, instalación de solo cliente en `~/software/libpq-18.6-static` y borrado de las `libpq.so*`. Verificar:
  - que `lib/` tiene `libpq.a`, `libpgcommon_shlib.a` y `libpgport_shlib.a`, y ninguna `libpq.so*`;
  - que `bin/pg_config --version` da 18.6;
  - que `/usr/pgsql-17` no ha cambiado.
- [x] 1.4 Comprobar los escenarios de la spec sobre el script completo:
  - una segunda ejecución sobre `~/software` termina sin descargar ni compilar nada;
  - con un `SOFTWARE_DIR` en `tmp/` y un tarball de Qt truncado en sus fuentes, el script falla antes de compilar, nombra el fichero y no deja nada en el prefijo.
- [x] 1.5 Documentar las dependencias estáticas:
  - `docs/architecture.md` §8: dos filas en la tabla de la toolchain (Qt 6.12.0 estático, libpq 18.6 estática) y una nota con sus opciones y los paquetes de sistema que necesitan;
  - AGENTS.md, entorno local: las dos rutas, `scripts/build-static-deps.sh` y los paquetes de xcb.

  Verificar que los comandos documentados coinciden con los del script.

## 2. Build estático del cliente

- [x] 2.1 Añadir la opción `GUITA_STATIC` al `CMakeLists.txt` raíz, según la decisión 6: la comprobación de que `Qt6::Core` es estático, `-static-libstdc++ -static-libgcc` en los ejecutables y nada de `CMAKE_BUILD_RPATH` con la opción encendida. Verificar:
  - que configurar con `GUITA_STATIC=ON` contra el kit compartido, en `tmp/`, falla sin compilar nada, con un mensaje que nombra ese `Qt6_DIR` y `~/software/Qt/6.12.0-static`;
  - que `cmake --preset native` configura igual que antes y que `readelf -d build/native/bin/server` sigue mostrando el `RUNPATH` a `gcc-16.2.0`.
- [x] 2.2 Enlazar la libpq estática en `src/backend/CMakeLists.txt` con la opción encendida (decisión 6): comprobar que `PostgreSQL_LIBRARY` es un `.a`, y añadir `pgcommon_shlib`, `pgport_shlib` y OpenSSL a `PostgreSQL::PostgreSQL`. Verificar que el build nativo sigue enlazando la `libpq.so.5` de `/usr/pgsql-17`.
- [x] 2.3 Añadir los presets `static` (decisión 7) y `qt_import_plugins` en `src/client/CMakeLists.txt` con la opción encendida (decisión 6), comprobando los nombres de los targets de plugins en el kit estático. Verificar:
  - que `cmake --preset static` y `cmake --build --preset static` compilan el cliente;
  - que `ldd build/static/bin/client` no muestra ninguna librería de Qt, de `libstdc++`, de `libgcc_s` ni de libpq;
  - que `scripts/build-wasm.sh` sigue compilando el cliente web, porque el cambio toca `src/client`.
- [x] 2.4 Crear `scripts/build-static.sh` según la decisión 8, con un modo `--check <ejecutable>` que solo hace la comprobación. Verificar:
  - que genera `build/dist/client` sin símbolos, termina sin errores y lista sus librerías `NEEDED`, todas del sistema;
  - que `scripts/build-static.sh --check build/native/bin/client` falla y nombra `libQt6Core.so.6`, `libstdc++.so.6`, `libpq.so.5` y el `RUNPATH`;
  - que con el Qt estático renombrado falla antes de configurar, remitiendo a `scripts/build-static-deps.sh`.
- [x] 2.5 Documentar el build estático:
  - `docs/architecture.md`: §8, los presets; §9.2, el ejecutable estático, qué sigue siendo dinámico y su comprobación; §12, la LGPL con Qt estático en escritorio (decisión 10);
  - AGENTS.md: que este build no forma parte de cada cambio, y que un módulo de Qt nuevo se añade también a `scripts/build-static-deps.sh`;
  - README: cómo obtener el ejecutable;
  - CHANGELOG: la entrada en `Added`.

  Verificar:
  - que `ctest --preset native` pasa completo;
  - que los pasos del README funcionan tal cual.

## 3. Verificación del ejecutable

- [x] 3.1 En esta máquina, sobre Xvfb, copiar `build/dist/client` a un directorio temporal y lanzarlo con `env -i` (solo `DISPLAY`, `HOME` y `LANG`) en modo conectado, contra `scripts/run-server.sh`. Verificar:
  - que lista las facturas y crea una nueva, que aparece en la lista;
  - que las capturas con `LANG=es_ES.UTF-8` y con `LANG=C` coinciden en estilo, vistas y textos con las del cliente nativo en el mismo idioma.

  Resultado: el ejecutable estático creó la factura 7 y la lista la mostró. Comparadas píxel a píxel, las capturas de los dos clientes solo difieren en 47 píxeles del suavizado de algunos glifos, en los dos idiomas: mismo estilo de Qt Quick Controls, mismas vistas y mismos textos. `pkill -f client` mató la propia shell de la prueba, cuya línea de órdenes contenía ese texto; `pkill -x client` compara solo el nombre del proceso.
- [x] 3.2 Repetirlo en modo standalone, con `--standalone` y `--db` hacia la base de datos de desarrollo. Verificar que lista y crea facturas, y que `ss -ltnp` no muestra ningún puerto abierto por el proceso.

  Resultado: creó la factura 8 sin ningún socket TCP ni UDP del proceso. Como esa base ya tenía las migraciones, se repitió contra un `postgres:18.6` vacío en un contenedor efímero: el cliente creó `invoices` y `schema_version` y guardó la factura 1.
- [x] 3.3 Ejecutarlo en un contenedor `ubuntu:24.04` con Xvfb y solo los paquetes de ejecución de X11, OpenGL y fontconfig, con el ejecutable montado de solo lectura y la red del host, en modo conectado. Verificar que la captura muestra la ventana con las facturas del servidor. Anotar en `docs/architecture.md` §9.2 la lista mínima de paquetes y el tamaño del ejecutable.

  Resultado: en lugar de un Xvfb dentro del contenedor, el cliente usó el Xvfb `:99` del host, con `/tmp/.X11-unix` montado de solo lectura. Lo que se comprueba es lo mismo: las librerías del ejecutable salen del contenedor. Arrancó, con glibc 2.39, y listó las facturas del servidor del host. Necesitó 60 paquetes sobre los 92 de la imagen: los de X11, xcb, xkbcommon, libglvnd, fontconfig, freetype, zlib y OpenSSL, el OpenGL de Mesa (no hay GPU) y una fuente. La lista y el tamaño (43 MB sin símbolos) están en §9.2.
- [x] 3.4 El usuario prueba `build/dist/client` en su escritorio, en los dos modos, y da el visto bueno.
