# Spec Delta

## REMOVED Requirements

### Requirement: Cobertura del comportamiento actual
**Reason**: Exigía cubrir dos codificaciones del API y la interoperabilidad entre dos generadores del contrato, que dejan de existir: el API solo es binario y el contrato se genera una vez para los dos lados.
**Migration**: La sustituye "Cobertura de las pruebas", con la misma cobertura salvo esos dos puntos, que pasan a ser una sola codificación y los bytes de referencia del contrato, comprobados en nativo y en WebAssembly.

## ADDED Requirements

### Requirement: Cobertura de las pruebas
La batería SHALL verificar el comportamiento que hoy ofrece el proyecto:
- las validaciones de negocio y la persistencia exacta de importes;
- las migraciones;
- el contrato RPC del API en su codificación binaria, con bytes de referencia del contrato;
- la autorización por token;
- el acceso del cliente al API por sus dos transportes (HTTP y en proceso);
- el análisis y formato de importes en la interfaz.

#### Scenario: Importes exactos
- **WHEN** se guardan y leen facturas con importes de 0,05, 1234,50 y el máximo admitido (9 999 999 999,99)
- **THEN** los importes leídos coinciden exactamente en céntimos con los guardados

#### Scenario: Contrato del API
- **WHEN** se llama a los procedimientos del API con peticiones válidas, sin token, con mensajes mal formados, con un cuerpo JSON u otro tipo de contenido no admitido, con un método HTTP distinto de POST o a un procedimiento inexistente
- **THEN** las respuestas tienen los estados y los errores documentados (200, 401, 400, 415, 405, 404) con sus códigos de error

#### Scenario: Bytes de referencia del contrato
- **WHEN** se codifican mensajes fijos del contrato, con campos opcionales ausentes y presentes y enteros de 64 bits en sus límites, y se decodifican los bytes de referencia, en nativo y en WebAssembly
- **THEN** los bytes coinciden con los de referencia guardados en las pruebas y el contenido decodificado con el original

#### Scenario: Transportes del cliente
- **WHEN** el cliente crea y lista facturas y recibe un error de validación por HTTP y en proceso
- **THEN** los resultados son los mismos con los dos transportes

#### Scenario: Sentencia incompatible con el esquema
- **WHEN** el backend arranca con una sentencia preparada que no casa con el esquema
- **THEN** el arranque falla con un error, antes de atender ninguna petición

#### Scenario: Importes en la interfaz
- **WHEN** se analizan importes escritos como `1234,5`, `1234.50`, `0`, `1.234,50` o `-1`
- **THEN** los tres primeros dan 123450, 123450 y 0 céntimos, y los dos últimos se rechazan
