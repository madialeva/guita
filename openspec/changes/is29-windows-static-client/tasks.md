# Tasks

## 1. toolchain-cpp v0.2.0

- [x] 1.1 Poner el clon de toolchain-cpp en `v0.2.0` y reinstalar lo que cambia (`install.sh qt`, por qtshadertools). Fijar la versión según la decisión 1: `TOOLCHAIN_CPP_REF` del CI, el README, AGENTS.md, `docs/architecture.md` §8 y §10.4, y la entrada del CHANGELOG que nombra la versión. Verificar:
  - que `ctest --preset native` pasa;
  - que `scripts/build-wasm.sh` compila;
  - que no queda ninguna referencia a v0.1.0 de toolchain-cpp fuera del CHANGELOG y de los changes archivados.

## 2. El build del cliente para Windows

- [ ] 2.1 Instalar con toolchain-cpp los componentes para Windows (`install.sh qt-mingw libpq-mingw libpqxx-mingw cpp-httplib-mingw protobuf-mingw`). Verificar que `install.sh --list` los da por instalados y que el compilador cruzado pasa su comprobación con el parche de las excepciones (`components/gcc-mingw.sh --check`).
- [ ] 2.2 Ajustar CMake según la decisión 4 y añadir los presets `windows` (decisión 3). Anotar en el diseño cualquier cambio que haya pedido el código para compilar en Windows. Verificar:
  - que `cmake --preset windows` y `cmake --build --preset windows` compilan `client.exe`;
  - que configurar con `GUITA_STATIC=ON` y el toolchain de Windows contra el kit de escritorio, en `tmp/`, falla sin compilar nada y nombra ese Qt y `qt-mingw`;
  - que `cmake --preset native` configura y compila igual que antes, y que `ctest --preset native` pasa;
  - que `scripts/build-wasm.sh` sigue compilando, porque el cambio toca `src/client` y `src/proto`.
- [ ] 2.3 Crear `scripts/build-windows.sh` según la decisión 2, con su lista de DLLs de Windows (decisión 6), cada una contrastada con la documentación de Microsoft. Verificar:
  - que genera `build/dist/client.exe` sin símbolos y termina listando sus DLLs, todas de la lista;
  - que `--check` sobre un ejecutable de `tmp/` que importa una DLL de prueba llamada `Qt6Core.dll` falla y la nombra;
  - que con un `SOFTWARE_DIR` de `tmp/` sin el kit `qt-mingw` falla antes de configurar, nombrando lo que falta y la orden de toolchain-cpp que lo instala.
- [ ] 2.4 Documentar según la decisión 8:
  - `docs/architecture.md`: §8, los componentes para Windows y el preset; §9.2, el ejecutable para Windows y su comprobación; §12, las licencias;
  - AGENTS.md: el entorno local;
  - README: la fila de Windows y cómo obtener el ejecutable;
  - CHANGELOG: `Added`.

  Verificar que coincide con el script y los presets, y que los pasos del README funcionan tal cual.

## 3. Verificación del ejecutable en Wine

Con Wine en el Xvfb `:99`, un prefijo temporal sin el perfil sRGB y `build/dist/client.exe` copiado a un directorio temporal (decisión 7). Si hizo falta otro backend de RHI, se anota en §9.2.

- [ ] 3.1 Modo conectado, contra `scripts/run-server.sh`. Verificar:
  - que lista las facturas y crea una nueva, que aparece en la lista;
  - que las capturas con `LANG=es_ES.UTF-8` y `LANG=C` muestran las mismas vistas y textos que el cliente nativo en el mismo idioma.
- [ ] 3.2 Modo autónomo, contra un `postgres:18.6` efímero vacío. Verificar que crea las tablas, lista y crea facturas, y que `ss -ltnp` no muestra ningún puerto abierto por los procesos de Wine.
- [ ] 3.3 Modo autónomo con TLS: el contenedor con `ssl=on`, un certificado autofirmado generado en `tmp/` y `sslmode=require` en la cadena de conexión. Verificar con `pg_stat_ssl`, desde `psql`, que la conexión del cliente va cifrada, y que con `ssl=off` el cliente falla con un error de TLS.
- [ ] 3.4 `wss://`: un HAProxy efímero delante de `scripts/run-server.sh`, con la configuración de §9.1 y un certificado autofirmado generado en `tmp/`. Verificar:
  - que, sin confiar en el certificado, el cliente no se conecta, indica que el servidor no está disponible y no se cierra;
  - que, con el certificado importado en el almacén Root de usuario del prefijo de Wine, lista y crea facturas. Si Wine no lo permite, anotarlo en §9.2 y dejar este caso para la tarea 4.1.

  Después se paran los contenedores y se borran los certificados.

## 4. Verificación final

- [ ] 4.1 El usuario copia solo `build/dist/client.exe` a un Windows 10 u 11 de 64 bits, y prueba los dos modos y `wss://` contra el HAProxy de la tarea 3.4, con el certificado importado con `certutil -user -addstore Root`.
- [ ] 4.2 El usuario hace push de la rama y el CI pasa, ya con toolchain-cpp v0.2.0.
- [ ] 4.3 El usuario revisa el cambio, incluida la verificación visual, y da el visto bueno.
