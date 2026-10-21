# Design

## Context

Ver proposal.md (Why) y las specs `canal-websocket`, `api-rpc-protobuf` y `suite-de-pruebas`. Lo que condiciona el diseño, comprobado en el código actual y en las librerías instaladas:

- `RpcRouter` solo conoce llamadas unarias: `call(RpcCall) -> RpcResult`, síncrono y thread-safe. Los transportes actuales son `HttpRpcTransport` (servidor) y `Backend::call()` (en proceso); en el cliente, `HttpTransport` e `InProcTransport` detrás de `ApiTransport`, que tiene una sola operación asíncrona y no es un `QObject`.
- `Backend.h` no puede incluir cpp-httplib, libpqxx ni protobuf (pimpl), y `core` no conoce el contrato.
- cpp-httplib 0.61 ofrece `Server::WebSocket(pattern, handler)`: el handler se ejecuta en un hilo del pool del servidor durante toda la vida de la conexión (`read()` bloqueante), y cada `ws::WebSocket` arranca además su propio hilo de *ping* (cada 30 s). `send()` es thread-safe (mutex de escritura) y `set_read_timeout()` permite acotar la espera. El pool del servidor tiene por defecto `max(8, núcleos − 1)` hilos fijos y hasta 4 veces eso en total; `new_task_queue` permite cambiarlo. Con canales abiertos ocupando esos hilos, las peticiones HTTP (también los ficheros del cliente web) se quedarían en cola.
- `Server::stop()` espera a los hilos del pool: un canal bloqueado en `read()` sin límite retrasaría el cierre del servidor hasta el *timeout* de reserva de lectura (300 s).
- El servidor ya activa `TCP_NODELAY`, que se aplica también a las conexiones WebSocket aceptadas. Sin él, cada ida y vuelta de un frame pequeño tardaba unos 82 ms (§4.5 de `docs/architecture.md`).
- Qt 6.12 trae el módulo Qt WebSockets (`qtwebsockets`) para los kits de escritorio y `wasm_singlethread`; en WASM, `QWebSocket` usa el WebSocket del navegador, que no permite cabeceras propias en el *handshake*.
- El contrato se genera para el runtime lite (sin descriptores): el router comprueba los tipos de cada método por el nombre (`<Method>Request`/`<Method>Response`), y `buf lint` (`STANDARD`) exige esos nombres también en los métodos de *streaming*.
- El esquema tiene una sola migración, aún no publicada en ninguna versión; `invoices` no tiene ninguna clave natural que sirva para detectar repeticiones.

## Goals / Non-Goals

**Goals:**
- Un único canal de larga vida por cliente en web y conectado, por el que viajan llamadas y eventos.
- Que `RpcRouter`, los procedimientos y `core` sigan sin saber por dónde llega una llamada o sale un evento: un evento se produce igual si la factura se crea por HTTP, por el canal o en proceso, y llega igual por el canal o en proceso.
- Reenviar tras una reconexión sin aplicar nada dos veces, con una regla simple que se pueda seguir al añadir métodos.
- Que el canal no pueda dejar sin servicio al HTTP del mismo servidor ni a los demás canales.
- Sin dependencias nuevas en el backend.

**Non-Goals:**
- Eventos entre procesos: el servidor remoto es único, sin escalado horizontal, así que los eventos se producen y se reparten dentro de su proceso, y cada backend standalone avisa solo de lo que crea su propio proceso. Es una decisión del proyecto, no trabajo pendiente: no se prevé comunicación entre servidores.
- Recuperar los eventos perdidos durante un corte (números de secuencia, reenvío desde el servidor): el aviso de inicio hace que la vista recargue, que basta para listas pequeñas.
- *Server streaming* de Connect por HTTP, y *streaming* del cliente o bidireccional. Decidido sin issue de seguimiento: ningún cliente del proyecto lo usaría, y el canal ya lleva los eventos.
- Una tabla de idempotencia genérica para operaciones que modifican: hoy solo existe `CreateInvoice`, y su clave vive en la propia fila (decisión 4).
- Un latido propio del protocolo: se confía en el *ping* de cpp-httplib (30 s) y en el plazo de las llamadas del cliente (30 s).
- Compresión de frames (`permessage-deflate`) y TLS en el backend (lo termina el proxy inverso, §9 de `docs/architecture.md`).
- Autenticación de usuarios (§11): el saludo lleva la misma autorización que hoy la cabecera, y cambiará con ella.
- Un servidor de bucle de eventos o NNG (decisión 9).

## Decisions

### 1. El cliente usa el canal para todo en web y conectado; HTTP se queda en el servidor

En web y conectado, el cliente hace llamadas y suscripciones por el canal (`WebSocketTransport`) y se retira `HttpTransport`. El servidor sigue sirviendo el API Connect por `POST`, que es el API público para herramientas (`buf curl`, `protoc --encode` + `curl`) y para otros clientes, y donde se prueban los errores HTTP.
- Una sola conexión, autenticada una vez, con un orden único entre respuestas y eventos.
- El cliente sigue teniendo dos transportes, uno por modo, como hoy.
- *Alternativas*:
  - HTTP para las llamadas y el canal solo para eventos: dos conexiones y dos autenticaciones por cliente, sin orden entre ellas, y la idempotencia y el reenvío dejarían de tener sentido, contra lo que pide el issue;
  - mantener `HttpTransport` como opción del cliente: un camino sin uso en la aplicación que habría que probar igual, y sin eventos.

### 2. Eventos como procedimientos de *server streaming* del contrato

`InvoiceService` gana `rpc WatchInvoices(WatchInvoicesRequest) returns (stream WatchInvoicesResponse)`. La respuesta es un `oneof event { WatchStarted started = 1; Invoice created = 2; }`:
- `started` es siempre el primer mensaje, enviado después de registrar la suscripción: lo que se confirme a partir de ahí llega como evento. La vista recarga al recibirlo, lo que cierra a la vez la carrera entre listar y suscribirse y el hueco de una reconexión. Una factura confirmada justo antes puede llegar como evento y estar ya en la lista: recargar es idempotente.
- Nuevos tipos de evento (factura modificada, anulada) son nuevos casos del `oneof`, una adición compatible con `WIRE_JSON`.
- Petición y respuesta propias, como exigen `buf lint` y el router; un método nuevo de eventos es otro procedimiento de *streaming*, no un mecanismo aparte.
- *Alternativas*:
  - un mensaje `Event` global con un `oneof` de todos los eventos, fuera de los servicios: crece sin límite y no encaja en las reglas de evolución por método;
  - mensajes de eventos solo en el sobre del canal: el modo standalone no los vería sin que el cliente supiera qué transporte usa.

### 3. Router con procedimientos de *streaming* y temas de eventos en `api`

- **`RpcRouter::addStream<Request, Response>(procedure, handler)`**: el handler recibe la petición decodificada y una función `emit(const Response &)`, y devuelve una suscripción. Se registra con la misma comprobación de nombres que `add()`.
- **`RpcRouter::subscribe(RpcCall, onMessage) -> RpcSubscription`**:
  - comprueba la autorización, busca el procedimiento y decodifica la petición como `call()`;
  - devuelve un `RpcResult` con el error, si lo hay, y un manejador que se puede mover pero no copiar, cuya destrucción termina la suscripción;
  - `onMessage` recibe cada mensaje ya codificado (`std::string`), así que el tipo no sale de `api`.
- **Cruce de llamadas**: `call()` a un procedimiento de *streaming*, o `subscribe()` a uno unario, devuelve `unimplemented` con un mensaje que lo explica. Así responde también `HttpRpcTransport`, que no cambia (404, código `unimplemented`).
- **`EventTopic<Message>`** (`api`): lista de suscriptores protegida por un mutex. `publish()` llama a cada suscriptor con el mensaje; la destrucción del manejador de un suscriptor toma el mismo mutex, de modo que, al volver, ese suscriptor no recibirá ninguna llamada más. Los suscriptores no deben bloquear: solo encolan (decisiones 6 y 8).
- **Origen de los eventos**:
  - `InvoiceService` recibe en su constructor un aviso `std::function<void(const Invoice &)>` de C++ plano, sin contrato. Lo llama cuando la transacción de una creación nueva se ha confirmado, nunca para una repetida ni para una que falla;
  - `InvoiceProcedures` lo conecta a un `EventTopic<WatchInvoicesResponse>` y registra `WatchInvoices`, que se suscribe al tema y emite `started`.
- **`Backend::subscribe(RpcCall, std::function<void(std::string)>)`**, con tipos estándar en `Backend.h` (`RpcSubscription` va en `RpcCall.h`), para el modo standalone.
- *Alternativa*: publicar desde el handler de `CreateInvoice` en `api`. No distinguiría una creación de una repetición idempotente sin que el servicio lo dijera; el aviso desde `core` lo sabe, y además cubre cualquier camino que cree facturas.

### 4. Idempotencia con `request_id` en el mensaje y la clave en la fila

- **Contrato**: `CreateInvoiceRequest` gana `string request_id = 4;`, un UUID elegido por quien llama, al estilo de AIP-155. Vacío significa sin clave.
  - La clave es parte de la operación, no del transporte: sirve igual por HTTP, en proceso y por el canal, y la puede usar cualquier cliente que reintente.
- **Esquema**: la columna `request_id uuid UNIQUE` se añade al `CREATE TABLE invoices` de la migración 1 (`NULL` para las facturas sin clave, que no chocan entre sí). La regla de no editar migraciones se refiere a las publicadas, y 0.1.0 aún no lo está; una migración nueva solo arrastraría un `ALTER TABLE` sin valor.
- **Store**: `INSERT ... ON CONFLICT (request_id) DO NOTHING RETURNING ...`. Si no devuelve fila, otra transacción ya usó la clave: el store lee la factura de esa clave en la misma transacción.
  - Con dos peticiones simultáneas, PostgreSQL hace esperar a la segunda a que la primera confirme; en `READ COMMITTED`, la lectura siguiente ya la ve.
- **Servicio**:
  - valida el formato de la clave (`invalid_argument` si no es un UUID);
  - si la factura existente no coincide con la petición (cliente, NIF normalizado, importe), lanza `invalid_argument` ("request_id was already used for a different invoice");
  - si coincide, la devuelve sin avisar de ninguna creación.
- **La clave no caduca**: identifica el origen de la factura y ocupa 16 bytes por fila.
- **Regla para el contrato** (AGENTS.md): toda operación que crea o modifica lleva `request_id`, y el backend la hace idempotente dentro de la misma transacción que la operación.
- *Alternativas*:
  - la clave en el sobre del canal con una tabla genérica de respuestas en el router: el router no comparte la transacción del servicio, así que una caída entre confirmar la factura y guardar la clave permitiría el duplicado; además solo protegería el canal;
  - una tabla `idempotency_keys` aparte: es necesaria para modificaciones repetidas de una misma fila, pero hoy no hay ninguna (Non-Goals).

### 5. Sobre del canal en `guita.channel.v1`

`src/proto/guita/channel/v1/channel.proto`, paquete `guita.channel.v1`, también lite. Es el marco del transporte, no el API, y lleva el nombre completo del procedimiento, así que un futuro `guita.v2` viajaría por el mismo canal. Un mensaje binario de WebSocket es un frame:

```proto
message ClientFrame {
  oneof frame {
    Hello hello = 1;      // primero y una sola vez
    Call call = 2;
    Cancel cancel = 3;    // termina una suscripción
  }
}
message Hello { string authorization = 1; }   // "Bearer <token>", como la cabecera
message Call { uint64 correlation_id = 1; string procedure = 2; bytes payload = 3; }
message Cancel { uint64 correlation_id = 1; }

message ServerFrame {
  oneof frame {
    Welcome welcome = 1;
    Reply reply = 2;            // respuesta unaria, o fin de una suscripción con error
    StreamMessage message = 3;  // un mensaje de una suscripción
    Goodbye goodbye = 4;        // el servidor va a cerrar, y por qué
  }
}
message Reply { uint64 correlation_id = 1; oneof result { bytes payload = 2; CallError error = 3; } }
message StreamMessage { uint64 correlation_id = 1; bytes payload = 2; }
message CallError { string code = 1; string message = 2; }  // nombres de código de Connect
message Welcome {}
message Goodbye { CallError error = 1; }
```

- **Códigos de error**: son los nombres de Connect (`connectCodeName()`), como en los errores JSON de HTTP: el cliente traduce un único modelo de errores.
- **Correlación**: la elige el cliente, es única entre sus llamadas en vuelo y no se reutiliza en la misma conexión. Una suscripción conserva su identificador hasta cancelarse.
- **Ideas de NNG adoptadas**: el identificador de petición y el reenvío de REQ/REP, los contextos (varias llamadas en vuelo por conexión) y PUB/SUB, aquí como procedimientos de *streaming*.
- *Alternativas*:
  - JSON o texto en el sobre: un segundo formato, cuando todo lo demás es binario;
  - subprotocolos de WebSocket para autenticar: el token acabaría en una cabecera que registran los proxies;
  - el sobre en `guita.v1`: mezclaría el marco del transporte con el API versionado.

### 6. Servidor: `WebSocketRpcTransport` sobre cpp-httplib

La clase `WebSocketRpcTransport` (`backend/api/WebSocketRpcTransport.{h,cpp}`), creada por `Backend` con el router, el `Authorizer` y unas `ChannelOptions`, registra la ruta con `addRoute(httplib::Server &)` junto a las rutas HTTP y lleva el registro de sesiones para `closeAll()`. Cada conexión es una sesión:

- **Lectura con plazo corto**: cerrar un WebSocket de cpp-httplib desde otro hilo no despierta un `read()` bloqueado (comprobado en su código), así que el lector lee con un plazo de 250 ms y vuelve a mirar si la sesión se cierra. Con ese plazo deja de actuar el de reserva de cpp-httplib (300 s), y es el *ping* el que descarta clientes muertos: el servidor cierra un canal tras dos *pings* sin respuesta (`set_websocket_max_missed_pongs(2)`).
- **Saludo**: hasta 10 s para el `Hello`. Con un saludo aceptado (el `Authorizer` de siempre) envía `Welcome` y guarda la autorización, que acompaña a cada `RpcCall`; el router la vuelve a comprobar, así que no cambia. En cualquier otro caso envía `Goodbye` con `unauthenticated` y cierra con el código 1008.
- **Lectura**:
  - el hilo del pool de cpp-httplib lee frames;
  - un `Call` unario se encarga a un pool de trabajo compartido por todos los canales (`httplib::ThreadPool`, tantos hilos como conexiones del pool de la base de datos);
  - un `Call` de *streaming* abre la suscripción con `RpcRouter::subscribe()` y la guarda por su identificador;
  - un `Cancel` la destruye.
  - Como mucho 64 llamadas unarias en vuelo por canal: al llegar al límite, el hilo deja de leer hasta que termine alguna, y TCP frena al cliente.
- **Escritura**:
  - respuestas y eventos se encolan en una cola de salida por sesión, que vacía un hilo escritor propio de la sesión;
  - quien produce (un hilo del pool de trabajo, o el hilo que creó una factura y publica el evento) nunca espera al socket;
  - si la cola pasa de 1 MiB, la sesión se cierra (código 1008 y `Goodbye` con `unavailable` si cabe).
- **Errores de protocolo**: un frame de texto, un frame ilegible, un segundo `Hello` o un identificador repetido en vuelo. Los tres primeros cierran con 1002 (o 1003 el texto); el identificador repetido responde `invalid_argument` a esa llamada.
- **Límite de canales** (`HttpOptions::maxChannels`, `WS_MAX_CHANNELS` en `server`, por defecto 64):
  - el pool de cpp-httplib se configura con `new_task_queue` para admitir los hilos por defecto más un hilo por canal, de modo que los canales nunca le quitan hilos a HTTP;
  - una conexión por encima del límite se cierra nada más aceptarse con el código 1013 (*Try Again Later*), que el cliente trata como un corte más.
- **Cierre**: las sesiones se registran en el transporte. `Backend::stop()` las cierra todas (1001, *Going Away*) antes de `Server::stop()`, de modo que ningún `read()` retrasa la parada. Al cerrarse, una sesión destruye sus suscripciones, espera a sus llamadas en vuelo y termina su hilo escritor.
- **Frames de más de 16 MiB**: los rechaza cpp-httplib (`CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH`, igual que el límite de los cuerpos HTTP).
- **Hilos por canal**: el lector (del pool), el *ping* de cpp-httplib y el escritor. Se documenta en §11 y §12 de `docs/architecture.md`.

### 7. Cliente: `ApiTransport` con suscripciones, reenvío explícito y disponibilidad

`ApiTransport` cambia así:
- **`call(procedure, request, context, done, repeat)`**: `repeat` (`Repeat::Never` por defecto, `Repeat::Safe`) dice si repetir la llamada es seguro. Es seguro por defecto: lo que no se marca no se reenvía.
- **`subscribe(procedure, request, context, onMessage, onEnd)`**: entrega cada mensaje codificado en el hilo de la interfaz mientras `context` viva. La suscripción termina al destruirse `context`, o con `onEnd` si el backend la termina con un error.
- **Disponibilidad**: el transporte avisa de si el backend está disponible.

`ApiConnection` expone lo mismo con tipos del contrato:
- `call<Response>(..., Repeat)` y `subscribe<Response>(procedure, request, context, onMessage, onError)`;
- `Q_PROPERTY(bool available ... NOTIFY availableChanged)` para la vista. Siempre `true` en proceso; en el canal, `false` desde que se pierde o falla la conexión hasta el siguiente `Welcome`.

`InvoiceRepository`:
- `list()` es `Repeat::Safe`;
- `create()` genera un `request_id` nuevo (`QUuid`) en cada llamada y es `Repeat::Safe`;
- `watch()` se suscribe a `WatchInvoices` (una vez por repositorio) y emite `watching()` con cada `started` e `invoiceCreated(InvoiceRow)` con cada factura.

`Main.qml` llama a `watch()` al cargarse, recarga la lista con `watching` y con `invoiceCreated`, y muestra un aviso mientras `ApiConnection.available` es `false`.

### 8. `WebSocketTransport` (nativo y WASM)

Sobre `QWebSocket`, en el hilo de la interfaz y sin esperas bloqueantes, como exige WASM:

- **URL**: `defaultChannelUrl()` en `ApiConfig.h`. En WASM es el origen de la página con `ws`/`wss` según su protocolo, más `/api/channel`; en nativo, `API_URL` con el esquema cambiado (`http` → `ws`, `https` → `wss`) más `channel`.
- **Conexión perezosa**: se abre con la primera llamada o suscripción. Las llamadas esperan en una cola hasta el `Welcome`.
- **Estado por llamada**: pendiente (sin enviar), en vuelo, o suscripción activa. Las suscripciones se guardan con su petición para restablecerlas.
- **Al perder la conexión**:
  - las llamadas en vuelo con `Repeat::Safe` vuelven a la cola y las demás fallan con "Connection to the backend lost";
  - si queda algo pendiente o alguna suscripción, se reconecta con espera exponencial: 0,5 s, que se dobla hasta 30 s, con ±20 % aleatorio, y vuelve a 0,5 s tras un `Welcome`;
  - tras reconectar se envían la cola y las suscripciones, cada una con un `Call` nuevo cuyo primer mensaje volverá a ser `started`.
- **Plazo**: toda llamada sin respuesta 30 s después de hacerse falla con "Backend unavailable". Una suscripción no tiene plazo.
- **Saludo rechazado** (`Goodbye` con `unauthenticated`): fallan las llamadas pendientes con el mensaje del backend, terminan las suscripciones con ese error y no se reconecta hasta la siguiente llamada o suscripción.
- **Cancelación**: al destruirse el `context` de una suscripción se envía `Cancel`. Al destruirse el de una llamada, su respuesta se descarta al llegar.
- **Opciones**: plazos y esperas se pueden cambiar (`WebSocketTransport::Options`) para que las pruebas no esperen 30 s.
- **`TCP_NODELAY` en el cliente**: `QWebSocket` no da acceso a su socket. Se comprueba con el bench (decisión 11) que una ida y vuelta no sufre el retraso de Nagle; si lo sufre, se resuelve en el propio transporte antes de dar por terminada la medición.

`InProcTransport` implementa `subscribe()` con `Backend::subscribe()`:
- el aviso del backend llega en el hilo que creó la factura y se pasa al de la interfaz por la cola de eventos, como los resultados de las llamadas, con un identificador de suscripción que se comprueba en el hilo de la interfaz;
- su destructor destruye las suscripciones antes de esperar al pool.

`repeat` no le afecta: en proceso no hay cortes.

### 9. Evaluación: cpp-httplib frente a NNG y a un servidor de bucle de eventos

- **NNG** (C, MIT):
  - sus protocolos REQ/REP y PUB/SUB y su transporte `ws` sirven de modelo (decisión 5), pero no de implementación;
  - en el navegador habría que implementar a mano el protocolo SP sobre WebSocket (las cabeceras de NNG con el identificador de petición), es decir, un sobre propio de todos modos;
  - el cliente Qt necesitaría NNG en nativo y esa implementación a mano en WASM: dos clientes distintos;
  - en el backend sería una dependencia nueva de producción;
  - sus *sockets* PUB/SUB no se integran con `RpcRouter` ni con el modo en proceso.
- **Servidor de bucle de eventos** (Boost.Beast/Asio, uWebSockets, libwebsockets):
  - atiende miles de conexiones con pocos hilos, que cpp-httplib no puede;
  - pero es una dependencia nueva de producción y un segundo servidor HTTP o un cambio del actual;
  - para una aplicación de facturación (decenas o cientos de usuarios por instancia), un hilo por conexión con un límite explícito basta.
- **Decisión**: cpp-httplib, ya presente. El canal queda detrás de `RpcRouter` y en un solo fichero (`WebSocketRpcTransport`), así que cambiar de servidor más adelante no toca procedimientos, contrato ni cliente. Queda como trabajo pendiente en §11.

### 10. Dependencia de cliente: Qt WebSockets

- `scripts/setup-qt.sh` añade `qtwebsockets` a los módulos de los dos kits. La comprobación de cada kit pasa a mirar también `Qt6WebSockets`, para que una instalación existente reciba el módulo al volver a ejecutar el script.
- `src/client/CMakeLists.txt` y la suite del cliente enlazan `Qt6::WebSockets`.
- Cambiar el script cambia la clave de caché del CI, que reinstala una vez; el job no necesita paquetes de `apt` nuevos, y se comprueba en el PR.
- Licencia: LGPLv3/GPL/comercial, como los demás módulos que se usan (§12).

### 11. Pruebas y medición

- **`contract`** (`unit`): bytes de referencia de `ClientFrame` y `ServerFrame` (saludo, llamada, respuesta con error, mensaje de *stream*), de `CreateInvoiceRequest` con `request_id` y de `WatchInvoicesResponse` con cada caso del `oneof`.
- **`invoiceservice`** (`unit`): formato de `request_id`.
- **`migrations`** e **`invoicestore`** (`db`): la columna y su unicidad, y `ON CONFLICT` con la lectura de la factura existente.
- **`idempotency`** (`db`, nueva): el servicio con la misma clave en serie, en paralelo desde varios hilos (una sola fila, todas las respuestas iguales) y con otros datos, y que el aviso de creación solo se produce una vez.
- **`rpcrouter`** (`unit`): procedimientos de *streaming* sin transporte. Mensajes en orden, cancelación al destruir el manejador, autorización al suscribirse, cruces de `call()` y `subscribe()`, y un `EventTopic` con publicaciones concurrentes y bajas de suscriptores.
- **`rpcapi`** (`api`): `WatchInvoices` por `POST` → 404 `unimplemented`; `request_id` por HTTP; una factura creada por HTTP llega a un suscriptor en proceso del mismo `Backend`.
- **`channel`** (`api`, nueva): el servidor con cpp-httplib `ws::WebSocketClient` y los frames del contrato, sobre un `Backend` con base de datos propia y un `RpcRouter` con procedimientos de prueba lentos y rápidos. Cubre:
  - saludo, sus rechazos y el plazo;
  - correlación y concurrencia, y errores iguales a HTTP;
  - suscripción, evento de una factura creada por otro canal y cancelación;
  - frames ilegibles e identificadores repetidos;
  - límite de canales con HTTP aún atendido;
  - cliente que no lee, cerrado sin bloquear a los demás;
  - parada del `Backend` con canales abiertos, sin esperas.
- **`invoicerepository`** (`client`):
  - pasa a `WebSocketTransport` e `InProcTransport`, con los mismos casos que hoy;
  - eventos de una factura creada por otra conexión;
  - `available`, y token incorrecto sin reconexiones.
- **`reconnection`** (`client`, nueva): un proxy TCP de prueba (`tests/support/CuttingProxy`, con `QTcpServer`) entre el cliente y el servidor. Puede retener lo que va del servidor al cliente y cortar las dos conexiones a voluntad, lo que permite cortar exactamente después de que el servidor confirme una creación y antes de que el cliente reciba la respuesta. Casos:
  - creación con clave reenviada, con una sola fila y la misma factura;
  - llamada `Repeat::Never` en vuelo fallida;
  - suscripción restablecida con `started` y factura creada durante el corte visible al recargar;
  - espera exponencial acotada;
  - plazo con el servidor inalcanzable.
- **Bench de latencia** (`tests/bench/latency_bench.cpp`), dos filas nuevas:
  - el canal real (`WebSocketRpcTransport` + `RpcRouter` con el procedimiento sin base de datos) con `ws::WebSocketClient` y el sobre;
  - el mismo con `WebSocketTransport` de Qt, que comprueba también `TCP_NODELAY` en el cliente.

  Se documenta en §4.5 junto con el tamaño de `client.wasm` antes y después (Qt WebSockets entra y `QNetworkAccessManager` deja de usarse).

## Risks / Trade-offs

- [Un hilo de cpp-httplib, uno de *ping* y uno escritor por canal] → Límite explícito de canales (decisión 6), pool de HTTP dimensionado aparte, y documentado en §11 y §12; cambiar a un servidor de bucle de eventos solo toca `WebSocketRpcTransport`.
- [Un evento publicado desde el hilo que creó la factura podría bloquearlo] → Los suscriptores solo encolan: la cola de salida de la sesión en el canal, la cola de eventos de Qt en proceso; la cola del canal está acotada y cierra al cliente lento.
- [Destruir una suscripción mientras se publica] → `EventTopic` publica y da de baja bajo el mismo mutex; ningún suscriptor puede darse de baja desde su propio aviso (está prohibido y documentado).
- [Reenviar una operación no idempotente aplicaría dos cambios] → El reenvío es opcional y explícito por llamada (`Repeat::Safe`) y solo lo marcan las lecturas y las operaciones con `request_id`; la regla del contrato está en AGENTS.md y la prueba `reconnection` cubre los dos caminos.
- [Eventos perdidos durante un corte] → Entrega como mucho una vez, documentada; el aviso de inicio tras reconectar hace que la vista recargue. Para listas grandes hará falta un *cursor* o secuencia (Non-Goals).
- [Eventos solo dentro de un proceso] → Es el despliegue decidido: un único servidor, sin escalado, que §9 de `docs/architecture.md` deja escrito.
- [`QWebSocket` sin `TCP_NODELAY` añadiría ~40 ms por llamada] → Se mide en el bench con el cliente Qt antes de cerrar el grupo; el servidor ya lo activa.
- [El proxy inverso podría cortar canales inactivos] → El *ping* del servidor cada 30 s mantiene el tráfico; §9 documenta `timeout tunnel` en HAProxy.
- [Cerrar el servidor con canales abiertos podría colgarse] → `Backend::stop()` cierra las sesiones antes de parar el servidor; una prueba lo cubre con un plazo.
- [La clave reutilizada con otros datos se detecta comparando campos] → Basta para `CreateInvoice`; si una operación futura no se puede comparar, se guardará un *hash* de la petición con la clave.
- [Cambiar el script de Qt invalida la caché del CI] → Una sola reinstalación, prevista en #4; el PR la valida.
- [Retirar `HttpTransport` del cliente] → El API HTTP sigue en el servidor y su suite (`rpcapi`); volver a un cliente HTTP sería otro `ApiTransport`.

## Migration Plan

- Sin versión publicada ni datos que conservar: cliente y servidor cambian a la vez en 0.1.0 y no hay plan de migración de datos. Una base de datos de desarrollo creada antes de este cambio ya tiene aplicada la migración 1 sin la columna, así que el backend no arranca con ella (las sentencias preparadas fallan al arrancar): se borra (`DROP DATABASE guita`) y se vuelve a crear con el `CREATE DATABASE` de `scripts/create-db.sql`. Las de las pruebas se crean en cada ejecución.
- El despliegue de §9 necesita que el proxy deje pasar el *upgrade* a WebSocket en `/api/channel` (HAProxy lo hace en modo HTTP) con `timeout tunnel` mayor que el intervalo de *ping*.

## Open Questions

- Valores por defecto de los límites (64 canales, 64 llamadas en vuelo por canal, 1 MiB de cola, plazos de 10 s y 30 s): se fijan con los del diseño y se ajustan, si hace falta, con las mediciones del bench sin cambiar las specs.
