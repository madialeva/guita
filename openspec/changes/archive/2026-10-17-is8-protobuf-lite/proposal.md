# Proposal

## Why

El contrato Protobuf se genera hoy dos veces: `protoc` para el backend y Qt Protobuf para el cliente. Se eligió Qt Protobuf para no compilar libprotobuf con emsdk y por sus tipos Qt, pero las mediciones han dado la vuelta a ese razonamiento:
- **Qt Protobuf es el códec más lento del proyecto.** Con 10 000 facturas en Chrome, codifica en 17 ms y decodifica en 15–16 ms; el código de `protoc` compilado a WebAssembly lo hace en 0,3 ms y 1,1 ms, unas 50 y 14 veces más rápido, prácticamente igual que en nativo.
- **Su ventaja para QML no existe.** Los `int64` no son accesibles desde QML, así que el repositorio ya adapta los mensajes a `InvoiceRow` de todas formas.
- **ASN.1 ya está descartado.** La comparación con asn1c (#7, cerrado como "not planned") no compensaba.

Con un solo generador, el cliente y el servidor usan exactamente el mismo código del contrato. La variante lite de libprotobuf es igual de rápida y mucho más pequeña: añade unos 297 KB frente a 1,24 MB de la completa, medido contra un programa vacío. Lite no tiene JSON ni reflexión, pero el API no los necesita: el cliente solo usa binario, `buf curl` envía `application/proto` aunque se le escriba JSON, y `protoc --decode`/`--encode` sirven para leer y escribir mensajes binarios a mano. Es el issue #8.

## What Changes

- **Un solo generador del contrato: `protoc`, en lite.** El `.proto` declara `optimize_for = LITE_RUNTIME` y la librería `contract` (`guita::v1`) se genera una vez y la usan el backend y el cliente, en nativo y en WebAssembly. Se retira Qt Protobuf (`contract_qt`, el espacio de nombres `qtc`, el módulo `qtgrpc` de Qt).
- **libprotobuf-lite en los dos lados.** Para el cliente web, libprotobuf y abseil se compilan con emsdk (kit `wasm_singlethread`), con la misma versión que en nativo.
- **BREAKING: el API deja de aceptar JSON.** `application/json` responde 415 y solo queda `application/proto`. Los errores siguen siendo los de Connect, un cuerpo JSON `{"code", "message"}`, ahora escrito sin la reflexión de protobuf. No hay consumidores externos ni versiones publicadas.
- **Depuración sin JSON en el API:** `buf curl` (que convierte el JSON en local) y `protoc --decode`/`--encode` sobre el contrato, documentados con ejemplos.
- **Comprobación del contrato al arrancar sin descriptores.** Lite no los tiene, así que el registro de procedimientos comprueba que los tipos de petición y respuesta son los del método según la convención de nombres del contrato (`<Método>Request`, `<Método>Response`), que `buf lint` ya impone.
- **Cliente:** `ApiConnection` y `InvoiceRepository` trabajan con los mensajes de `protoc`. Las vistas reciben los mismos `InvoiceRow`, así que QML no cambia.
- **Mediciones:** se rehacen las de codificación (nativo y WASM) y latencia con el nuevo códec del cliente, se mide el tamaño de `client.wasm` antes y después, y se añade a la latencia la llamada con cuerpo. Esa fila la reveló la comparación con ASN.1: el cliente de cpp-httplib cobra unos 6 µs más por cualquier petición con cuerpo.

## Capabilities

### New Capabilities

Ninguna.

### Modified Capabilities

- `api-rpc-protobuf`:
  - el contrato se genera con un único generador para los dos lados;
  - la codificación JSON del API desaparece (solo binario, JSON responde 415);
  - el modelo de errores ya no incluye mensajes JSON mal formados.
- `suite-de-pruebas`: la cobertura pasa de "dos codificaciones e interoperabilidad entre generadores" a una sola codificación, con bytes de referencia del contrato que también se comprueban en WebAssembly.

## Impact

- **Código:**
  - `src/proto` (opción lite, una sola librería en nativo y WASM, `protoc` del host en el build WASM);
  - `src/backend/api` (`RpcRouter` sin JSON ni descriptores, `HttpRpcTransport` con el error JSON escrito a mano, `RpcCall` sin códec);
  - `src/client` (`ApiConnection`, `InvoiceRepository`, CMake);
  - pruebas (`contract`, `rpcrouter`, `rpcapi`, `invoicerepository`) y mediciones.
- **Dependencias:**
  - se retira el módulo `qtgrpc` de Qt;
  - libprotobuf pasa a enlazarse en el cliente web, compilada con emsdk por `scripts/build-deps.sh` en `~/software/protobuf-36.2-wasm`, después de `scripts/setup-qt.sh`, que instala emsdk.

  La librería de protobuf ya era una dependencia aprobada del proyecto; cambian la variante (lite) y dónde se enlaza.
- **API:** sin JSON (BREAKING, 0.1.0 sin publicar). Las rutas, los mensajes binarios y los errores no cambian. El esquema de la base de datos no cambia.
- **Documentación:**
  - `docs/architecture.md`: contrato, API, cliente, toolchain, mediciones, licencias y tabla de suites;
  - AGENTS.md: contrato, dependencias y QML;
  - README: requisitos, dependencias y ejemplos;
  - CHANGELOG: se corrige la entrada del API, que aún no se ha publicado.
