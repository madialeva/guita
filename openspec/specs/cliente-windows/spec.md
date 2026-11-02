# cliente-windows Specification

## Purpose

Ejecutable autocontenido del cliente para Windows de 64 bits: un único `client.exe`, compilado en Linux con los componentes para Windows de toolchain-cpp, con Qt, el backend del modo autónomo, libpq con TLS y el runtime de C++ enlazados estáticamente, que solo importa DLLs de Windows y se ejecuta sin instalar nada. Se genera bajo demanda, en la máquina del desarrollador, fuera del CI y del trabajo de cada cambio.

## Requirements

### Requirement: Dependencias para Windows de toolchain-cpp
El build para Windows SHALL tomar el compilador cruzado, el Qt estático para Windows y las librerías del cliente para Windows (la librería TLS de libpq, libpq, libpqxx, cpp-httplib y protobuf con abseil) de los componentes para Windows de toolchain-cpp, en la versión que fija el proyecto. El Qt MUST ser de la misma versión que el kit de escritorio del build nativo, que hace de host. El proyecto MUST NOT incluir scripts que compilen esas dependencias. Si falta alguna, la generación del ejecutable para Windows MUST fallar antes de configurar, nombrando lo que falta y los componentes de toolchain-cpp que hay que instalar.

#### Scenario: Componentes instalados
- **WHEN** el desarrollador ha instalado con toolchain-cpp, en la versión fijada, los componentes para Windows
- **THEN** el build para Windows los encuentra sin variables de entorno ni más pasos

#### Scenario: Sin los componentes para Windows
- **WHEN** se genera el ejecutable para Windows en una máquina sin el Qt para Windows o sin alguna de las librerías para Windows
- **THEN** falla sin configurar ni compilar nada, y nombra lo que falta y la orden de toolchain-cpp que lo instala

### Requirement: Ejecutable autocontenido para Windows
El build para Windows SHALL producir un único ejecutable del cliente para Windows de 64 bits que lleve dentro, enlazados estáticamente:
- Qt con los plugins y módulos QML que usa el cliente;
- el backend del modo autónomo;
- libpq con su TLS;
- el runtime de C++.

El ejecutable MUST importar solo DLLs que forman parte de Windows 10 (versión 1809 o posterior) y Windows 11. MUST arrancar sin ningún otro fichero a su lado, sin instalar nada y sin variables de entorno.

#### Scenario: Ejecutable copiado a un Windows
- **WHEN** se copia solo el ejecutable a un Windows 10 u 11 de 64 bits sin Qt, PostgreSQL ni ninguna herramienta de desarrollo, y se lanza
- **THEN** arranca y muestra la ventana del cliente

#### Scenario: DLLs que importa
- **WHEN** se listan las DLLs que importa el ejecutable
- **THEN** solo aparecen DLLs de Windows, y ninguna de Qt, del runtime de GCC, de OpenSSL ni de PostgreSQL

### Requirement: Comprobación del ejecutable para Windows
La generación del ejecutable para Windows SHALL fallar si el resultado importa alguna DLL que no esté en la lista de DLLs de Windows que admite el proyecto, y el error MUST nombrar cada una. La comprobación SHALL poder aplicarse también a un ejecutable dado. Configurar el build para Windows con un Qt que no sea el kit estático para Windows MUST fallar antes de compilar ningún fichero del proyecto, con un mensaje que diga qué Qt se encontró y cuál se necesita.

#### Scenario: Ejecutable correcto
- **WHEN** se genera el ejecutable para Windows con los componentes para Windows instalados
- **THEN** la generación termina sin errores y muestra las DLLs que importa el ejecutable, todas de Windows

#### Scenario: Una DLL que sobra
- **WHEN** se comprueba un ejecutable que importa `Qt6Core.dll` o `libstdc++-6.dll`
- **THEN** la comprobación falla y nombra esa DLL

#### Scenario: Otro Qt
- **WHEN** se configura el build para Windows contra un Qt que no es el kit estático para Windows
- **THEN** la configuración falla sin compilar ningún fichero del proyecto e indica qué Qt encontró y dónde se espera el kit para Windows

### Requirement: El mismo cliente en Windows
El ejecutable para Windows SHALL comportarse como el cliente nativo de desarrollo: los mismos modos de ejecución, conectado a un servidor y autónomo con el backend embebido, las mismas vistas y las mismas traducciones. En modo conectado MUST poder hablar con el servidor por `wss://`, con el TLS de Windows y sus certificados de confianza. El modo autónomo MUST seguir necesitando un servidor PostgreSQL accesible, y MUST poder conectarse a él con TLS cuando la cadena de conexión lo exige.

#### Scenario: Modo conectado
- **WHEN** se lanza el ejecutable para Windows contra un servidor en marcha
- **THEN** lista las facturas del servidor y crea una nueva, que aparece en la lista

#### Scenario: Modo autónomo
- **WHEN** se lanza el ejecutable para Windows con `--standalone` y la cadena de conexión de un PostgreSQL accesible
- **THEN** aplica las migraciones, lista las facturas y crea una nueva sin abrir ningún puerto de red

#### Scenario: Modo autónomo con TLS
- **WHEN** se lanza en modo autónomo con una cadena de conexión que exige TLS (`sslmode=require`) hacia un PostgreSQL con TLS activado
- **THEN** se conecta cifrando la conexión, y lista y crea facturas como sin TLS

#### Scenario: Modo conectado con TLS
- **WHEN** se lanza el ejecutable para Windows contra un servidor detrás de un proxy inverso con TLS (`wss://`), con un certificado en el que confía el sistema
- **THEN** lista las facturas del servidor y crea una nueva, como sin TLS

#### Scenario: Certificado en el que no confía el sistema
- **WHEN** se lanza en modo conectado contra un servidor con TLS cuyo certificado no es de confianza para el sistema
- **THEN** no se conecta, y el cliente indica que el servidor no está disponible, sin cerrarse

#### Scenario: Traducciones
- **WHEN** se lanza el ejecutable para Windows en un sistema con el idioma español
- **THEN** la interfaz se muestra en español, como el cliente nativo

### Requirement: Build para Windows bajo demanda
El build para Windows SHALL ejecutarse solo cuando el desarrollador lo pide. El build nativo, el build WebAssembly, la batería de pruebas y el CI MUST NOT necesitar los componentes para Windows de toolchain-cpp, ni el compilador cruzado, ni ejecutar el build para Windows. Un cambio MUST poder darse por terminado sin generar el ejecutable para Windows, salvo que modifique el propio build para Windows.

#### Scenario: Máquina sin los componentes para Windows
- **WHEN** se configura y se prueba el preset nativo en una máquina sin el compilador cruzado ni los componentes para Windows
- **THEN** se compila y `ctest --preset native` pasa como antes

#### Scenario: CI
- **WHEN** el CI se ejecuta sobre un push o un PR
- **THEN** no instala los componentes para Windows ni compila el ejecutable para Windows

#### Scenario: Cambio que no toca el build para Windows
- **WHEN** un cambio modifica el backend o el cliente sin tocar los scripts ni la configuración del build para Windows
- **THEN** se da por terminado con las comprobaciones habituales, sin generar el ejecutable para Windows
