# Proposal

## Why

Hoy el cliente de escritorio solo arranca en la máquina que lo compiló. Carga tres cosas de fuera del ejecutable:
- las librerías compartidas de Qt y sus plugins, desde `~/software/Qt`;
- el `libstdc++` de GCC 16, por un run path absoluto a `~/software/gcc-16.2.0`;
- libpq, de los paquetes de PostgreSQL del sistema, con Kerberos, LDAP y SASL detrás.

El cliente web no tiene ese problema: `client.wasm` lleva Qt dentro y el navegador no necesita nada instalado. La idea es probar lo mismo en escritorio: un único ejecutable con todo enlazado estáticamente salvo las librerías del sistema operativo, que se pueda copiar a otro equipo Linux y ejecutar sin más. Es una prueba local y bajo demanda, que no entra en el CI ni en el trabajo de cada cambio. Es el issue #20.

## What Changes

- **Un Qt 6.12.0 estático compilado desde las fuentes.** Un script nuevo, `scripts/build-static-deps.sh`:
  - deja las fuentes en `~/software/src`;
  - compila con GCC 16.2 solo los módulos que usa el cliente (qtbase, qtshadertools, qtdeclarative, qtwebsockets y las herramientas de Linguist de qttools);
  - lo instala en `~/software/Qt/6.12.0-static`.

  Es la misma versión de Qt que el kit de escritorio, solo que estática.
- **Una libpq estática de PostgreSQL 18.6**, la versión del servidor de pruebas, en `~/software/libpq-18.6-static`. La compila el mismo script, sin GSSAPI ni LDAP.
- **Un preset `static`** (Release, `build/static`) que activa la opción `GUITA_STATIC`. Con ella el build:
  - comprueba que el Qt que encuentra es estático;
  - enlaza `libstdc++` y `libgcc` estáticas, sin run path;
  - enlaza la libpq estática;
  - fija los plugins de Qt que van dentro del ejecutable.
- **`scripts/build-static.sh`**, que compila solo el cliente y deja el ejecutable, sin símbolos, en `build/dist/client`. Falla si ese ejecutable necesita alguna librería compartida de Qt, del runtime de C++ o de PostgreSQL, alguna de `~/software`, o si lleva run path.
- **Lo que sigue siendo dinámico** son las librerías del sistema:
  - glibc 2.34 o posterior (la de Oracle Linux 9, donde se compila);
  - OpenGL/EGL, X11/xcb y xkbcommon;
  - fontconfig y freetype, y zlib;
  - OpenSSL 3, que necesita libpq. Qt la carga en tiempo de ejecución para `wss://`, y D-Bus también.
- **Fuera del flujo habitual:** el preset `native`, `ctest --preset native` y el CI no cambian ni necesitan el kit estático. Un cambio cualquiera no tiene que compilar este build.

## Capabilities

### New Capabilities

- `cliente-estatico`: el ejecutable autocontenido del cliente de escritorio. Cubre las dependencias estáticas locales (Qt y libpq), qué librerías puede seguir cargando el ejecutable y la comprobación que lo garantiza, los dos modos de ejecución dentro de él, y que es un build bajo demanda, fuera del CI y de los cambios ordinarios.

### Modified Capabilities

Ninguna. El build nativo de desarrollo, las pruebas y el CI no cambian de comportamiento.

## Impact

- **Código y scripts:**
  - `scripts/build-static-deps.sh` y `scripts/build-static.sh` (nuevos);
  - `CMakePresets.json`: el preset `static`;
  - `CMakeLists.txt`: la opción `GUITA_STATIC`, la comprobación del kit y el enlace estático del runtime de C++;
  - `src/backend/CMakeLists.txt`: las librerías que necesita la libpq estática;
  - `src/client/CMakeLists.txt`: los plugins de Qt del ejecutable estático.
- **Dependencias:** ninguna de producción nueva. Qt 6.12.0 y libpq ya lo son; cambia cómo se compilan para este build. Se descargan las fuentes de Qt 6.12.0 (unos 100 MB entre los cinco módulos) y de PostgreSQL 18.6.
- **Comportamiento:** ninguno de la aplicación, el API ni la base de datos. El ejecutable estático hace lo mismo que el nativo, en los dos modos.
- **Entorno local:**
  - instalar con `sudo` los paquetes de desarrollo de xcb que faltan para compilar Qt con X11: `xcb-util-*-devel` y `libxkbcommon-x11-devel`;
  - compilar Qt, del orden de una hora con 12 núcleos, con unos 15 GB de disco temporal que el script borra al acabar.
- **Documentación:**
  - `docs/architecture.md`: §8 (toolchain), §9.2 (escritorio) y §12 (licencias: Qt estático en escritorio, como en web);
  - README: cómo obtener el ejecutable;
  - AGENTS.md: entorno local y que este build no forma parte de cada cambio;
  - CHANGELOG: entrada en `Added`.
- **Otras issues:** ninguna depende de esta. Compilarlo en el CI queda para más adelante.
