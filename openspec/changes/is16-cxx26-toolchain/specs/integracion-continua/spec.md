# Spec Delta

## MODIFIED Requirements

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
