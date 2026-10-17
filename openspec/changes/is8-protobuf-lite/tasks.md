# Tasks

## 1. Dependencias y build del contrato en WASM

- [x] 1.1 Ampliar `scripts/build-deps.sh` con protobuf 36.2 para WASM:
  - mismo checkout del tag, `emcmake`, estático, C++23, abseil como módulo, sin `protoc`, sin pruebas y sin upb;
  - instalación en `~/software/protobuf-36.2-wasm`;
  - si falta emsdk, aviso y se salta esa parte.

  Verificar que el script termina sin errores con emsdk y sin él, y que instala `libprotobuf.a` y `libprotobuf-lite.a` para WASM.
- [x] 1.2 Generar `contract` también en el build WASM: el `protoc` nativo como `PROTOC_EXE`, y la protobuf de WASM en el preset `wasm` (`CMAKE_PREFIX_PATH`, `CMAKE_FIND_ROOT_PATH`). Verificar que `cmake --build --preset wasm --target contract` compila sin avisos del proyecto y que el preset `native` no cambia.
- [x] 1.3 Documentar la dependencia y el orden de instalación (`docs/architecture.md` §8, README, AGENTS.md). Verificar que las órdenes documentadas funcionan tal cual.

## 2. Backend sin JSON ni descriptores

- [ ] 2.1 Retirar `RpcCodec` de `RpcCall`, y de `RpcRouter` el códec JSON; comprobar el contrato al registrar con `GetTypeName()` según la convención `<Método>Request`/`<Método>Response`. Adaptar `InProcTransport` a la nueva `RpcCall` y la suite `rpcrouter` (casos binarios y rechazo de tipos de otro método). Verificar con `ctest --preset native -L unit`.
- [ ] 2.2 `HttpRpcTransport`: solo `application/proto` (JSON y otros tipos dan 415) y `connectErrorBody()`, sin `google.protobuf.Struct`. Adaptar la suite `rpcapi`:
  - casos binarios y campo desconocido;
  - `application/json` → 415;
  - binario mal formado → 400;
  - `connectErrorBody()` con comillas, barras y texto no ASCII, leído con `QJsonDocument`.

  Verificar con `ctest --preset native -L api`.
- [ ] 2.3 Documentar el API binario y la depuración con `buf curl` y `protoc --encode`/`--decode` (`docs/architecture.md` §4.3 y §4.4, README) y corregir las entradas del CHANGELOG. Verificar los ejemplos contra `scripts/run-server.sh` tal como se documentan.

## 3. Cliente sobre los mensajes de `protoc`

- [ ] 3.1 `ApiConnection::call<Response>()` con `MessageLite` e `InvoiceRepository` con los mensajes de `guita::v1`; enlazar `contract` en el cliente, nativo y WASM. Verificar que el cliente compila en los dos presets y que `client_qmllint` sigue limpio.
- [ ] 3.2 Retirar Qt Protobuf:
  - `contract_qt`, `qt_add_protobuf()` y el componente `Protobuf` de Qt;
  - `qtgrpc` en `scripts/setup-qt.sh`;
  - en las pruebas y en `bench_encoding`, que pasa a los mensajes de `protoc`.

  Reescribir la suite `contract` con los bytes de referencia de `tests/support/ProtoSamples.h`, las idas y vueltas y los campos desconocidos. Verificar que ningún objetivo enlaza `Qt6::Protobuf` y que `ctest --preset native -L unit` pasa.
- [ ] 3.3 Adaptar la suite `invoicerepository` a los nuevos mensajes, con los dos transportes. Verificar con `ctest --preset native -L client`.
- [ ] 3.4 Verificar en Xvfb que los modos standalone, conectado y web (WASM) guardan y listan facturas como antes, con y sin NIF, y que el standalone no escucha en ningún puerto.
- [ ] 3.5 Documentar el cliente y el contrato generado una vez (`docs/architecture.md` §3, §5 y §12, AGENTS.md). Verificar que no queda ninguna mención a Qt Protobuf fuera de la explicación de por qué se dejó.

## 4. Lite

- [ ] 4.1 Declarar `option optimize_for = LITE_RUNTIME;` en el contrato y enlazar `protobuf::libprotobuf-lite` en lugar de la completa. Verificar:
  - que `buf lint` y `buf format` pasan;
  - que `scripts/check-contract.sh` no detecta cambios incompatibles;
  - que ningún objetivo enlaza `protobuf::libprotobuf`;
  - que `ctest --preset native -j8` pasa;
  - que el preset `wasm` compila.
- [ ] 4.2 Documentar la variante lite y lo que deja fuera (JSON, reflexión) en `docs/architecture.md` §3 y AGENTS.md. Verificar que las órdenes documentadas funcionan tal cual.

## 5. Medición

- [ ] 5.1 Ejecutar `bench_encoding` en nativo (preset `bench`) y en WASM (preset `wasm-bench`, en Chrome sin pantalla), con la comprobación de los bytes de referencia. Verificar que da resultados estables entre dos ejecuciones.
- [ ] 5.2 Añadir a `bench_latency` una llamada con cuerpo. Verificar que da resultados estables entre dos ejecuciones.
- [ ] 5.3 Medir `client.wasm` antes y después del cambio y documentar los resultados y su lectura en `docs/architecture.md` §4.5, conservando las cifras de Qt Protobuf como referencia.

## 6. Integración

- [ ] 6.1 Ejecutar la batería completa (`ctest --preset native -j8`) y, contra `scripts/run-server.sh`:
  - listar facturas con `buf curl`;
  - crear una con `protoc --encode` y `curl`, leyendo la respuesta con `protoc --decode`;
  - comprobar que una petición `application/json` da 415.

  Verificar que todo pasa.
