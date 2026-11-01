# cliente-estatico Specification

## Purpose

Ejecutable autocontenido del cliente de escritorio: un único fichero, con Qt, el backend, libpq y el runtime de C++ enlazados estáticamente, que se puede copiar a otro equipo Linux y ejecutar sin instalar Qt ni la toolchain del proyecto (toolchain-cpp). Se genera bajo demanda, en la máquina del desarrollador, fuera del CI y del trabajo de cada cambio.

## Requirements

### Requirement: Ejecutable autocontenido
El build estático SHALL producir un único ejecutable del cliente que lleve dentro, enlazados estáticamente, Qt con los plugins y módulos QML que usa el cliente, el backend del modo standalone, libpq y el runtime de C++. Sus dependencias dinámicas MUST ser solo librerías del sistema operativo: glibc, OpenGL/EGL, las de X11, fontconfig, freetype, zlib, OpenSSL y las que estas arrastran. El ejecutable MUST NOT llevar run path ni depender de ninguna ruta del directorio de dependencias locales ni del árbol de build.

#### Scenario: Ejecutable copiado a otro directorio
- **WHEN** se copia el ejecutable a un directorio cualquiera y se lanza desde una shell sin `LD_LIBRARY_PATH` ni variables de Qt
- **THEN** arranca y muestra la ventana del cliente, sin `qt.conf` ni ningún otro fichero a su lado

#### Scenario: Otra distribución sin las dependencias locales
- **WHEN** se ejecuta el ejecutable en un sistema Linux de otra distribución, con glibc igual o posterior a la de la máquina que lo compiló, sus librerías de escritorio y ningún fichero del directorio de dependencias locales
- **THEN** arranca y muestra la ventana del cliente

### Requirement: Comprobación del ejecutable
La generación del ejecutable estático SHALL fallar si el resultado necesita una librería compartida de Qt, del runtime de C++ o de PostgreSQL, si necesita una librería del directorio de dependencias locales o si lleva run path. El error MUST nombrar cada librería o run path que sobra. Configurar el build estático con un Qt que no es estático MUST fallar antes de compilar ningún fichero del proyecto, con un mensaje que diga qué Qt se encontró y cuál se necesita.

#### Scenario: Ejecutable correcto
- **WHEN** se genera el ejecutable estático con las dependencias estáticas instaladas
- **THEN** la generación termina sin errores y muestra las librerías dinámicas que necesita el ejecutable, todas del sistema operativo

#### Scenario: Una librería compartida que sobra
- **WHEN** el ejecutable resultante necesita `libQt6Core.so.6` o `libstdc++.so.6`
- **THEN** la generación falla y nombra esa librería

#### Scenario: Kit de Qt compartido
- **WHEN** se configura el build estático contra el kit de escritorio compartido
- **THEN** la configuración falla sin compilar ningún fichero del proyecto e indica que ese Qt no es estático y dónde se espera el Qt estático

### Requirement: El mismo cliente en los dos modos
El ejecutable estático SHALL comportarse como el cliente nativo de desarrollo: los mismos modos de ejecución, conectado a un servidor y standalone con el backend embebido, las mismas vistas y controles y las mismas traducciones. El modo standalone MUST seguir necesitando un servidor PostgreSQL accesible, como el nativo.

#### Scenario: Modo conectado
- **WHEN** se lanza el ejecutable estático contra un servidor en marcha
- **THEN** lista las facturas del servidor y crea una nueva, que aparece en la lista

#### Scenario: Modo standalone
- **WHEN** se lanza el ejecutable estático con `--standalone` y la cadena de conexión de un PostgreSQL accesible
- **THEN** aplica las migraciones, lista las facturas y crea una nueva sin abrir ningún puerto de red

#### Scenario: Traducciones
- **WHEN** se lanza el ejecutable estático con un idioma español en el entorno
- **THEN** la interfaz se muestra en español, como el cliente nativo

### Requirement: Build bajo demanda
El build estático SHALL ejecutarse solo cuando el desarrollador lo pide. El build nativo, la batería de pruebas y el CI MUST NOT necesitar el Qt estático ni la libpq estática, ni ejecutar el build estático. Un cambio MUST poder darse por terminado sin generar el ejecutable estático, salvo que modifique el propio build estático.

#### Scenario: Máquina sin las dependencias estáticas
- **WHEN** se configura y se prueba el preset nativo en una máquina sin el Qt estático ni la libpq estática
- **THEN** se compila y `ctest --preset native` pasa como antes

#### Scenario: CI
- **WHEN** el CI se ejecuta sobre un push o un PR
- **THEN** no descarga ni compila las dependencias estáticas ni el ejecutable estático

#### Scenario: Cambio que no toca el build estático
- **WHEN** un cambio modifica el backend o el cliente sin tocar los scripts ni la configuración del build estático
- **THEN** se da por terminado con las comprobaciones habituales, sin generar el ejecutable estático
