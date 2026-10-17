# Design

## Context

Ver proposal.md (Why) y las specs `api-rpc-protobuf` y `suite-de-pruebas`. Lo que condiciona el diseño, comprobado con código generado de verdad (Qt 6.12, `protoc` 36.2):

- El backend no puede depender de Qt; el cliente vive en tipos Qt y QML; los dos se compilan en el mismo ejecutable en modo standalone.
- Qt Protobuf funciona en los dos kits (`Qt6Protobuf` y `Qt6ProtobufQuick` vienen también en `wasm_singlethread`) y necesita `protoc` en el host, al que `qtprotobufgen` se conecta como plugin.
- Los mensajes de Qt Protobuf son tipos de valor de QML (`QML_STRUCTURED_VALUE`), con soporte de `optional` (`hasCustomerTaxId()`), pero **los campos `int64` se generan con `SCRIPTABLE false`**: QML no puede leer ni escribir ni el identificador ni el importe. Además un campo `id` se renombra a `id_proto` para no chocar con el `id` de QML.
- Qt Protobuf y `protoc` generan, para el paquete `guita.v1`, clases con el mismo nombre (`guita::v1::Invoice`); `EXTRA_NAMESPACE` de `qt_add_protobuf()` mueve las de Qt a otro namespace.
- El serializador JSON de Qt escribe los `int64` como números y acepta también el texto que usa el mapeo canónico de proto3; el binario es idéntico en los dos generadores.
- cpp-httplib registra rutas por expresión regular; el backend ya separa `core` (reglas y SQL) de `api` (HTTP).

## Goals / Non-Goals

**Goals:**
- Un único `.proto` del que salen los tipos de los dos lados, en nativo y en WASM, en cada compilación.
- El mismo comportamiento visible para el usuario que hoy.
- Que añadir un método nuevo sea añadirlo al `.proto`, escribir su handler en el backend y su llamada en el repositorio del cliente.
- Que el despacho de procedimientos no dependa del transporte: HTTP para web y conectado, en proceso para el standalone, y un canal WebSocket posterior que reutilice los mismos handlers sin tocarlos.
- Un standalone sin red: ni puertos abiertos ni token, con la interfaz igual de fluida.

**Non-Goals:**
- gRPC, HTTP/2 y streaming; tampoco las llamadas Connect por GET ni el respeto de `Connect-Timeout-Ms`.
- Exponer los mensajes de Qt Protobuf directamente a QML: los `int64` no son accesibles; QML recibe valores adaptados por el repositorio (decisión 6).
- Qt GRPC como cliente: el cliente usa `QNetworkAccessManager` sobre HTTP/1.1, igual que hoy.
- Saltarse el contrato en el standalone llamando directamente a los servicios de `core`: el transporte en proceso sigue pasando mensajes del contrato por `RpcRouter`.
- Que `core` use los tipos del contrato.
- Un cliente JSON en la interfaz: el cliente solo usa binario; JSON es para depurar y para herramientas.
- El canal WebSocket (issue posterior al archivar este cambio): un sobre `{correlation_id, method, payload}` sobre una conexión persistente que reutilice `RpcRouter` (decisión 3), con eventos push del servidor, claves de idempotencia en las operaciones que crean o modifican, autenticación en el primer mensaje y reconexión en el cliente. Como inspiración, NNG (C, MIT, sucesor de nanomsg, de la línea de ZeroMQ): REQ/REP con identificador de petición y reenvío, PUB/SUB para eventos, contextos para peticiones concurrentes sobre un mismo socket, E/S asíncrona sobre un pool de hilos y transportes intercambiables (`inproc`, `ipc`, `tcp`, `ws`); hay que evaluar NNG o un servidor de bucle de eventos frente al WebSocket de cpp-httplib, que usa un hilo por conexión, y si el modo standalone puede llamar a `RpcRouter` en el propio proceso (al estilo `inproc`) sin pasar por la red.

## Decisions

### 1. Dos generadores, con namespaces separados

Decidido con el usuario (opción B, tras comprobar la limitación de los `int64` en QML). El backend usa el código de `protoc` (`guita::v1::...`, sin Qt) y el cliente el de `qt_add_protobuf()` con `EXTRA_NAMESPACE qtc` (`qtc::guita::v1::...`), de modo que el cliente standalone, que enlaza los dos, no tiene símbolos duplicados.
- *Ventajas*: el cliente trabaja con `QString`/`QList`/`optional` de Qt y el build de WASM no necesita libprotobuf ni abseil (Qt Protobuf ya viene en el kit).
- *Alternativa*: `protoc` en los dos lados (un único código generado, al estilo ASN.1), a cambio de compilar libprotobuf y abseil con emsdk y adaptar igualmente los tipos para QML.
- Una suite de interoperabilidad (decisión 10) garantiza que los dos generadores producen y leen los mismos bytes.

### 2. Contrato y su organización

- `src/proto/guita/v1/invoice_service.proto`: `Invoice` (`id`, `customer`, `optional customer_tax_id`, `total_cents`), `ListInvoicesRequest`/`Response`, `CreateInvoiceRequest`/`Response` e `InvoiceService` con `ListInvoices` y `CreateInvoice`. Identificadores e importes son `int64`. Los nombres siguen el estilo `STANDARD` de buf (petición y respuesta propias por método, directorio igual al paquete).
- `src/proto/buf.yaml` declara el módulo, el lint `STANDARD` y las reglas de compatibilidad `FILE`.
- `src/proto/CMakeLists.txt` define dos librerías: `contract` (código de `protoc`, enlaza `protobuf::libprotobuf`, solo en nativo) y `contract_qt` (`qt_add_protobuf`, enlaza `Qt6::Protobuf`, nativo y WASM). Sin la opción `QML`: QML no consume los mensajes directamente (decisión 6).
- *Alternativa*: un `.proto` por mensaje; para dos métodos no aporta nada y buf admite crecer hacia varios ficheros sin cambiar el paquete.

### 3. Despacho independiente del transporte; Connect sobre HTTP como primer transporte

**`RpcRouter`** (`backend/api`) conoce procedimientos, no transportes. Recibe una llamada `RpcCall { procedure, codec (binario o JSON), payload, authorization }` y devuelve un `RpcResult { code, payload | message }`, con los códigos de error de Connect, que no dependen de HTTP (decisión 4). Hace todo lo común a cualquier transporte:
- comprobar la autorización con el `Authorizer`;
- buscar el procedimiento en una tabla `nombre → handler`;
- decodificar la petición y codificar la respuesta con el códec indicado (binario, o el mapeo canónico de proto3 con campos desconocidos ignorados y los campos sin presencia impresos aunque valgan cero, para que el JSON sea legible);
- traducir las excepciones a códigos y registrar las inesperadas.

Los handlers se registran tipados (`add<Request, Response>("guita.v1.InvoiceService/CreateInvoice", handler)`), así que solo ven mensajes ya decodificados y no saben por dónde llegó la llamada. El router es síncrono y thread-safe: lo pueden llamar a la vez varios hilos de cualquier transporte.

**El primer transporte, `HttpRpcTransport`**, adapta el subconjunto unario de Connect sobre cpp-httplib a `RpcCall`:
- una ruta `POST /api/(guita\.v1\.\w+)/(\w+)`;
- el códec por `Content-Type` (`application/proto` o `application/json`), y la respuesta en el mismo; otro tipo → 415;
- otro método HTTP en una ruta de procedimiento → 405 con `Allow: POST`;
- el código del resultado al estado HTTP de Connect, y el cuerpo de error JSON;
- `Connect-Protocol-Version: 1` se acepta y no se exige; el cliente propio la envía.

El segundo transporte es la llamada en proceso del standalone (decisión 7), y un canal WebSocket posterior (Non-Goals) será otro adaptador que traduzca su sobre a `RpcCall`.
- *Alternativas*: despachar dentro de los handlers de cpp-httplib (ata la lógica a HTTP y obligaría a duplicarla para WebSocket); Twirp en lugar de Connect (equivalente, menos herramientas); un formato propio (sin herramientas como `buf curl`).

### 4. Errores de Connect sin glaze

Los códigos de error son los de Connect y forman parte de `RpcResult`, independientes del transporte; cada adaptador los traduce (el de HTTP, a la tabla de estados de Connect). En HTTP, los errores son `{"code", "message"}` con la tabla de Connect: `invalid_argument` 400 (validaciones `std::invalid_argument` y mensajes que no se decodifican), `unauthenticated` 401, `unimplemented` 404/415 y `internal` 500 (registrado en el log). El cuerpo se escribe con el impresor JSON de protobuf a partir de un `google.protobuf.Struct`, el tipo conocido que ya trae libprotobuf, sin añadir un `.proto` interno al contrato. Con eso todo el JSON del backend pasa por protobuf y glaze se retira del proyecto.
- *Alternativa*: mantener glaze para el JSON (más rápido), pero el JSON ya no está en el camino caliente (el cliente usa binario) y obligaría a duplicar en structs lo que define el `.proto`.

### 5. `core` no conoce el contrato

`backend/core` sigue con sus structs (`Invoice`, `NewInvoice`) y `backend/api` convierte entre ellos y los mensajes. La normalización del NIF vacío sigue en `InvoiceService`, y el NIF ausente del contrato corresponde al `std::optional` vacío del dominio.
- *Alternativa*: usar los mensajes generados en `core`: menos conversiones, pero ataría las reglas de negocio y el SQL al formato de transporte.

### 6. Cliente: mensajes de Qt Protobuf hasta el repositorio, valores tipados para QML

- `ApiConnection::call(procedure, body)` envía `POST <base>/<procedure>` con `application/proto` y `Connect-Protocol-Version: 1`; `errorMessage()` lee el `message` de los errores de Connect.
- `InvoiceRepository` construye `qtc::guita::v1::CreateInvoiceRequest` y decodifica las respuestas con `QProtobufSerializer`.
- QML recibe `InvoiceRow`, un tipo de valor propio (`Q_GADGET`, `QML_VALUE_TYPE(invoiceRow)`) con `customer`, `customerTaxId`, `hasCustomerTaxId` y `id`/`totalCents` como `double`, que representa enteros exactos hasta 2^53 (los importes no pasan de 10^12 céntimos). `create()` recibe argumentos tipados (`customer`, `customerTaxId`, `totalCents`) en lugar de un mapa, y la vista declara los tipos (`required property invoiceRow modelData`), así que `qmllint` comprueba los accesos.
- *Alternativas*: exponer los mensajes de Qt Protobuf a QML (inviable por los `int64`); seguir con `QVariantMap` (sin tipos para `qmllint`).

### 7. Transportes del cliente: HTTP y en proceso

`ApiConnection` delega en un `ApiTransport`, una interfaz C++ del cliente no visible para QML, con una única operación asíncrona: enviar `(procedimiento, bytes)` y entregar más tarde los bytes de la respuesta o el error.
- **`HttpTransport`** (web y conectado): `POST <base>/<procedimiento>` con `QNetworkAccessManager`, `application/proto`, `Connect-Protocol-Version: 1` y el token Bearer; lee los errores de Connect del cuerpo JSON.
- **`InProcTransport`** (solo nativo, con `APP_STANDALONE_SUPPORT`): construye un `RpcCall` y llama a `Backend::call()` en un hilo de `QThreadPool`, porque el router es síncrono y PostgreSQL puede tardar; el `RpcResult` vuelve al hilo de la interfaz por la cola de eventos (`QMetaObject::invokeMethod`), así que la interfaz no se bloquea. Respuestas y errores son los mismos que por HTTP porque salen del mismo `RpcRouter`.
- **`Backend`**: la parte HTTP de `BackendOptions` pasa a ser opcional. `server` la configura siempre; el standalone no la configura, así que no abre puertos ni genera token, y su `Authorizer` acepta las llamadas en proceso, que solo existen dentro del ejecutable. `Backend.h` expone `RpcCall`, `RpcResult` y `call()` con tipos estándar (`std::string` para los bytes, un enum para el códec), sin protobuf ni cpp-httplib: el pimpl se mantiene.
- **Cierre ordenado**: antes de destruir el `Backend`, `main.cpp` espera a que terminen las llamadas en proceso pendientes (`QThreadPool::waitForDone()`).
- **Principio revisado** (AGENTS.md y `docs/architecture.md`): "el cliente siempre habla con el backend a través del contrato RPC y nunca sabe qué transporte lo lleva", en lugar de "siempre habla HTTP/JSON".
- *Alternativas*: mantener HTTP por *loopback* en el standalone (más simple, pero con puerto abierto, token y la sobrecarga de HTTP y TCP); llamar directamente a los servicios de `core` (más rápido aún, pero dos caminos de código y sin contrato); un único `QThread` de trabajo en lugar del pool (serializaría las llamadas, cuando el router admite concurrencia).

### 8. Dependencias

- **protobuf 36.2** compilado con `scripts/build-deps.sh` en `~/software/protobuf-36.2`, como libpqxx: estático y PIC (se enlaza dentro del cliente), con abseil incluido como submódulo (`protobuf_ABSL_PROVIDER=module`), sin tests, y con el mismo estándar que el proyecto (C++23) para que abseil y el proyecto vean los mismos tipos `std`. Aporta también el `protoc` del host que necesitan `protobuf_generate()` y `qtprotobufgen`, también para el preset `wasm`.
  - *Alternativas*: el paquete de EL9 (3.14, de 2020, en CodeReady, con sudo); el `protoc` precompilado (no trae la librería C++).
- **Módulo `qtgrpc`** de Qt 6.12 (Qt Protobuf) en los dos kits, desde `scripts/setup-qt.sh`.
- **buf 1.74.0**, un binario de desarrollo, en `~/software/buf-1.74.0`, también desde `scripts/build-deps.sh`.
- Se retira glaze de `build-deps.sh`, de CMake y de la documentación.

### 9. Gobierno del contrato

- Una prueba de CTest (`contract_lint`, categoría `unit`) ejecuta `buf lint` y `buf format --diff --exit-code` sobre `src/proto`.
- `scripts/check-contract.sh` ejecuta `buf breaking` contra el contrato de la rama de desarrollo (`.git#branch=develop/vX.Y.Z,subdir=src/proto`); si la rama aún no tiene contrato, lo indica y termina sin error. La usará el CI (#4) en cada PR.
- Reglas en AGENTS.md: nunca reutilizar ni renumerar campos, `reserved` para los eliminados, añadir en vez de modificar, y un paquete nuevo (`guita.v2`) para cambios incompatibles una vez publicada una versión.

### 10. Pruebas

- `contract` (`unit`): interoperabilidad de los dos generadores en los dos sentidos, con NIF ausente y presente, importes 0 y máximos de `int64`, y listados.
- `rpcrouter` (`unit`): `RpcRouter` sin ningún transporte, con procedimientos de prueba: los dos códecs, autorización, procedimiento inexistente, mensaje mal formado, y excepciones de validación e inesperadas traducidas a sus códigos. Demuestra que el despacho no depende de HTTP.
- `rpcapi` (`api`) sustituye a `restapi`: los dos procedimientos en binario (decodificando con el código de `protoc`) y en JSON (analizado con `QJsonDocument`), y los errores de la spec.
- `invoicerepository` (`client`) pasa a `InvoiceRow` y a los argumentos tipados, y se ejecuta con los dos transportes (HTTP contra un `Backend` con HTTP y token, y en proceso contra un `Backend` sin HTTP), comprobando que los resultados coinciden y que el `Backend` sin HTTP no escucha en la red.
- `invoicestore`, `invoiceservice`, `migrations` y `database` no cambian.
- Además, una comprobación manual con `buf curl` en la integración.

### 11. Medición

Dos programas de medición, fuera de `ctest`, con resultados documentados en `docs/architecture.md`:
- `tests/bench/encoding_bench.cpp`: para listados de 1 000 y 10 000 facturas, el tamaño (JSON, JSON con gzip, binario) y el tiempo de codificar y decodificar en el cliente (Qt JSON a `QVariantList`, como hasta ahora, frente a `QProtobufSerializer`). Se ejecuta en nativo y compilado para WASM en Chrome sin pantalla.
- `tests/bench/latency_bench.cpp`: la latencia de ida y vuelta de una llamada pequeña en proceso (`Backend::call`), con `POST` y *keep-alive*, y con un frame de WebSocket que lleva los mismos bytes, estos dos sobre cpp-httplib en *loopback*, para cuantificar lo que gana el standalone y dimensionar el canal WebSocket posterior.

## Risks / Trade-offs

- [Dos generadores que podrían divergir] → La suite `contract` codifica con uno y decodifica con el otro; el formato binario de Protobuf es estable entre implementaciones.
- [abseil compilado con un estándar distinto al del proyecto cambia tipos internos] → Se compila protobuf y abseil con C++23, como el proyecto.
- [Compilar protobuf y abseil lleva varios minutos] → Una sola vez por máquina, en `~/software`; el CI lo cacheará (#4).
- [Identificadores por encima de 2^53 en QML] → No se alcanzan en una aplicación de facturación (`bigserial` crece de uno en uno); documentado junto a `InvoiceRow`.
- [El JSON de Qt escribe los `int64` como números y el canónico como texto] → El cliente no usa JSON; las pruebas comprueban que el backend acepta ambos.
- [Qt Protobuf aumenta el tamaño del `.wasm`] → Se mide en la decisión 11 y se documenta.
- [Un router común a varios transportes podría acabar con supuestos de HTTP] → La suite `rpcrouter` lo prueba sin ningún transporte, y los estados HTTP solo existen en `HttpRpcTransport`.
- [Llamadas en proceso en vuelo al cerrar la aplicación] → `main.cpp` espera al pool antes de destruir el `Backend`.
- [Los dos transportes del cliente podrían divergir] → `invoicerepository` ejecuta los mismos casos con ambos.
- [Cambio incompatible del API] → No hay versiones publicadas ni consumidores externos; cliente y servidor salen de la misma versión.

## Migration Plan

Cliente y servidor cambian a la vez en la misma versión (0.1.0, no publicada), sin datos que migrar: el esquema de la base de datos no cambia. Revertir es revertir el merge del cambio.
