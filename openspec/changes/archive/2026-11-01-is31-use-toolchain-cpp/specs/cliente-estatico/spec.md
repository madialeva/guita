# Spec Delta

## REMOVED Requirements

### Requirement: Dependencias estáticas locales
**Reason**: el Qt estático y la libpq estática los construyen ahora los componentes `qt-static` y `libpq-static` de toolchain-cpp, y guita ya no tiene un script propio para ellos.
**Migration**: son los requisitos «Qt estático de Linux compartido» (spec `qt`) y «libpq estática» (spec `librerias`) de toolchain-cpp. El build estático los toma de su directorio de instalación (requisito «Toolchain de toolchain-cpp» de la spec `toolchain`).
