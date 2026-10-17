# Spec Delta

## MODIFIED Requirements

### Requirement: Cobertura del comportamiento actual
La batería SHALL verificar el comportamiento que hoy ofrece el proyecto: las validaciones de negocio, la persistencia exacta de importes, las migraciones, el contrato RPC del API en sus dos codificaciones, la interoperabilidad entre el código generado para el backend y para el cliente, la autorización por token, el acceso del cliente al API por sus dos transportes (HTTP y en proceso) y el análisis y formato de importes en la interfaz.

#### Scenario: Importes exactos
- **WHEN** se guardan y leen facturas con importes de 0,05, 1234,50 y el máximo admitido (9 999 999 999,99)
- **THEN** los importes leídos coinciden exactamente en céntimos con los guardados

#### Scenario: Contrato del API
- **WHEN** se llama a los procedimientos del API en binario y en JSON con peticiones válidas, sin token, con mensajes mal formados, con un tipo de contenido no admitido, con un método HTTP distinto de POST o a un procedimiento inexistente
- **THEN** las respuestas tienen los estados y los errores documentados (200, 401, 400, 415, 405, 404) con sus códigos de error

#### Scenario: Interoperabilidad de los generadores
- **WHEN** se codifica un mensaje del contrato con el código generado para un lado y se decodifica con el del otro
- **THEN** el contenido coincide, incluidos los campos opcionales ausentes y los enteros de 64 bits en sus límites

#### Scenario: Transportes del cliente
- **WHEN** el cliente crea y lista facturas y recibe un error de validación por HTTP y en proceso
- **THEN** los resultados son los mismos con los dos transportes

#### Scenario: Sentencia incompatible con el esquema
- **WHEN** el backend arranca con una sentencia preparada que no casa con el esquema
- **THEN** el arranque falla con un error, antes de atender ninguna petición

#### Scenario: Importes en la interfaz
- **WHEN** se analizan importes escritos como `1234,5`, `1234.50`, `0`, `1.234,50` o `-1`
- **THEN** los tres primeros dan 123450, 123450 y 0 céntimos, y los dos últimos se rechazan
