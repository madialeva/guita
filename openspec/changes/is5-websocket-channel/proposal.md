# Proposal

## Why

Hoy el cliente solo sabe del backend lo que pregunta: si otro usuario crea una factura, la lista de los demás no cambia hasta que vuelven a pedirla. #2 dejó el API independiente del transporte (`RpcRouter` recibe un `RpcCall` y devuelve un `RpcResult`) precisamente para poder añadir un canal persistente con eventos del servidor, y las mediciones de #2 muestran que ese canal no se justifica por latencia (un frame de WebSocket con cpp-httplib no es más rápido que HTTP con *keep-alive*) sino por el *push* y por una única conexión de larga vida. Con el CI ya en marcha (#4), que validará este cambio, el más grande hasta ahora y uno que toca `src/proto/`, es el momento de afrontarlo. Es el issue #5.

## What Changes

- **Eventos del servidor en el contrato**: un procedimiento de eventos, `InvoiceService.WatchInvoices`, de *server streaming* en el `.proto`, que avisa de cada factura creada, la cree quien la cree y por el transporte que sea. Es el evento real que justifica el canal: cuando un cliente crea una factura, los demás refrescan su lista sin pedirlo.
- **Canal WebSocket persistente** como tercer transporte del backend, junto a HTTP y a la llamada en proceso: un sobre definido en Protobuf (`guita.channel.v1`: llamada con `correlation_id`, procedimiento y mensaje; respuesta, error o mensaje de evento correlacionados) que se traduce a `RpcCall` y reutiliza `RpcRouter` y los procedimientos existentes sin cambiarlos. Varias llamadas concurrentes por conexión, respondidas en el orden en que terminan.
- **Autenticación en el primer mensaje** del canal: el navegador no puede poner cabeceras en el *handshake* de un WebSocket, así que el cliente envía la autorización (`Bearer ...`, la misma que comprueba hoy el `Authorizer`) en un primer frame, sin el cual el servidor no atiende ninguna llamada.
- **Claves de idempotencia** en las operaciones que crean o modifican: `CreateInvoiceRequest` gana un `request_id` (UUID) opcional; el backend guarda la clave con la factura en la misma transacción y, si recibe otra vez la misma, devuelve la factura ya creada en lugar de crear otra. Sirve igual por HTTP, en proceso y por el canal. Al no haber ninguna versión publicada, la columna se añade a la migración inicial en lugar de en una nueva.
- **Cliente sobre el canal en los modos web y conectado**: un `WebSocketTransport` (Qt WebSockets, nativo y WASM) sustituye a `HttpTransport` en el cliente. Abre la conexión al hacer falta, reconecta con *backoff* exponencial, reenvía tras reconectar las llamadas pendientes que son seguras de repetir (lecturas y operaciones con clave de idempotencia), da por fallidas las demás que estaban en vuelo y restablece las suscripciones a eventos.
- **Eventos también en standalone**: `InProcTransport` recibe los eventos del backend embebido por la cola de eventos de Qt, de modo que el cliente no sabe qué transporte lleva las llamadas ni los eventos. El modo standalone sigue sin escuchar en la red.
- **Vista**: la lista de facturas se suscribe a los eventos y se refresca al recibirlos (también tras una reconexión, para recuperar los perdidos), y la ventana indica cuándo no hay conexión con el backend.
- **HTTP se mantiene en el servidor**: el API Connect por `POST` sigue siendo el API público para herramientas (`buf curl`) y otros clientes; los procedimientos de eventos no se sirven por HTTP (`unimplemented`).
- **Límites del servidor**: el WebSocket de cpp-httplib ocupa un hilo por conexión, así que el número de canales simultáneos se limita (`WS_MAX_CHANNELS`) para que las peticiones HTTP, incluidos los ficheros del cliente web, siempre tengan hilos; un cliente que no lee sus eventos se desconecta en lugar de bloquear al resto.
- **Evaluación de NNG y de un servidor de bucle de eventos** frente al WebSocket de cpp-httplib, registrada en el diseño; para la prueba de concepto se usa cpp-httplib, sin dependencias nuevas en el backend.
- **Medición**: el bench de latencia añade el canal real (sobre, sesión y router) y el cliente Qt.
- **BREAKING** (solo entre versiones de desarrollo, sin versión publicada): el cliente web y conectado deja de llamar por HTTP y necesita un servidor con el canal.

## Capabilities

### New Capabilities

- `canal-websocket`: canal persistente entre cliente y backend sobre WebSocket: sobre en Protobuf, autenticación en el primer mensaje, llamadas concurrentes correlacionadas, eventos por el mismo canal, límites del servidor, y reconexión con reenvío seguro y restablecimiento de suscripciones en el cliente.

### Modified Capabilities

- `api-rpc-protobuf`: el contrato añade procedimientos de eventos (`WatchInvoices`) independientes del transporte y claves de idempotencia en las operaciones que crean; el transporte en proceso entrega también los eventos; HTTP rechaza los procedimientos de eventos.
- `suite-de-pruebas`: la cobertura exigida añade el canal, los eventos, la idempotencia y la reconexión.

## Impact

- **Contrato** (`src/proto`): `request_id` en `CreateInvoiceRequest`, `WatchInvoices` con su petición y respuesta en `guita.v1`, y el sobre del canal en un paquete propio, `guita.channel.v1`. Todo son adiciones: `scripts/check-contract.sh` debe seguir pasando.
- **Backend**:
  - `core`: columna `request_id` única en `invoices` dentro de la migración inicial, `InvoiceStore` e `InvoiceService` con la clave de idempotencia y un aviso de factura creada, sin conocer el contrato;
  - `api`: procedimientos de *streaming* en `RpcRouter`, `WatchInvoices` en `InvoiceProcedures`, el transporte `WebSocketRpcTransport`, y `Backend` con suscripciones en proceso y límite de canales;
  - `server`: variable `WS_MAX_CHANNELS`.
- **Cliente**: `ApiTransport` con suscripciones y estado de conexión, `WebSocketTransport` nuevo, `HttpTransport` retirado, `InProcTransport` con eventos, `ApiConnection` (`available`), `InvoiceRepository` (`watch()`, clave de idempotencia), `Main.qml` y las traducciones.
- **Dependencias**: módulo Qt WebSockets (`qtwebsockets`) de Qt 6.12 en los kits de escritorio y WASM, instalado por `scripts/setup-qt.sh` (LGPLv3, como el resto de módulos de Qt que se usan). Cambiar el script invalida una vez la caché del CI. El backend no añade dependencias (cpp-httplib ya trae WebSocket).
- **Pruebas**: suites nuevas del canal y ampliación de `rpcrouter`, `rpcapi`, `invoiceservice`, `invoicestore`, `migrations` e `invoicerepository`; un proxy TCP de prueba para cortar conexiones a voluntad.
- **Documentación**: `docs/architecture.md` (§1–§7, §9 con el proxy inverso y WebSocket, §10.3, §11 y §12 con el límite de un hilo por conexión), AGENTS.md (transportes, regla de idempotencia), README y CHANGELOG.
- **Despliegue**: el proxy inverso debe dejar pasar el *upgrade* a WebSocket en `/api/channel` y no cortar conexiones inactivas antes del *ping* del servidor.
