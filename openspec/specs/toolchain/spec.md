# toolchain Specification

## Purpose

Compiladores y estándar de C++ con los que se construye Guita: un único estándar, C++26, en los builds nativo y WebAssembly; un compilador nativo con la reflexión de C++26; un código de cliente que compilan los dos compiladores; y la toolchain, que viene de toolchain-cpp en una versión fijada.

## Requirements

### Requirement: C++26 en todos los builds
El proyecto SHALL compilarse en C++26 en todos sus builds: el nativo (backend, cliente de escritorio, pruebas y mediciones) y el WebAssembly (cliente web). Las dependencias que comparten tipos de la biblioteca estándar con el proyecto, protobuf y abseil, MUST compilarse en cada build con el mismo estándar que el proyecto.

#### Scenario: Build nativo
- **WHEN** se configura y se compila el preset `native`
- **THEN** todo el código del proyecto se compila en C++26

#### Scenario: Build WebAssembly
- **WHEN** se compila el cliente web con el preset `wasm`
- **THEN** se compila en C++26 con el compilador de Emscripten y enlaza con protobuf y abseil compilados también en C++26

### Requirement: Compilador nativo con reflexión
El build nativo SHALL usar el GCC 16 del componente de compilador nativo de toolchain-cpp, en la versión de toolchain-cpp que fija el proyecto. La configuración nativa MUST fallar antes de compilar ningún fichero del proyecto si el compilador no es GCC 16 o posterior, o si no admite la reflexión de C++26. El mensaje de error MUST decir qué compilador se encontró y cuál se necesita.

#### Scenario: Compilador del proyecto
- **WHEN** se configura el preset `native` con el GCC 16 de toolchain-cpp
- **THEN** la configuración termina sin errores

#### Scenario: Compilador antiguo
- **WHEN** se configura el build nativo con GCC 14
- **THEN** la configuración falla sin compilar ningún fichero del proyecto e indica que encontró GCC 14 y que se necesita GCC 16 con reflexión

#### Scenario: Instalar el compilador
- **WHEN** el desarrollador instala el componente de compilador nativo de toolchain-cpp, en la versión que fija el proyecto, en una máquina que no lo tiene
- **THEN** el compilador queda instalado sin root, y el preset nativo lo encuentra sin más configuración

### Requirement: Ejecutables nativos sin preparar el entorno
Los ejecutables nativos compilados con ese GCC SHALL arrancar sin variables de entorno ni configuración del sistema adicionales, aunque la biblioteca estándar de C++ del sistema sea más antigua que la del compilador. Esto incluye el servidor, el cliente de escritorio, las pruebas, las mediciones y el `protoc` de las dependencias locales.

#### Scenario: Servidor en una shell nueva
- **WHEN** se ejecuta el servidor compilado con el preset `native` desde una shell sin `LD_LIBRARY_PATH`
- **THEN** arranca usando la biblioteca estándar de C++ de GCC 16

#### Scenario: protoc durante la compilación
- **WHEN** el build genera el código del contrato con el `protoc` que compilan los scripts del proyecto
- **THEN** `protoc` se ejecuta sin errores al cargar sus bibliotecas

#### Scenario: Pruebas
- **WHEN** se ejecuta `ctest --preset native` desde una shell sin `LD_LIBRARY_PATH`
- **THEN** todas las suites arrancan

### Requirement: Subconjunto de C++26 en el cliente
El código del cliente SHALL usar solo lo que admiten a la vez el compilador nativo y el de Emscripten, porque los dos compilan el mismo código. La reflexión, los contratos, los expansion statements y las partes de la biblioteca de C++26 que no trae la biblioteca de Emscripten MUST quedar fuera del cliente. El backend, que solo se compila en nativo, SHALL poder usar todo lo que admite el compilador nativo.

#### Scenario: Cliente en los dos builds
- **WHEN** se compila el cliente con el preset `native` y con el preset `wasm`
- **THEN** los dos builds compilan el mismo código del cliente sin errores

#### Scenario: Una característica que solo tiene GCC, en el cliente
- **WHEN** un cambio usa expansion statements (`template for`) en el código del cliente
- **THEN** el build WebAssembly falla, y el cambio no se da por terminado hasta que el cliente vuelve a compilar en los dos builds

#### Scenario: La misma característica en el backend
- **WHEN** un cambio usa expansion statements en el código del backend
- **THEN** el build nativo lo compila, y el build WebAssembly no se ve afectado porque no incluye el backend

### Requirement: Toolchain de toolchain-cpp
El proyecto SHALL obtener su compilador, sus kits de Qt y sus librerías de los componentes de toolchain-cpp, en una versión de toolchain-cpp que fija el proyecto, clonado junto a él. El proyecto MUST NOT incluir scripts que construyan esas dependencias. Los presets MUST encontrarlas sin variables de entorno, con rutas relativas al directorio del proyecto, y los scripts del proyecto MUST usar por defecto el mismo directorio de instalación.

#### Scenario: Máquina nueva
- **WHEN** el desarrollador clona toolchain-cpp junto al proyecto, en la versión fijada, e instala los componentes del build nativo
- **THEN** el preset nativo configura y compila sin más pasos, y `ctest --preset native` pasa

#### Scenario: toolchain-cpp en otro sitio
- **WHEN** el desarrollador tiene toolchain-cpp en otra ruta
- **THEN** puede usarlo cambiando las rutas en un `CMakeUserPresets.json` y la variable del directorio de instalación de los scripts, sin tocar ningún fichero versionado
