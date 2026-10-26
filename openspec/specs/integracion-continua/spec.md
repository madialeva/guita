# integracion-continua Specification

## Purpose

Integración continua de Guita: valida automáticamente en GitHub Actions cada push a las ramas de desarrollo y de release y cada PR hacia ellas (compilación, batería de pruebas con PostgreSQL, compatibilidad del contrato y versión frente a rama), para que ningún cambio llegue a esas ramas con estas comprobaciones en rojo.

## Requirements

### Requirement: Cuándo se ejecuta
El CI SHALL ejecutarse en cada push a una rama `develop/**` o `release/**` y en cada PR cuya rama de destino sea una de ellas, y SHALL poder lanzarse a mano sobre cualquier rama. Una ejecución nueva de la misma rama o del mismo PR MUST cancelar la que siga en curso.

#### Scenario: PR hacia la rama de desarrollo
- **WHEN** se abre o se actualiza un PR de `change/is<n>-<slug>` hacia `develop/vX.Y.Z`
- **THEN** el CI se ejecuta sobre el resultado de fusionar el PR y su estado aparece en el PR

#### Scenario: Push a una rama de desarrollo o de release
- **WHEN** se hace push a `develop/vX.Y.Z` o a `release/vX.Y.Z`
- **THEN** el CI se ejecuta sobre ese commit

#### Scenario: Ejecución manual
- **WHEN** el desarrollador lanza el CI a mano sobre una rama de cambio
- **THEN** se ejecutan las mismas comprobaciones que en un PR, salvo la de la versión, que no tiene rama de referencia

### Requirement: Batería de pruebas completa con PostgreSQL
El CI SHALL compilar el proyecto con el preset nativo y ejecutar la batería completa de pruebas, incluidas las suites con base de datos y las del contrato, sin pantalla y con un PostgreSQL 18.6 propio de la ejecución. El CI MUST fallar si alguna suite falla o no llega a ejecutarse.

#### Scenario: Todo en verde
- **WHEN** el código compila y todas las suites pasan
- **THEN** el CI termina en verde

#### Scenario: Una prueba falla
- **WHEN** una suite falla, también una que necesita base de datos
- **THEN** el CI termina en rojo y su registro muestra la salida de la suite que falla

#### Scenario: Las mismas pruebas que en local
- **WHEN** el CI ejecuta la batería
- **THEN** ejecuta las mismas suites que `ctest --preset native` en local; solo cambia quién proporciona el servidor PostgreSQL

### Requirement: Compatibilidad del contrato
El CI SHALL comprobar en cada ejecución que el contrato (`src/proto`) sigue siendo compatible, en binario y en JSON, con el de la rama de desarrollo de su versión, y MUST fallar si no lo es.

#### Scenario: Cambio incompatible
- **WHEN** un PR borra un campo del contrato sin reservar su número y su nombre, o cambia su número o su tipo
- **THEN** el CI termina en rojo indicando el cambio incompatible

#### Scenario: Cambio compatible
- **WHEN** un PR añade un campo nuevo o reserva uno eliminado
- **THEN** la comprobación del contrato pasa

### Requirement: Versión frente a rama
El CI SHALL comprobar que la versión del proyecto declarada en `CMakeLists.txt` coincide con la de la rama `develop/vX.Y.Z` o `release/vX.Y.Z` en la que se hace push o hacia la que va el PR, y MUST fallar si no coincide. La misma comprobación SHALL poder ejecutarse en local.

#### Scenario: Versión distinta de la rama
- **WHEN** se hace push a `develop/v0.2.0` con la versión 0.1.0 en `CMakeLists.txt`
- **THEN** el CI termina en rojo indicando las dos versiones

#### Scenario: Rama sin versión
- **WHEN** la rama de referencia no es `develop/vX.Y.Z` ni `release/vX.Y.Z`
- **THEN** la comprobación no tiene con qué comparar y pasa

### Requirement: Toolchain reproducible y cacheada
El CI SHALL instalar Qt y las librerías que no vienen del sistema con los mismos scripts del repositorio que se usan en local, en las versiones que fijan esos scripts. El CI SHALL compilar con la misma versión de GCC, configurada con las mismas opciones, que instala en local el script del proyecto. La instalación SHALL reutilizarse entre ejecuciones mientras no cambien los scripts ni el compilador, también cuando una ejecución termina con pruebas fallidas.

#### Scenario: Segunda ejecución
- **WHEN** el CI se ejecuta de nuevo sin cambios en los scripts de instalación
- **THEN** reutiliza la toolchain instalada sin volver a compilar las librerías

#### Scenario: Cambio de versión de una dependencia
- **WHEN** un cambio modifica la versión de una dependencia en los scripts de instalación
- **THEN** el CI vuelve a instalar la toolchain con la versión nueva

#### Scenario: El mismo compilador que en local
- **WHEN** el CI compila el proyecto
- **THEN** usa la misma versión de GCC, con las mismas opciones de configuración, que instala el script local del compilador

#### Scenario: Cambio de versión de GCC
- **WHEN** un cambio sube la versión de GCC
- **THEN** la cambia a la vez en el script local y en el CI, y el CI vuelve a instalar la toolchain compilada con la versión nueva

### Requirement: Estado visible y exigible
El estado del CI de la rama de desarrollo SHALL verse en el README, y el repositorio SHALL documentar cómo exigir que el CI esté en verde para fusionar un PR en las ramas `develop/**` y `release/**`.

#### Scenario: Badge
- **WHEN** alguien abre el README
- **THEN** ve el estado de la última ejecución del CI en la rama por defecto

#### Scenario: PR en rojo
- **WHEN** el CI de un PR hacia `develop/vX.Y.Z` termina en rojo y la regla de la rama está configurada
- **THEN** GitHub no permite fusionar el PR
