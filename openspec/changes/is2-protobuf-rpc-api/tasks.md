# Tasks

## 1. Dependencias y contrato

- [x] 1.1 Ampliar `scripts/build-deps.sh` con protobuf 36.2 (estático, PIC, abseil como submódulo, C++23, sin tests) y buf 1.74.0 (glaze se retira en 3.3, cuando el código deja de usarlo); añadir `-m qtgrpc` a `scripts/setup-qt.sh`; verificar que ambos scripts terminan sin errores y que `protoc --version` y `buf --version` dan las versiones esperadas
- [x] 1.2 Crear `src/proto/guita/v1/invoice_service.proto` y `src/proto/buf.yaml`, y la prueba de CTest `contract_lint` (`buf lint` y `buf format --diff --exit-code`); verificar que pasa y que falla con un mensaje mal nombrado
- [x] 1.3 Crear `src/proto/CMakeLists.txt` con las librerías `contract` (`protoc`, solo nativo) y `contract_qt` (`qt_add_protobuf`, `EXTRA_NAMESPACE qtc`), y apuntar los presets `native` y `wasm` a protobuf; verificar que los presets `native` y `wasm` configuran y compilan sin avisos
- [x] 1.4 Documentar el contrato, las dependencias nuevas y las reglas de evolución (`docs/architecture.md`, AGENTS.md, README); verificar que las órdenes documentadas funcionan tal cual

## 2. Interoperabilidad de los generadores

- [x] 2.1 Suite `contract` (`unit`): codificar con el código de `protoc` y decodificar con el de Qt Protobuf y al revés, con NIF ausente y presente, importes 0 y máximos de `int64` y listados; verificar con `ctest --preset native -L unit`

## 3. Backend RPC

- [x] 3.1 Crear `RpcRouter` en `backend/api`, independiente del transporte (`RpcCall`/`RpcResult`, registro tipado de handlers, códecs binario y JSON, autorización, códigos de Connect), con la suite `rpcrouter` (`unit`) que lo prueba con procedimientos de prueba y sin ningún transporte; verificar con `ctest --preset native -L unit`
- [x] 3.2 Crear `HttpRpcTransport` sobre cpp-httplib (ruta de procedimientos, códec por `Content-Type`, 415, 405 con `Allow: POST`, estados HTTP de Connect, cuerpo JSON de error con `google.protobuf.Struct`); verificar con las pruebas de la tarea 3.4
- [x] 3.3 Implementar `ListInvoices` y `CreateInvoice` sobre `InvoiceService` con la conversión entre mensajes y structs de `core`, servidos junto a las rutas REST hasta que el cliente cambie (grupo 4); verificar que `server` compila y atiende ambos
- [x] 3.4 Añadir la suite `rpcapi` (`api`): los dos procedimientos en binario y en JSON, NIF ausente, campos desconocidos y los errores 400, 401, 404, 405 y 415 con sus códigos; verificar con `ctest --preset native -L api`
- [x] 3.5 Hacer opcional la parte HTTP de `BackendOptions` y añadir `Backend::call(RpcCall)` con tipos estándar en `Backend.h`; verificar que `server` sigue igual (`rpcapi`) y que un `Backend` sin HTTP atiende llamadas en proceso sin escuchar en la red
- [x] 3.6 Actualizar `docs/architecture.md` (§4.3 pasa a describir el contrato RPC, el despacho independiente del transporte y sus errores) y el CHANGELOG; verificar que los ejemplos de JSON documentados funcionan con `curl` tal cual

## 4. Cliente

- [ ] 4.1 `ApiTransport` con `HttpTransport` (POST de procedimiento, `application/proto`, `Connect-Protocol-Version`, errores de Connect) e `InProcTransport` (`Backend::call()` en `QThreadPool`, resultado por la cola de eventos), y `ApiConnection` sobre ellos; el arranque standalone de `main.cpp` pasa a un `Backend` sin HTTP ni token con espera del pool al cerrar; verificar que el cliente compila en nativo y en WASM (solo `HttpTransport`)
- [ ] 4.2 `InvoiceRepository` con los mensajes de Qt Protobuf y el tipo de valor `InvoiceRow`; adaptar `Main.qml` a `InvoiceRow` y a `create()` con argumentos tipados; verificar que `client_qmllint` sigue limpio
- [ ] 4.3 Retirar las rutas REST (`InvoiceRoutes`), los helpers JSON de `Http`, la suite `restapi` y glaze (código, CMake, presets, `scripts/build-deps.sh` y documentación); verificar que `api` ya no enlaza glaze y que una petición a `/api/invoices` responde 404
- [ ] 4.4 Adaptar la suite `invoicerepository` (`client`) a `InvoiceRow` y a la nueva firma de `create()`, y ejecutarla con los dos transportes comprobando que los resultados coinciden; verificar con `ctest --preset native -L client`
- [ ] 4.5 Verificar en Xvfb que los modos standalone, conectado y web (WASM) guardan y listan facturas como antes, con y sin NIF, y con `ss` que el standalone no escucha en ningún puerto
- [ ] 4.6 Documentar los transportes y el principio revisado (AGENTS.md, `docs/architecture.md` §1, §2 y §6, README); verificar que las órdenes documentadas funcionan tal cual

## 5. Gobierno del contrato

- [ ] 5.1 Crear `scripts/check-contract.sh` (`buf breaking` contra la rama de desarrollo, sin error si esta aún no tiene contrato); verificar que pasa con el contrato actual y que falla al renumerar o eliminar un campo sin reservarlo en una copia de prueba
- [ ] 5.2 Documentar el flujo de evolución del contrato (AGENTS.md y `docs/architecture.md`); verificar que las órdenes documentadas funcionan tal cual

## 6. Medición

- [ ] 6.1 Crear `tests/bench/encoding_bench.cpp` (fuera de `ctest`) que mida tamaño y tiempo de codificar y decodificar JSON, JSON con gzip y binario para 1 000 y 10 000 facturas; verificar que se ejecuta en nativo y da resultados estables entre dos ejecuciones
- [ ] 6.2 Crear `tests/bench/latency_bench.cpp` (fuera de `ctest`) con la latencia de ida y vuelta de una llamada en proceso, de un `POST` con *keep-alive* y de un frame de WebSocket con los mismos bytes, estos dos sobre cpp-httplib en *loopback*; verificar que da resultados estables entre dos ejecuciones
- [ ] 6.3 Compilar la medición de codificación para WASM y ejecutarla en Chrome sin pantalla; documentar los resultados de las dos mediciones, en nativo y WASM, y el tamaño del `.wasm` del cliente antes y después del cambio, en `docs/architecture.md`

## 7. Integración

- [ ] 7.1 Ejecutar la batería completa (`ctest --preset native -j8`) y listar facturas con `buf curl` contra `scripts/run-server.sh`; verificar que todo pasa y que la herramienta Connect muestra el listado
