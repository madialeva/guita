# Proposal

## Why

El proyecto sirve también para mostrar los últimos estándares de C++, y el siguiente paso previsto, un contenedor de inyección de dependencias con autowiring, necesita la reflexión de C++26: P2996, sus anotaciones (P3394), la reflexión de parámetros de función (P3096) y los expansion statements (P1306). GCC 16 es la primera versión que las trae (16.1 en abril de 2026, 16.2 en agosto). Hoy el proyecto compila en C++23 con GCC 14, en local y en el CI. `gcc-toolset-16` aún no ha llegado a Oracle Linux 9, y para `ubuntu-24.04` solo hay un snapshot anterior a 16.1. Es el issue #16.

## What Changes

- **GCC 16.2.0 compilado desde las fuentes GNU.** Un script nuevo, `scripts/build-gcc.sh`, lo instala sin root en `~/software/gcc-16.2.0` con las mismas opciones que la imagen oficial `gcc:16.2.0-trixie`. Así el compilador local y el del CI son la misma versión con la misma configuración.
- **C++26 en todo el proyecto** (`CMAKE_CXX_STANDARD 26`), también en el cliente WebAssembly: el Clang 23 de emsdk 5.0.5 lo compila entero en C++26 (comprobado).
- **El preset nativo usa ese GCC.** La configuración nativa falla al principio, con un mensaje claro, si el compilador no es GCC 16 o posterior o no acepta `-std=c++26 -freflection`. La reflexión todavía no se activa en ningún target.
- **Los ejecutables nativos llevan un rpath al `libstdc++` de GCC 16**, porque el del sistema es más antiguo. Vale para los del proyecto y para `protoc`, que así arrancan sin `LD_LIBRARY_PATH`.
- **`scripts/build-deps.sh` compila con GCC 16 y C++26:** libpqxx, protobuf y abseil, y también protobuf para WebAssembly en C++26. **BREAKING** para el entorno local: hay que volver a compilar las dependencias de `~/software`.
- **El CI se ejecuta en el contenedor `gcc:16.2.0-trixie`**, sobre el runner `ubuntu-24.04`, con el mismo GCC que en local. PostgreSQL se alcanza por el nombre del servicio y la caché de `~/software` cambia de clave.
- **Regla para el código del cliente:** solo puede usar lo que soportan a la vez GCC 16 y el Clang de emsdk. Por ejemplo, sí el indexado de packs, `= delete("motivo")` o `#embed`. La reflexión, los contratos, los expansion statements y casi toda la biblioteca nueva de C++26 quedan para el backend.

## Capabilities

### New Capabilities

- `toolchain`: compiladores y estándar con los que se construye el proyecto. Cubre C++26 en todos los builds, GCC 16 con comprobación temprana en el lado nativo, ejecutables nativos que arrancan sin preparar el entorno y el subconjunto de C++26 que puede usar el cliente.

### Modified Capabilities

- `integracion-continua`: el requisito "Toolchain reproducible y cacheada" pasa a exigir que el CI compile con la misma versión de GCC, y las mismas opciones de configuración, que se instala en local.

## Impact

- **Código y scripts:**
  - `CMakeLists.txt`: estándar, comprobación del compilador y rpath;
  - `CMakePresets.json`: compilador del preset nativo;
  - `scripts/build-gcc.sh` (nuevo) y `scripts/build-deps.sh`;
  - `.github/workflows/ci.yml`.
- **Dependencias:** ninguna de producción nueva. GCC 16.2.0 sustituye a GCC 14 como compilador nativo. libpqxx 8.0.2, protobuf 36.2 y abseil no cambian de versión, solo se vuelven a compilar.
- **Comportamiento:** ninguno de la aplicación, el API ni la base de datos.
- **Entorno local:** compilar GCC con `scripts/build-gcc.sh` (entre media hora y una hora con bootstrap completo), volver a compilar las dependencias con `scripts/build-deps.sh` y configurar de nuevo `build/native` desde cero.
- **Documentación:**
  - AGENTS.md: estándar, compilador, entorno local y CI;
  - README: requisitos e instalación;
  - `docs/architecture.md`: estructura del código (el proyecto deja de ser C++23), §8 (toolchain) y §10.4 (CI);
  - CHANGELOG: entrada en `Added`.
- **Otras issues:** #12 (build WASM en el CI) necesitará el mismo contenedor, porque `scripts/build-deps.sh` compilará con GCC 16 el `protoc` nativo que genera el código del build WASM.
