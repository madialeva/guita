# api-rpc-protobuf Specification

## Purpose

Contrato tipado entre el cliente y el backend de Guita, definido una sola vez en Protobuf, con eventos del servidor y operaciones idempotentes, y sus transportes: llamadas RPC unarias sobre HTTP compatibles con el protocolo Connect, en binario, con herramientas que escriben y leen sus mensajes a mano; el canal persistente (capacidad `canal-websocket`); y llamadas y eventos en el propio proceso en el modo standalone.

## Requirements

### Requirement: Contrato único en Protobuf
El API entre cliente y backend SHALL estar definido en ficheros `.proto` versionados en el repositorio, en el paquete `guita.v1`, y el código de los mensajes SHALL generarse a partir de esos ficheros en cada compilación con un único generador, de modo que el backend y el cliente, en nativo y en WebAssembly, usen el mismo código del contrato. Ninguno de los dos lados MUST definir a mano la forma de los mensajes del API.

#### Scenario: Un campo nuevo llega a los dos lados
- **WHEN** se añade un campo a un mensaje del contrato y se recompila
- **THEN** el backend y el cliente disponen del campo sin escribir a mano ninguna definición del mensaje

#### Scenario: Mismos bytes en los dos lados
- **WHEN** se codifican los mismos mensajes del contrato en el backend nativo y en el cliente, nativo o WebAssembly
- **THEN** los bytes son idénticos y cada lado decodifica los del otro con el mismo contenido, incluidos los campos opcionales ausentes y los enteros de 64 bits en sus límites

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

### Requirement: Autenticación en todos los procedimientos
Toda llamada que llegue por la red SHALL exigir la autorización configurada del backend (token Bearer), en todos los procedimientos: por HTTP en la cabecera `Authorization` de cada petición, y por el canal persistente en su primer mensaje. Sin una autorización válida la respuesta MUST ser un error `unauthenticated` (401 por HTTP), sin ejecutar el procedimiento. Las llamadas en proceso del modo standalone no llevan credenciales, porque solo existen dentro del propio ejecutable.

#### Scenario: Sin token
- **WHEN** se llama a un procedimiento sin cabecera `Authorization` o con un token incorrecto
- **THEN** la respuesta es un 401 con código `unauthenticated` y no se ejecuta ninguna operación

#### Scenario: Sin token en el canal
- **WHEN** se abre el canal persistente con un saludo sin token o con un token incorrecto
- **THEN** la respuesta es un error `unauthenticated`, el canal se cierra y no se ejecuta ninguna operación

### Requirement: Compatibilidad con clientes Connect
Las llamadas unarias SHALL seguir el protocolo Connect, de modo que un cliente o herramienta compatible con Connect (por ejemplo, `buf curl`) pueda llamar al API usando `/api` como prefijo. La cabecera `Connect-Protocol-Version: 1` SHALL aceptarse, y su ausencia MUST NOT impedir la llamada.

#### Scenario: Herramienta Connect
- **WHEN** se lista las facturas con `buf curl` contra el servidor, indicando el esquema del contrato y la URL del procedimiento
- **THEN** la herramienta muestra el listado de facturas

### Requirement: Transporte en proceso en el modo standalone
En el modo standalone, el cliente SHALL llamar a los procedimientos del contrato y suscribirse a sus eventos dentro del propio proceso, sin red, con los mismos resultados, errores y eventos que por la red, y sin bloquear la interfaz mientras el backend atiende la llamada. En ese modo la aplicación MUST NOT escuchar en ningún puerto de red.

#### Scenario: Sin puertos abiertos
- **WHEN** la aplicación de escritorio arranca en modo standalone
- **THEN** no escucha en ningún puerto TCP, ni siquiera en `127.0.0.1`

#### Scenario: Mismo comportamiento que por HTTP
- **WHEN** se crean y listan facturas en modo standalone, incluida una factura sin cliente
- **THEN** los resultados y el error de validación (`invalid_argument`, `Customer is required`) son los mismos que se obtienen por HTTP y por el canal persistente

#### Scenario: Eventos en proceso
- **WHEN** en modo standalone una vista está suscrita a `WatchInvoices` y se crea una factura
- **THEN** la vista recibe el aviso de inicio y el de la factura creada, en el hilo de la interfaz, como por el canal

#### Scenario: Interfaz fluida
- **WHEN** una llamada en proceso tarda en completarse
- **THEN** la interfaz sigue respondiendo y el resultado llega cuando la llamada termina

### Requirement: Facturas en el contrato
`InvoiceService` SHALL ofrecer `ListInvoices`, que devuelve todas las facturas en orden de creación; `CreateInvoice`, que aplica las validaciones de negocio, admite una clave de idempotencia y devuelve la factura creada; y `WatchInvoices`, que avisa de las facturas creadas. Los importes SHALL viajar como enteros de 64 bits en céntimos y el NIF del cliente SHALL ser un campo opcional: un NIF ausente o vacío se guarda y se devuelve como ausente.

#### Scenario: Crear y listar
- **WHEN** se crea una factura para `Acme S.L.` con NIF `B12345678` y 121000 céntimos y después se listan las facturas
- **THEN** la respuesta de creación y el listado contienen esa factura con su identificador, sus datos y el importe exacto en céntimos

#### Scenario: Sin NIF
- **WHEN** se crea una factura sin NIF o con NIF vacío
- **THEN** la factura creada y la listada no tienen NIF, distinto de un NIF vacío

#### Scenario: Vista al día
- **WHEN** dos clientes muestran la lista de facturas y uno de ellos crea una
- **THEN** la lista del otro se actualiza sin que su usuario haga nada

### Requirement: Evolución controlada del contrato
El contrato SHALL cumplir unas reglas de estilo comprobadas automáticamente, y los cambios incompatibles respecto al contrato de la rama de desarrollo (reutilizar o renumerar campos, cambiar tipos, eliminar campos sin reservarlos) SHALL detectarse automáticamente.

#### Scenario: Estilo del contrato
- **WHEN** se ejecutan las comprobaciones del proyecto
- **THEN** el contrato pasa las reglas de estilo, y un mensaje que las incumpla hace fallar la comprobación

#### Scenario: Cambio incompatible
- **WHEN** se elimina o se renumera un campo de un mensaje sin reservar su número y se compara con el contrato de la rama de desarrollo
- **THEN** la comprobación de compatibilidad falla indicando el cambio

### Requirement: Depuración de los mensajes binarios
Los mensajes del API SHALL poder escribirse y leerse a mano sin que el API sirva JSON: una petición escrita en JSON o en el formato de texto de Protobuf SHALL poder codificarse con las herramientas del contrato y enviarse por HTTP, y una respuesta binaria SHALL poder mostrarse legible con ellas.

#### Scenario: Herramienta Connect con JSON local
- **WHEN** se lista las facturas con `buf curl` contra el servidor, escribiendo la petición en JSON
- **THEN** la herramienta envía la petición en binario y muestra el listado de facturas

#### Scenario: Mensajes binarios a mano
- **WHEN** se escribe una petición en el formato de texto de Protobuf, se codifica con `protoc --encode`, se envía con `curl` y se decodifica la respuesta con `protoc --decode`
- **THEN** se obtiene el mensaje de respuesta legible, con la factura creada o el listado

### Requirement: Eventos del servidor
El contrato SHALL definir los eventos del servidor como procedimientos de *server streaming* de sus servicios, con una petición y una respuesta propias como cualquier otro método. `InvoiceService.WatchInvoices` SHALL enviar primero un aviso de inicio y después un aviso por cada factura creada, con la factura, la cree el cliente que sea y por el transporte que sea, solo cuando la creación se ha confirmado en la base de datos. Una creación rechazada o repetida con la misma clave de idempotencia MUST NOT producir ningún aviso. Los eventos se entregan como mucho una vez: los que se producen mientras un suscriptor no está conectado se pierden, y el aviso de inicio indica al suscriptor cuándo debe recargar lo que muestra. Los procedimientos de eventos SHALL servirse por el canal persistente y en proceso; por HTTP MUST responderse con un error `unimplemented`.

#### Scenario: Factura creada por otro cliente
- **WHEN** un cliente está suscrito a `WatchInvoices` y otro crea una factura por HTTP
- **THEN** el suscrito recibe un aviso con la factura creada, con su identificador y sus datos

#### Scenario: Aviso de inicio
- **WHEN** un cliente se suscribe a `WatchInvoices`
- **THEN** el primer mensaje que recibe es el aviso de inicio, y toda factura creada después le llega como aviso

#### Scenario: Sin aviso para creaciones fallidas
- **WHEN** se intenta crear una factura sin cliente, o se repite una creación con la misma clave de idempotencia
- **THEN** los suscriptores no reciben ningún aviso

#### Scenario: Eventos por HTTP
- **WHEN** se llama por `POST` a `/api/guita.v1.InvoiceService/WatchInvoices`
- **THEN** la respuesta es un 404 con un error de código `unimplemented` que indica que el procedimiento solo se sirve por el canal

### Requirement: Operaciones idempotentes
Las operaciones del contrato que crean o modifican SHALL admitir una clave de idempotencia (`request_id`, un UUID elegido por quien llama). Si llega otra petición con una clave ya usada en la misma operación, el backend SHALL devolver el resultado de la primera sin volver a aplicarla, también cuando las dos llegan a la vez y por transportes distintos. Una clave reutilizada con datos distintos, o que no es un UUID, MUST rechazarse con `invalid_argument`. Sin clave, cada petición se aplica.

#### Scenario: Creación repetida
- **WHEN** se envía dos veces `CreateInvoice` con los mismos datos y el mismo `request_id`
- **THEN** las dos respuestas contienen la misma factura, con el mismo identificador, y en la base de datos hay una sola

#### Scenario: Creaciones simultáneas con la misma clave
- **WHEN** se envía la misma creación con el mismo `request_id` a la vez desde varios hilos
- **THEN** todas las respuestas contienen la misma factura y en la base de datos hay una sola

#### Scenario: Clave reutilizada con otros datos
- **WHEN** se envía `CreateInvoice` con un `request_id` ya usado y otro importe
- **THEN** la respuesta es un error `invalid_argument` y no se crea ninguna factura

#### Scenario: Clave no válida
- **WHEN** se envía `CreateInvoice` con un `request_id` que no es un UUID
- **THEN** la respuesta es un error `invalid_argument`

#### Scenario: Sin clave
- **WHEN** se envía dos veces `CreateInvoice` con los mismos datos y sin `request_id`
- **THEN** se crean dos facturas
