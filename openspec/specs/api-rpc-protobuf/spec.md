# api-rpc-protobuf Specification

## Purpose

Contrato tipado entre el cliente y el backend de Guita, definido una sola vez en Protobuf, y sus transportes: llamadas RPC unarias sobre HTTP compatibles con el protocolo Connect, con codificación binaria para el cliente y JSON para depurar, y llamadas en el propio proceso en el modo standalone.

## Requirements

### Requirement: Contrato único en Protobuf
El API entre cliente y backend SHALL estar definido en ficheros `.proto` versionados en el repositorio, en el paquete `guita.v1`, y el código de mensajes de ambos lados SHALL generarse a partir de esos ficheros en cada compilación. Ninguno de los dos lados MUST definir a mano la forma de los mensajes del API.

#### Scenario: Un campo nuevo llega a los dos lados
- **WHEN** se añade un campo a un mensaje del contrato y se recompila
- **THEN** el backend y el cliente disponen del campo sin escribir a mano ninguna definición del mensaje

#### Scenario: Mismos bytes en los dos lados
- **WHEN** el cliente codifica un mensaje del contrato y el backend lo decodifica, o al revés
- **THEN** se obtiene exactamente el mismo contenido, incluidos los campos opcionales ausentes y los enteros de 64 bits en sus límites

### Requirement: Procedimientos RPC sobre HTTP
Cada método del contrato SHALL atenderse como `POST /api/<paquete>.<Servicio>/<Método>` (por ejemplo, `/api/guita.v1.InvoiceService/ListInvoices`) y una llamada correcta SHALL responder con el estado 200 y el mensaje de respuesta del método. El API MUST NOT ofrecer las rutas REST anteriores (`/api/invoices`).

#### Scenario: Llamada correcta
- **WHEN** un cliente autenticado llama por POST a un procedimiento existente con un mensaje de petición válido
- **THEN** recibe un 200 con el mensaje de respuesta del método

#### Scenario: Procedimiento inexistente
- **WHEN** se llama a un procedimiento que el contrato no define
- **THEN** la respuesta es un 404 con un error de código `unimplemented`

#### Scenario: Método HTTP distinto de POST
- **WHEN** se llama a un procedimiento existente con un método HTTP distinto de POST
- **THEN** la respuesta es un 405 que indica que solo se admite POST

### Requirement: Codificaciones binaria y JSON
Cada procedimiento SHALL aceptar el mensaje de petición codificado en binario Protobuf (`Content-Type: application/proto`) o en JSON con el mapeo canónico de proto3 (`application/json`), y SHALL responder en la misma codificación de la petición. Con cualquier otro tipo de contenido la respuesta MUST ser un 415. En JSON, los campos desconocidos SHALL ignorarse.

#### Scenario: Petición binaria
- **WHEN** se llama a un procedimiento con un cuerpo `application/proto`
- **THEN** la respuesta es `application/proto` con el mensaje codificado en binario

#### Scenario: Petición JSON para depurar
- **WHEN** se llama a un procedimiento con un cuerpo `application/json` escrito a mano, con los nombres de campo en lowerCamelCase
- **THEN** la respuesta es `application/json` con el mensaje en el mapeo canónico de proto3

#### Scenario: Tipo de contenido no admitido
- **WHEN** se llama a un procedimiento con un cuerpo de otro tipo, por ejemplo `application/x-www-form-urlencoded`
- **THEN** la respuesta es un 415

#### Scenario: Campos desconocidos
- **WHEN** una petición JSON incluye un campo que el contrato no define
- **THEN** el campo se ignora y la llamada se atiende con normalidad

### Requirement: Modelo de errores común
Toda llamada fallida SHALL responder con un cuerpo JSON `{"code": ..., "message": ...}` con los códigos de error del protocolo Connect y su estado HTTP correspondiente: `invalid_argument` (400) para validaciones de negocio y mensajes mal formados, `unauthenticated` (401), `unimplemented` (404 para procedimientos inexistentes) e `internal` (500) para errores inesperados, que además se registran. El mensaje de las validaciones de negocio SHALL ser el del backend.

#### Scenario: Validación de negocio
- **WHEN** se crea una factura sin cliente
- **THEN** la respuesta es un 400 con código `invalid_argument` y el mensaje `Customer is required`

#### Scenario: Mensaje mal formado
- **WHEN** el cuerpo no se puede decodificar como el mensaje de petición del procedimiento, en binario o en JSON
- **THEN** la respuesta es un 400 con código `invalid_argument`

### Requirement: Autenticación en todos los procedimientos
Toda llamada que llegue por la red SHALL exigir la autorización configurada del backend (token Bearer en la cabecera `Authorization`), en todos los procedimientos. Sin una autorización válida la respuesta MUST ser un 401 con código `unauthenticated`, sin ejecutar el procedimiento. Las llamadas en proceso del modo standalone no llevan credenciales, porque solo existen dentro del propio ejecutable.

#### Scenario: Sin token
- **WHEN** se llama a un procedimiento sin cabecera `Authorization` o con un token incorrecto
- **THEN** la respuesta es un 401 con código `unauthenticated` y no se ejecuta ninguna operación

### Requirement: Compatibilidad con clientes Connect
Las llamadas unarias SHALL seguir el protocolo Connect, de modo que un cliente o herramienta compatible con Connect (por ejemplo, `buf curl`) pueda llamar al API usando `/api` como prefijo. La cabecera `Connect-Protocol-Version: 1` SHALL aceptarse, y su ausencia MUST NOT impedir la llamada.

#### Scenario: Herramienta Connect
- **WHEN** se lista las facturas con `buf curl` contra el servidor, indicando el esquema del contrato y la URL del procedimiento
- **THEN** la herramienta muestra el listado de facturas

### Requirement: Transporte en proceso en el modo standalone
En el modo standalone, el cliente SHALL llamar a los procedimientos del contrato dentro del propio proceso, sin red, con los mismos resultados y errores que por HTTP, y sin bloquear la interfaz mientras el backend atiende la llamada. En ese modo la aplicación MUST NOT escuchar en ningún puerto de red.

#### Scenario: Sin puertos abiertos
- **WHEN** la aplicación de escritorio arranca en modo standalone
- **THEN** no escucha en ningún puerto TCP, ni siquiera en `127.0.0.1`

#### Scenario: Mismo comportamiento que por HTTP
- **WHEN** se crean y listan facturas en modo standalone, incluida una factura sin cliente
- **THEN** los resultados y el error de validación (`invalid_argument`, `Customer is required`) son los mismos que se obtienen por HTTP

#### Scenario: Interfaz fluida
- **WHEN** una llamada en proceso tarda en completarse
- **THEN** la interfaz sigue respondiendo y el resultado llega cuando la llamada termina

### Requirement: Facturas en el contrato
`InvoiceService` SHALL ofrecer `ListInvoices`, que devuelve todas las facturas en orden de creación, y `CreateInvoice`, que aplica las validaciones de negocio y devuelve la factura creada. Los importes SHALL viajar como enteros de 64 bits en céntimos y el NIF del cliente SHALL ser un campo opcional: un NIF ausente o vacío se guarda y se devuelve como ausente.

#### Scenario: Crear y listar
- **WHEN** se crea una factura para `Acme S.L.` con NIF `B12345678` y 121000 céntimos y después se listan las facturas
- **THEN** la respuesta de creación y el listado contienen esa factura con su identificador, sus datos y el importe exacto en céntimos

#### Scenario: Sin NIF
- **WHEN** se crea una factura sin NIF o con NIF vacío
- **THEN** la factura creada y la listada no tienen NIF, distinto de un NIF vacío

### Requirement: Evolución controlada del contrato
El contrato SHALL cumplir unas reglas de estilo comprobadas automáticamente, y los cambios incompatibles respecto al contrato de la rama de desarrollo (reutilizar o renumerar campos, cambiar tipos, eliminar campos sin reservarlos) SHALL detectarse automáticamente.

#### Scenario: Estilo del contrato
- **WHEN** se ejecutan las comprobaciones del proyecto
- **THEN** el contrato pasa las reglas de estilo, y un mensaje que las incumpla hace fallar la comprobación

#### Scenario: Cambio incompatible
- **WHEN** se elimina o se renumera un campo de un mensaje sin reservar su número y se compara con el contrato de la rama de desarrollo
- **THEN** la comprobación de compatibilidad falla indicando el cambio
