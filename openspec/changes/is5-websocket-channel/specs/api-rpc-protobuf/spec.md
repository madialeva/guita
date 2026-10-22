# Spec Delta

## ADDED Requirements

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

## MODIFIED Requirements

### Requirement: Autenticación en todos los procedimientos
Toda llamada que llegue por la red SHALL exigir la autorización configurada del backend (token Bearer), en todos los procedimientos: por HTTP en la cabecera `Authorization` de cada petición, y por el canal persistente en su primer mensaje. Sin una autorización válida la respuesta MUST ser un error `unauthenticated` (401 por HTTP), sin ejecutar el procedimiento. Las llamadas en proceso del modo standalone no llevan credenciales, porque solo existen dentro del propio ejecutable.

#### Scenario: Sin token
- **WHEN** se llama a un procedimiento sin cabecera `Authorization` o con un token incorrecto
- **THEN** la respuesta es un 401 con código `unauthenticated` y no se ejecuta ninguna operación

#### Scenario: Sin token en el canal
- **WHEN** se abre el canal persistente con un saludo sin token o con un token incorrecto
- **THEN** la respuesta es un error `unauthenticated`, el canal se cierra y no se ejecuta ninguna operación

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
