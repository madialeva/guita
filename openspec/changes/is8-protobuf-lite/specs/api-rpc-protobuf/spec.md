# Spec Delta

## RENAMED Requirements

- FROM: `### Requirement: Codificaciones binaria y JSON`
- TO: `### Requirement: Codificación binaria`

## MODIFIED Requirements

### Requirement: Contrato único en Protobuf
El API entre cliente y backend SHALL estar definido en ficheros `.proto` versionados en el repositorio, en el paquete `guita.v1`, y el código de los mensajes SHALL generarse a partir de esos ficheros en cada compilación con un único generador, de modo que el backend y el cliente, en nativo y en WebAssembly, usen el mismo código del contrato. Ninguno de los dos lados MUST definir a mano la forma de los mensajes del API.

#### Scenario: Un campo nuevo llega a los dos lados
- **WHEN** se añade un campo a un mensaje del contrato y se recompila
- **THEN** el backend y el cliente disponen del campo sin escribir a mano ninguna definición del mensaje

#### Scenario: Mismos bytes en los dos lados
- **WHEN** se codifican los mismos mensajes del contrato en el backend nativo y en el cliente, nativo o WebAssembly
- **THEN** los bytes son idénticos y cada lado decodifica los del otro con el mismo contenido, incluidos los campos opcionales ausentes y los enteros de 64 bits en sus límites

### Requirement: Codificación binaria
Cada procedimiento SHALL aceptar el mensaje de petición codificado en binario Protobuf (`Content-Type: application/proto`) y SHALL responder en binario Protobuf. Con cualquier otro tipo de contenido, también `application/json`, la respuesta MUST ser un 415. Los campos que el contrato no define SHALL ignorarse.

#### Scenario: Petición binaria
- **WHEN** se llama a un procedimiento con un cuerpo `application/proto`
- **THEN** la respuesta es `application/proto` con el mensaje codificado en binario

#### Scenario: Petición JSON para depurar
- **WHEN** se llama a un procedimiento con un cuerpo `application/json` escrito a mano
- **THEN** la respuesta es un 415, y para depurar con JSON se usa una herramienta que lo convierte a binario antes de enviarlo (requisito "Depuración de los mensajes binarios")

#### Scenario: Tipo de contenido no admitido
- **WHEN** se llama a un procedimiento con un cuerpo de otro tipo, por ejemplo `application/x-www-form-urlencoded`
- **THEN** la respuesta es un 415

#### Scenario: Campos desconocidos
- **WHEN** una petición binaria incluye un campo que el contrato no define, por ejemplo de una versión posterior
- **THEN** el campo se ignora y la llamada se atiende con normalidad

### Requirement: Modelo de errores común
Toda llamada fallida SHALL responder con un cuerpo JSON `{"code": ..., "message": ...}` con los códigos de error del protocolo Connect y su estado HTTP correspondiente: `invalid_argument` (400) para validaciones de negocio y mensajes mal formados, `unauthenticated` (401), `unimplemented` (404 para procedimientos inexistentes) e `internal` (500) para errores inesperados, que además se registran. El mensaje de las validaciones de negocio SHALL ser el del backend.

#### Scenario: Validación de negocio
- **WHEN** se crea una factura sin cliente
- **THEN** la respuesta es un 400 con código `invalid_argument` y el mensaje `Customer is required`

#### Scenario: Mensaje mal formado
- **WHEN** el cuerpo no se puede decodificar como el mensaje binario de petición del procedimiento
- **THEN** la respuesta es un 400 con código `invalid_argument`

#### Scenario: Mensaje de error con caracteres especiales
- **WHEN** el mensaje de un error contiene comillas, barras invertidas o caracteres no ASCII
- **THEN** el cuerpo del error sigue siendo JSON válido y el mensaje se lee igual

## ADDED Requirements

### Requirement: Depuración de los mensajes binarios
Los mensajes del API SHALL poder escribirse y leerse a mano sin que el API sirva JSON: una petición escrita en JSON o en el formato de texto de Protobuf SHALL poder codificarse con las herramientas del contrato y enviarse por HTTP, y una respuesta binaria SHALL poder mostrarse legible con ellas.

#### Scenario: Herramienta Connect con JSON local
- **WHEN** se lista las facturas con `buf curl` contra el servidor, escribiendo la petición en JSON
- **THEN** la herramienta envía la petición en binario y muestra el listado de facturas

#### Scenario: Mensajes binarios a mano
- **WHEN** se escribe una petición en el formato de texto de Protobuf, se codifica con `protoc --encode`, se envía con `curl` y se decodifica la respuesta con `protoc --decode`
- **THEN** se obtiene el mensaje de respuesta legible, con la factura creada o el listado
