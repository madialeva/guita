# toolchain Specification

## Purpose

Compiladores y estándar de C++ con los que se construye Guita: un único estándar, C++26, en los builds nativo y WebAssembly; un compilador nativo con la reflexión de C++26; un código de cliente que compilan los dos compiladores; y un compilador cruzado de la misma versión de GCC, bajo demanda, que genera desde Linux ejecutables de Windows que solo necesitan DLLs del sistema.

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
El build nativo SHALL usar GCC 16 compilado desde las fuentes GNU, en la versión y con las opciones de configuración que fija el proyecto, e instalado sin privilegios de root junto al resto de dependencias locales. La configuración nativa MUST fallar antes de compilar ningún fichero del proyecto si el compilador no es GCC 16 o posterior, o si no admite la reflexión de C++26. El mensaje de error MUST decir qué compilador se encontró y cuál se necesita.

#### Scenario: Compilador del proyecto
- **WHEN** se configura el preset `native` con el GCC 16 que instala el proyecto
- **THEN** la configuración termina sin errores

#### Scenario: Compilador antiguo
- **WHEN** se configura el build nativo con GCC 14
- **THEN** la configuración falla sin compilar ningún fichero del proyecto e indica que encontró GCC 14 y que se necesita GCC 16 con reflexión

#### Scenario: Instalar el compilador
- **WHEN** el desarrollador ejecuta el script de instalación del compilador en una máquina que no lo tiene
- **THEN** el compilador queda instalado sin root en el directorio de dependencias locales, con la versión y las opciones de configuración que fija el proyecto

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

### Requirement: Compilador cruzado para Windows
El proyecto SHALL incluir un script que compile e instale, sin privilegios de root y en el directorio de dependencias locales, un compilador cruzado de C y C++ que se ejecuta en Linux y genera ejecutables de Windows de 64 bits (x86_64). El compilador cruzado MUST ser la misma versión de GCC que el compilador nativo del proyecto, y MUST compilarse con ese compilador nativo desde las fuentes oficiales de GCC, binutils y mingw-w64, en las versiones y con las opciones de configuración que fija el script. El script MUST comprobar la firma de cada descarga antes de compilar nada, y MUST NOT volver a descargar ni compilar nada cuando el compilador cruzado ya está instalado. Una instalación que no terminó MUST NOT darse por instalada.

#### Scenario: Primera instalación
- **WHEN** el desarrollador ejecuta el script en una máquina con el compilador nativo del proyecto y sin el compilador cruzado
- **THEN** el compilador cruzado queda instalado sin root en el directorio de dependencias locales, informa de la misma versión de GCC que el compilador nativo, y el compilador nativo no cambia

#### Scenario: Segunda ejecución
- **WHEN** el desarrollador ejecuta el script con el compilador cruzado ya instalado
- **THEN** termina sin descargar ni compilar nada

#### Scenario: Descarga que no coincide con su firma
- **WHEN** un fichero descargado no coincide con su firma, o está firmado con otra clave que la que fija el script
- **THEN** el script falla antes de compilar, nombra el fichero y no instala nada

#### Scenario: Sin compilador nativo
- **WHEN** se ejecuta el script en una máquina sin el compilador nativo del proyecto
- **THEN** falla sin descargar ni compilar nada e indica qué script instala el compilador nativo

#### Scenario: Instalación interrumpida
- **WHEN** una ejecución anterior del script falló a mitad de la compilación
- **THEN** la siguiente ejecución no da el compilador cruzado por instalado y lo compila de nuevo desde el principio

### Requirement: Ejecutables de Windows sin DLLs del runtime
Los ejecutables que genera el compilador cruzado SHALL necesitar solo DLLs del sistema operativo: las del núcleo de Windows (como `KERNEL32.dll`) y las del runtime de C universal (UCRT) que trae Windows. El runtime de C++ y el de hilos MUST quedar dentro del ejecutable sin opciones de compilación ni de enlace adicionales. Los ejecutables MUST poder ejecutarse en Windows 10 (versión 1809 o posterior) y Windows 11 de 64 bits, los sistemas que admite Qt 6.12, y MUST admitir los hilos, las variables `thread_local` y las excepciones de C++.

#### Scenario: Programa con hilos y excepciones
- **WHEN** se compila sin opciones adicionales un programa en C++26 que crea hilos, usa una variable `thread_local` en cada uno y lanza y captura una excepción, y se ejecuta en Windows 10 u 11 de 64 bits o en Wine, sin ninguna DLL a su lado
- **THEN** termina con el resultado esperado

#### Scenario: DLLs que importa
- **WHEN** se listan las DLLs que importa un ejecutable generado sin opciones adicionales
- **THEN** solo aparecen DLLs del núcleo de Windows y del UCRT, y ninguna del runtime de GCC (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`) ni el runtime de C antiguo (`msvcrt.dll`)

### Requirement: Comprobación del compilador cruzado
La instalación del compilador cruzado SHALL terminar comprobándolo, y la comprobación SHALL poder repetirse a demanda sobre el compilador ya instalado. La comprobación MUST compilar un programa de prueba en C++26 que use hilos, variables `thread_local`, excepciones y punteros compartidos, y MUST fallar si el ejecutable necesita alguna DLL que no sea del sistema, nombrándola. La comprobación de las DLLs SHALL poder aplicarse también a un ejecutable dado. Si Wine está instalado, la comprobación MUST ejecutar el programa de prueba con él, sin abrir ventanas ni tocar la configuración de Wine del usuario, y MUST fallar si el programa no termina con el resultado esperado. Sin Wine, MUST avisar de que no lo ha ejecutado, sin fallar por ello.

#### Scenario: Compilador correcto
- **WHEN** se ejecuta la comprobación sobre el compilador cruzado instalado, en una máquina con Wine
- **THEN** termina sin errores y muestra las DLLs que importa el programa de prueba, todas del sistema, y la salida de ejecutarlo

#### Scenario: Una DLL que sobra
- **WHEN** se comprueba un ejecutable que importa una DLL que no es del sistema, como `libstdc++-6.dll`
- **THEN** la comprobación falla y nombra esa DLL

#### Scenario: Sin Wine
- **WHEN** se ejecuta la comprobación en una máquina sin Wine
- **THEN** comprueba las DLLs del programa de prueba, avisa de que no lo ha ejecutado y termina sin error si las DLLs son correctas

#### Scenario: Configuración de Wine del usuario
- **WHEN** la comprobación ejecuta el programa de prueba con Wine
- **THEN** no se abre ninguna ventana, y el prefijo de Wine del usuario (`~/.wine`) no se crea ni cambia

### Requirement: Compilador cruzado bajo demanda
El compilador cruzado SHALL instalarse solo cuando el desarrollador lo pide. El build nativo, el build WebAssembly, la batería de pruebas y el CI MUST NOT necesitarlo ni instalarlo. Un cambio MUST poder darse por terminado sin el compilador cruzado, salvo que modifique su propio script.

#### Scenario: Máquina sin el compilador cruzado
- **WHEN** se configura y se prueba el preset nativo en una máquina sin el compilador cruzado
- **THEN** se compila y `ctest --preset native` pasa como antes

#### Scenario: CI
- **WHEN** el CI se ejecuta sobre un push o un PR
- **THEN** no descarga ni compila el compilador cruzado

#### Scenario: Cambio que no toca el compilador cruzado
- **WHEN** un cambio modifica el backend o el cliente sin tocar el script del compilador cruzado
- **THEN** se da por terminado con las comprobaciones habituales, sin instalar el compilador cruzado
