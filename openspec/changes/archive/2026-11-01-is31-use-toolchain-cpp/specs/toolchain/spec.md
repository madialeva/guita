# Spec Delta

## ADDED Requirements

### Requirement: Toolchain de toolchain-cpp
El proyecto SHALL obtener su compilador, sus kits de Qt y sus librerías de los componentes de toolchain-cpp, en una versión de toolchain-cpp que fija el proyecto, clonado junto a él. El proyecto MUST NOT incluir scripts que construyan esas dependencias. Los presets MUST encontrarlas sin variables de entorno, con rutas relativas al directorio del proyecto, y los scripts del proyecto MUST usar por defecto el mismo directorio de instalación.

#### Scenario: Máquina nueva
- **WHEN** el desarrollador clona toolchain-cpp junto al proyecto, en la versión fijada, e instala los componentes del build nativo
- **THEN** el preset nativo configura y compila sin más pasos, y `ctest --preset native` pasa

#### Scenario: toolchain-cpp en otro sitio
- **WHEN** el desarrollador tiene toolchain-cpp en otra ruta
- **THEN** puede usarlo cambiando las rutas en un `CMakeUserPresets.json` y la variable del directorio de instalación de los scripts, sin tocar ningún fichero versionado

## MODIFIED Requirements

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

## REMOVED Requirements

### Requirement: Compilador cruzado para Windows
**Reason**: el compilador cruzado lo construye ahora toolchain-cpp, como componente `gcc-mingw`.
**Migration**: es el requisito «Compilador cruzado para Windows» de la spec `compilador-cruzado` de toolchain-cpp. guita lo instalará con ese componente cuando lo use su build para Windows (#29).

### Requirement: Ejecutables de Windows sin DLLs del runtime
**Reason**: describe lo que genera el compilador cruzado, que ahora es de toolchain-cpp.
**Migration**: es el requisito del mismo nombre de la spec `compilador-cruzado` de toolchain-cpp.

### Requirement: Comprobación del compilador cruzado
**Reason**: la comprobación va con el componente `gcc-mingw` de toolchain-cpp.
**Migration**: es el requisito del mismo nombre de la spec `compilador-cruzado` de toolchain-cpp (`components/gcc-mingw.sh --check`).

### Requirement: Compilador cruzado bajo demanda
**Reason**: guita ya no tiene un script del compilador cruzado: es un componente de toolchain-cpp que solo se instala si se pide, y ni el build nativo, ni las pruebas, ni el CI de guita lo piden.
**Migration**: el build para Windows de #29 fijará cuándo se necesita.
