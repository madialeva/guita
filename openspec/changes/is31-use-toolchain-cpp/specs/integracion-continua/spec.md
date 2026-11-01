# Spec Delta

## MODIFIED Requirements

### Requirement: Toolchain reproducible y cacheada
El CI SHALL instalar Qt y las librerías que no vienen del sistema con los componentes de toolchain-cpp, en la misma versión de toolchain-cpp que fija el proyecto para el entorno local. El CI SHALL compilar con la misma versión de GCC, configurada con las mismas opciones, que el componente de compilador nativo de esa versión de toolchain-cpp. La instalación SHALL reutilizarse entre ejecuciones mientras no cambien esa versión, los componentes ni el compilador, también cuando una ejecución termina con pruebas fallidas.

#### Scenario: Segunda ejecución
- **WHEN** el CI se ejecuta de nuevo sin cambiar la versión de toolchain-cpp ni sus componentes
- **THEN** reutiliza la toolchain instalada sin volver a compilar las librerías

#### Scenario: Cambio de versión de una dependencia
- **WHEN** un cambio fija otra versión de toolchain-cpp, que trae otra versión de una dependencia
- **THEN** el CI vuelve a instalar la toolchain con esa versión

#### Scenario: El mismo compilador que en local
- **WHEN** el CI compila el proyecto
- **THEN** usa la misma versión de GCC, con las mismas opciones de configuración, que el componente de compilador nativo de la versión fijada de toolchain-cpp

#### Scenario: Cambio de versión de GCC
- **WHEN** una versión nueva de toolchain-cpp trae otra versión de GCC
- **THEN** el cambio que la fija cambia a la vez la imagen del CI, y el CI falla si la imagen y toolchain-cpp no coinciden
