# Tasks

## 1. Contrato

- [x] 1.1 Añadir a `src/proto/guita/v1/invoice_service.proto` el `request_id` de `CreateInvoiceRequest` (campo 4) y `WatchInvoices` con `WatchInvoicesRequest`, `WatchInvoicesResponse` (`oneof event { WatchStarted started; Invoice created; }`) y `WatchStarted`, con sus comentarios; verificar que `buf lint`, `buf format --diff --exit-code` y `scripts/check-contract.sh` pasan
- [x] 1.2 Crear `src/proto/guita/channel/v1/channel.proto` (paquete `guita.channel.v1`, lite) con los frames de la decisión 5 del diseño y añadirlo a `GUITA_PROTO_FILES`; verificar que los presets `native` y `wasm` configuran y compilan, y que `contract_lint` y `contract_format` pasan
- [x] 1.3 Ampliar `tests/support/ProtoSamples.h` y la suite `contract` con los bytes de referencia, calculados a mano, de `CreateInvoiceRequest` con `request_id`, de `WatchInvoicesResponse` con cada caso del `oneof` y de `ClientFrame`/`ServerFrame` (saludo, llamada, cancelación, respuesta con mensaje y con error, mensaje de *stream*, despedida); verificar con `ctest --preset native -R contract` y con `bench_encoding` en WASM, que comprueba las mismas muestras
- [x] 1.4 Documentar el contrato nuevo en `docs/architecture.md` (§3: dos ficheros y dos paquetes; §4.3: tabla de procedimientos con `WatchInvoices` y `request_id`); verificar que los ejemplos de `buf curl` y `protoc --encode`/`--decode` de §4.3 siguen funcionando tal cual

## 2. Idempotencia de las creaciones

- [x] 2.1 Añadir la columna `request_id uuid UNIQUE` al `CREATE TABLE invoices` de la migración 1 y ampliar la suite `migrations` (columna, tipo, unicidad, segunda ejecución sin cambios); verificar con `ctest --preset native -R migrations`
- [x] 2.2 `InvoiceStore`: inserción con `ON CONFLICT (request_id) DO NOTHING RETURNING` y lectura de la factura de una clave, como sentencias preparadas registradas en `prepare()`; ampliar `invoicestore` (clave nueva, clave existente, sin clave); verificar con `ctest --preset native -L db`
- [x] 2.3 `InvoiceService::create()` con clave: validación del formato UUID, devolución de la factura existente si coincide y `invalid_argument` si no; ampliar `invoiceservice` (formatos válidos e inválidos, sin base de datos) y crear la suite `idempotency` (`db`: misma clave en serie, desde varios hilos a la vez y con otros datos, una sola fila); verificar con `ctest --preset native -L unit -L db`
- [x] 2.4 `InvoiceProcedures` pasa `request_id` al servicio; ampliar `rpcapi` con la creación repetida por HTTP (misma factura, una sola fila) y la clave no válida (400 `invalid_argument`); verificar con `ctest --preset native -L api`
- [x] 2.5 Documentar la idempotencia: `docs/architecture.md` (§4.1, §4.2 y §4.3), la regla de `request_id` en AGENTS.md (Contrato y Backend), la tabla de suites (§10.3) y el CHANGELOG (`Added`); verificar que la creación repetida documentada con `protoc --encode` + `curl` devuelve la misma factura

## 3. Eventos independientes del transporte

- [ ] 3.1 Crear `EventTopic<Message>` en `backend/api` y añadir a `RpcRouter` `addStream<Request, Response>()` y `subscribe(RpcCall, onMessage) -> RpcSubscription` (`RpcSubscription` con tipos estándar en `RpcCall.h`), con `unimplemented` en los cruces entre `call()` y `subscribe()`; ampliar `rpcrouter` (mensajes en orden, cancelación al destruir el manejador, autorización, cruces, publicaciones y bajas concurrentes en `EventTopic`); verificar con `ctest --preset native -L unit`
- [ ] 3.2 `InvoiceService` con el aviso de creación tras confirmar la transacción, solo para facturas nuevas; `InvoiceProcedures` con el tema de facturas y `WatchInvoices` (suscripción y después `started`); ampliar `idempotency` para comprobar que la repetición no avisa y `invoiceservice` para que una validación fallida tampoco; verificar con `ctest --preset native -L unit -L db`
- [ ] 3.3 `Backend::subscribe()` en `Backend.h`, sin incluir cpp-httplib, libpqxx ni protobuf; ampliar `rpcapi`: `WatchInvoices` por `POST` responde 404 `unimplemented` con su mensaje, y una factura creada por HTTP llega a un suscriptor en proceso del mismo `Backend` con `started` primero; verificar con `ctest --preset native -L api`
- [ ] 3.4 Documentar los procedimientos de *streaming*, `EventTopic`, el origen de los eventos y la entrega como mucho una vez en `docs/architecture.md` (§4.3 y §4.4) y la tabla de suites (§10.3); verificar que `docs/architecture.md` no describe ya el canal como trabajo futuro en §4.3

## 4. Canal en el servidor

- [ ] 4.1 Crear `backend/api/WebSocketRpcTransport.{h,cpp}` con `addChannelRoute()`: sesiones con saludo y plazo de 10 s, despacho de llamadas unarias en un pool de trabajo compartido con un máximo de 64 en vuelo por canal, suscripciones por identificador y `Cancel`, cola de salida acotada a 1 MiB con hilo escritor, y errores de protocolo con sus códigos de cierre; registrarlo desde `Backend` en `/api/channel`; verificar que `server` compila y que un cliente `ws::WebSocketClient` saluda y lista facturas
- [ ] 4.2 Límite de canales (`HttpOptions::maxChannels`, `WS_MAX_CHANNELS` en `server`, por defecto 64) con el pool de cpp-httplib dimensionado con `new_task_queue`, y cierre de todas las sesiones en `Backend::stop()` antes de `Server::stop()`; verificar que `server` arranca con la variable y se detiene con `SIGTERM` con canales abiertos sin esperar
- [ ] 4.3 Crear la suite `channel` (`api`, `DATABASE`) con `ws::WebSocketClient` y los frames del contrato, y un `RpcRouter` con procedimientos de prueba lentos y rápidos. Debe cubrir:
  - saludo aceptado, token incorrecto, llamada sin saludo y plazo del saludo;
  - correlación, llamadas concurrentes y errores iguales a HTTP;
  - suscripción con evento de una factura creada por otro canal, y cancelación;
  - texto e ilegibles, e identificador repetido en vuelo;
  - límite de canales con HTTP aún atendido;
  - cliente que no lee, cerrado sin bloquear la creación ni los demás canales;
  - parada del `Backend` con canales abiertos en menos de un segundo.

  Verificar con `ctest --preset native -L api -j8` varias veces seguidas, sin fallos intermitentes
- [ ] 4.4 Documentar el canal en `docs/architecture.md`:
  - §2 y §4.3: transporte, sobre y errores;
  - §4.4: `WS_MAX_CHANNELS`;
  - §7: saludo;
  - §9: HAProxy con WebSocket y `timeout tunnel`, y un único servidor sin escalado, del que dependen los eventos;
  - §12: hilos por canal y límite;
  - la tabla de suites (§10.3).

  Documentar también el nuevo transporte del backend en AGENTS.md y el canal en el CHANGELOG. Verificar que las variables y rutas documentadas coinciden con el código (`grep` de `WS_MAX_CHANNELS` y `/api/channel`)

## 5. Cliente sobre el canal y eventos en la vista

- [ ] 5.1 Añadir `qtwebsockets` a `scripts/setup-qt.sh` en los dos kits, con la comprobación de `Qt6WebSockets` para instalaciones existentes, y ejecutarlo; verificar que `~/software/Qt/6.12.0/{gcc_64,wasm_singlethread}/lib/cmake/Qt6WebSockets` existen y que `--desktop-only` solo instala el de escritorio
- [ ] 5.2 Ampliar `ApiTransport` (`Repeat`, `subscribe()`, disponibilidad) y `ApiConnection` (`call<Response>(..., Repeat)`, `subscribe<Response>()`, propiedad `available`); `InProcTransport` con suscripciones vía `Backend::subscribe()` y su cierre ordenado; verificar que el cliente nativo compila y que la suite `invoicerepository` pasa en proceso
- [ ] 5.3 Crear `WebSocketTransport` (decisión 8 del diseño: conexión perezosa, saludo, correlación, cola, reenvío solo de `Repeat::Safe`, espera exponencial, plazo, saludo rechazado sin reconexión, `Cancel`, `Options`) y `defaultChannelUrl()` en `ApiConfig.h`; retirar `HttpTransport` y configurar el canal en `main.cpp` para web y conectado; enlazar `Qt6::WebSockets`; verificar que el cliente compila en nativo y en WASM (`scripts/build-wasm.sh`)
- [ ] 5.4 `InvoiceRepository` con `request_id` (`QUuid`) y `Repeat::Safe` en `create()`, `Repeat::Safe` en `list()` y `watch()` con las señales `watching()` e `invoiceCreated(InvoiceRow)`; `Main.qml` con `watch()`, recarga con los eventos y aviso de backend no disponible; refrescar las traducciones con `update_translations` y traducir al español los textos nuevos; verificar que `client_qmllint` sigue limpio y que `guita_es.ts` no tiene entradas sin traducir
- [ ] 5.5 Pasar `invoicerepository` a `WebSocketTransport` e `InProcTransport` con los casos de hoy y añadir:
  - eventos de una factura creada por otra conexión, con `watching` primero, en los dos transportes;
  - `available` en los dos transportes;
  - token incorrecto con el mensaje del backend y sin reconexiones (contando las conexiones que recibe el servidor);
  - llamadas descartadas para objetos destruidos.

  Verificar con `ctest --preset native -L client`
- [ ] 5.6 Documentar el cliente:
  - `docs/architecture.md`: §1, §2, §3 (enlaces de `client`), §5.1, §5.3, §6 (eventos en proceso), §8 (Qt WebSockets), §12 (licencia) y la tabla de suites;
  - AGENTS.md: tabla de modos, principios, QML y C++ con `WebSocketTransport` y `Repeat`;
  - README: modos, tabla de estado y requisitos;
  - CHANGELOG.

  Verificar que no queda ninguna mención de `HttpTransport` en el código ni en los documentos (`grep -rn HttpTransport src docs AGENTS.md README.md`)

## 6. Reconexión del cliente

- [ ] 6.1 Crear `tests/support/CuttingProxy` (`QTcpServer`) que reenvía en los dos sentidos, puede retener lo que va del servidor al cliente y corta las dos conexiones a voluntad; verificar con un caso propio de la suite del grupo 6.2 que un cliente pasa a través del proxy sin cortes
- [ ] 6.2 Crear la suite `reconnection` (`client`, `DATABASE`) con el proxy entre `WebSocketTransport` y un `Backend` con HTTP. Debe cubrir:
  - creación con clave cortada tras la confirmación en la base de datos y antes de la respuesta: reenviada, con la misma factura y una sola fila;
  - llamada `Repeat::Never` en vuelo: falla con "Connection to the backend lost" y no se reenvía;
  - suscripción restablecida con `watching` de nuevo y la factura creada durante el corte en la lista recargada;
  - esperas crecientes y acotadas entre intentos;
  - plazo con el servidor inalcanzable;
  - `available` a `false` durante el corte y a `true` al volver.

  Con `Options` cortas, verificar con `ctest --preset native -L client -j8` varias veces seguidas, sin fallos intermitentes
- [ ] 6.3 Documentar la reconexión y el reenvío en `docs/architecture.md` (§5.1) y la suite en §10.3; verificar que los plazos y esperas documentados coinciden con los valores por defecto de `WebSocketTransport::Options`

## 7. Medición

- [ ] 7.1 Añadir a `tests/bench/latency_bench.cpp` el canal real (`WebSocketRpcTransport` + `RpcRouter` con el procedimiento sin base de datos) con `ws::WebSocketClient` y el sobre, y con `WebSocketTransport` de Qt; verificar que da resultados estables entre dos ejecuciones y que ninguna ida y vuelta del cliente Qt se acerca a los 40 ms (si lo hace, activar `TCP_NODELAY` en el cliente y repetir)
- [ ] 7.2 Medir el tamaño de `client.wasm` antes y después del cambio; documentar en `docs/architecture.md` §4.5 las filas nuevas, el tamaño y la lectura, y en §11 el servidor de bucle de eventos como trabajo pendiente; verificar que las órdenes de §4.5 funcionan tal cual

## 8. Integración

- [ ] 8.1 Ejecutar `ctest --preset native -j8` completo y `scripts/check-contract.sh`; verificar que todo pasa
- [ ] 8.2 Verificar en Xvfb, con `scripts/run-server.sh` y `API_TOKEN`:
  - dos clientes conectados: la factura creada en uno aparece en el otro sin tocarlo;
  - el cliente web (WASM) en Chrome igual, junto a uno de escritorio;
  - al reiniciar el servidor, los clientes muestran el aviso, reconectan solos y siguen recibiendo eventos;
  - en standalone la lista se actualiza con los eventos y `ss` no muestra ningún puerto en escucha.

  Dejar el build compilado para la verificación visual del usuario
- [ ] 8.3 Tras subir el usuario la rama y abrir el PR, verificar que el CI reinstala la toolchain con Qt WebSockets una vez, guarda la caché y termina en verde, con `check-contract.sh` incluido
