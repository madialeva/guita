# Spec Delta

## ADDED Requirements

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
