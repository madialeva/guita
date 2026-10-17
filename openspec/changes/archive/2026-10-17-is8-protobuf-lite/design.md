# Design

## Context

Ver proposal.md (Why) y las specs `api-rpc-protobuf` y `suite-de-pruebas`. Lo que condiciona el diseño, comprobado con una prueba en WebAssembly antes de proponer el cambio:

- **protobuf 36.2 con abseil compila para el kit `wasm_singlethread` de Qt** con emsdk 5.0.5, sin hilos y con los flags por defecto de Emscripten. El cliente Qt tampoco usa excepciones de WASM ni SIMD, así que las librerías casan.
  - Salen unos mil avisos, casi todos de abseil por usar macros de versión de Emscripten obsoletas (`__EMSCRIPTEN_major__`).
  - Con el mismo fuente se obtienen `libprotobuf` y `libprotobuf-lite`.
- **Velocidad en Chrome con 10 000 facturas:**

  | Códec | Codificar | Decodificar |
  |---|---|---|
  | Código de `protoc` con libprotobuf completa | 0,35 ms | 1,15–1,2 ms |
  | Código de `protoc` con lite | 0,30 ms | 1,05 ms |
  | Qt Protobuf (hoy) | 17 ms | 15–16 ms |

  Node da las mismas cifras de `protoc`. Completa y lite rinden igual.
- **Tamaño frente a un programa vacío** (`-O3`, mismos flags de enlace): +1,24 MB la completa y +297 KB lite, parte de los cuales es libc++, que el cliente ya incluye. Qt Protobuf añadió en su día 211 KiB a `client.wasm`.
- **Lo que lite no tiene y el código actual usa:**
  - JSON (`JsonStringToMessage`, `MessageToJsonString`), en `RpcRouter` y en el cuerpo de error de `HttpRpcTransport` (a través de `google.protobuf.Struct`);
  - descriptores (`DescriptorPool`, `Message::descriptor()`), en la comprobación del contrato de `RpcRouter::add()`.

  Lite sí tiene `GetTypeName()`, que da el nombre completo del mensaje (`guita.v1.ListInvoicesRequest`).
- **`buf curl` envía `application/proto`**, aunque la petición se le escriba en JSON (comprobado). `protoc --encode`/`--decode` convierten entre el formato de texto de Protobuf y el binario.
- **`protobuf_generate()` acepta `PROTOC_EXE`.** El build WASM puede usar el `protoc` nativo, porque la instalación WASM no lo trae.

## Goals / Non-Goals

**Goals:**
- Un único código generado del contrato, el de `protoc`, para el backend y el cliente, en nativo y en WASM.
- El códec del cliente web al nivel del nativo, y un `client.wasm` que no crezca de forma apreciable.
- El mismo comportamiento visible para el usuario; para quien depura el API, las herramientas que sustituyen al JSON.

**Non-Goals:**
- Volver a servir JSON en el API. Si hiciera falta, exigiría la libprotobuf completa en el backend y, por tanto, otra generación del contrato (Decisión 1).
- Las funciones de libprotobuf que dependen de reflexión (`DebugString`, `TextFormat`, descriptores) en el código del proyecto. Las herramientas de línea de órdenes las cubren.
- El canal WebSocket (#5), el CI (#4) y gRPC.
- Conservar Qt Protobuf como alternativa.

## Decisions

### 1. Lite en los dos lados, por opción del `.proto`

`src/proto/guita/v1/invoice_service.proto` declara `option optimize_for = LITE_RUNTIME;`. La opción afecta a todo el código generado del fichero, así que los dos lados usan lite.
- *Alternativas*:
  - **Completa en los dos lados:** conserva JSON y reflexión, pero cuesta +1,24 MB en el `.wasm`, con la misma velocidad.
  - **Completa en el backend y lite en el cliente:** obliga a generar el contrato dos veces con opciones distintas, que es justo lo que este cambio elimina.

### 2. Una librería `contract`, en nativo y en WASM

- **`src/proto/CMakeLists.txt`** genera `contract` con `protobuf_generate()` en los dos builds y la enlaza con `protobuf::libprotobuf-lite` (pública). Se retiran `contract_qt`, `qt_add_protobuf()`, el espacio de nombres `qtc` y el componente `Protobuf` de Qt en el `find_package` raíz.
- **El `protoc` del host.** En WASM, `find_program()` busca el `protoc` nativo (`~/software/protobuf-36.2/bin`, fuera de la raíz de búsqueda del toolchain) y se pasa como `PROTOC_EXE`.
- **Presets.** El preset `wasm` encuentra la protobuf de WASM con `CMAKE_PREFIX_PATH` y `CMAKE_FIND_ROOT_PATH` (el toolchain de Emscripten solo busca en su sysroot). Deja de necesitar `QT_ADDITIONAL_HOST_PACKAGES_PREFIX_PATH`, que existía para Qt Protobuf.
- **El cliente** enlaza `contract`; el standalone, además, `api`, que ya la enlaza, así que el contrato está una sola vez en el ejecutable.

### 3. protobuf para WASM en `scripts/build-deps.sh`

- **Qué compila.** El mismo checkout del tag, compilado con `emcmake` en `~/software/protobuf-36.2-wasm`: estático, C++23, abseil como módulo, sin `protoc`, sin pruebas y sin upb. La misma variable de versión que la nativa, porque el código generado comprueba la versión de la librería.
- **Si falta emsdk** (lo instala `scripts/setup-qt.sh`), el script avisa y se salta esta parte, para no impedir instalar solo el servidor. El orden documentado pasa a ser `setup-qt.sh` y después `build-deps.sh`.
- `setup-qt.sh` deja de instalar el módulo `qtgrpc`.

### 4. `RpcRouter` sin JSON ni descriptores

- **`RpcCall` pierde el códec** (`RpcCodec` desaparece): solo hay una codificación. `rpc::decode`/`encode` pasan a ser `ParseFromString`/`SerializeAsString`. Los campos desconocidos los conserva lite como bytes y el handler no los ve.
- **Comprobación del contrato.** `add<Request, Response>("guita.v1.InvoiceService/ListInvoices", ...)` comprueba que `Request::default_instance().GetTypeName()` es `guita.v1.ListInvoicesRequest` y que el de la respuesta es `guita.v1.ListInvoicesResponse`: el paquete del servicio y la convención de nombres que impone `buf lint` (`STANDARD`).
  - Detecta tipos cambiados o de otro método, pero no un método que el servicio no declare; eso lo cubren las suites del API.
- **Mensaje de error** de una petición mal formada: `Malformed <GetTypeName()>`.
- *Alternativas*:
  - **Mantener `RpcCodec` con un solo valor:** deja una abstracción vacía en `Backend.h`.
  - **Un plugin de `protoc` que genere la tabla de métodos:** demasiado para dos métodos.

### 5. Errores de Connect sin reflexión

`HttpRpcTransport` escribe `{"code":"…","message":"…"}` con una función propia, `connectErrorBody()`, expuesta en su cabecera para probarla.
- Escapa comillas, barras invertidas y caracteres de control (`\uXXXX`); el UTF-8 pasa tal cual, como permite JSON.
- `application/json` y cualquier otro tipo distinto de `application/proto` dan 415.
- Los errores siguen siendo JSON porque así los define Connect para cualquier codificación, y el cliente los lee con `QJsonDocument` como hasta ahora.

### 6. Cliente sobre los mensajes de `protoc`

- **`ApiConnection::call<Response>()`** recibe un `google::protobuf::MessageLite` y decodifica con `Response::ParseFromArray()`. Desaparece `QProtobufSerializer`.
- **`InvoiceRepository`** construye los mensajes de `guita::v1` y convierte `std::string` ↔ `QString` (UTF-8) al pasar a `InvoiceRow`.
- **`InvoiceRow`** no cambia; su comentario explica ahora que los `double` se deben a que el `int` de QML tiene 32 bits.
- Las vistas, `qmllint` y los transportes no cambian.

### 7. Depuración sin JSON en el API

Se documentan, con ejemplos verificados contra `scripts/run-server.sh`:
- `buf curl --schema src/proto --data '{...}' <url>`: JSON escrito a mano, enviado en binario y mostrado en JSON.
- `protoc --encode=guita.v1.CreateInvoiceRequest` sobre el formato de texto, enviado con `curl`, y la respuesta leída con `protoc --decode=guita.v1.CreateInvoiceResponse`.

No hace falta ninguna herramienta propia.

### 8. Pruebas

- **`contract` (`unit`), reescrita:**
  - bytes de referencia de mensajes fijos, calculados a mano según el formato de Protobuf, en `tests/support/ProtoSamples.h`;
  - ida y vuelta con NIF ausente, vacío y presente, y con los límites de `int64`;
  - campos desconocidos de una versión posterior: se ignoran al leer y se conservan al reescribir.

  La medición WASM comprueba los mismos bytes (Decisión 9), que es lo que pide "Mismos bytes en los dos lados".
- **`rpcrouter`:** los casos JSON pasan a binario; añade el rechazo de tipos que no son los del método.
- **`rpcapi`:**
  - binario, con un campo desconocido;
  - `application/json` → 415;
  - mensaje binario mal formado → 400;
  - errores leídos con `QJsonDocument`;
  - `connectErrorBody()` con comillas, barras y texto no ASCII.
- **`invoicerepository`:** los mismos casos, con los dos transportes, sobre los nuevos mensajes.

### 9. Medición

- **`bench_encoding`** se rehace sobre los mensajes de `protoc` (nativo y WASM): tamaño, comprimido, codificar y decodificar, frente a JSON. Antes de medir comprueba los bytes de `ProtoSamples.h`.
- **`bench_latency`** añade una llamada con cuerpo, porque el cliente de cpp-httplib cobra unos 6 µs más por una petición con cuerpo (hallado en #7).
- **Tamaño:** se mide `client.wasm` antes y después.
- **`docs/architecture.md` §4.5** sustituye las cifras de Qt Protobuf por las nuevas y conserva las anteriores como referencia de por qué se cambió.

### 10. Documentación

- **`docs/architecture.md`:**
  - el contrato y su generación (§3);
  - el API y la depuración (§4.3);
  - el backend (§4.4) y el cliente (§5);
  - el toolchain (§8), las mediciones (§4.5), las suites (§10.3) y las licencias (§12, Qt Protobuf deja de usarse).
- **AGENTS.md:** contrato, dependencias y la regla de QML sobre `int64`.
- **README:** requisitos, orden de instalación y ejemplos de depuración.
- **CHANGELOG:** como 0.1.0 no se ha publicado, se corrigen las entradas de `[Unreleased]` que mencionan JSON o Qt Protobuf, en lugar de añadir una entrada `Removed`.

## Risks / Trade-offs

- [Compilar protobuf y abseil dos veces (nativo y WASM) alarga la instalación] → Una vez por máquina, en `~/software`; el CI (#4) deberá cachear las dos instalaciones.
- [Versiones distintas de protobuf en nativo y WASM romperían el código generado] → Una sola variable de versión en `build-deps.sh`, y el mismo checkout del tag para las dos.
- [`client.wasm` puede crecer más de lo estimado] → Se mide antes y después (Decisión 9). Lite es la variante pequeña y Qt Protobuf se retira.
- [Avisos de abseil en el build WASM] → Las cabeceras de los objetivos importados se tratan como de sistema y no avisan en el código del proyecto; se comprueba al compilar.
- [Sin descriptores, la comprobación al arrancar es más débil] → Sigue detectando tipos cambiados. Un método que no exista en el servicio lo detectan `buf lint` y las suites del API, que llaman a todos los procedimientos.
- [Clientes Connect que usen JSON por defecto (por ejemplo, connect-web) no funcionarían] → Connect permite que un servidor admita solo algunas codificaciones; el cliente del proyecto y `buf curl` usan binario. Queda documentado.
- [Lite no imprime mensajes en los registros (`DebugString`)] → Los registros muestran el procedimiento y el tipo; el contenido se inspecciona con `protoc --decode`.
- [Excepciones en WASM: protobuf se compila sin capturarlas, como Qt] → Lite informa de los errores de parseo con valores de retorno, no con excepciones.

## Migration Plan

Cambio en la versión 0.1.0, no publicada: cliente y servidor cambian a la vez, sin datos que migrar y sin cambios en el esquema de la base de datos ni en los mensajes binarios. Revertir es revertir el merge del cambio.
