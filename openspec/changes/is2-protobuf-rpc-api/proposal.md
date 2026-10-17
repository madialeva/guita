# Proposal

## Why

Hoy el contrato entre cliente y backend no existe como tal: cada lado tiene su propia definición de los mensajes (structs reflejados con glaze en `InvoiceRoutes`, `QJsonObject`/`QVariantMap` en `InvoiceRepository`), los nombres de campo se repiten como cadenas en el cliente y QML recibe mapas sin tipo, así que un desajuste entre los dos lados solo aparece en ejecución. Para que la arquitectura sirva a una aplicación de facturación real, con decenas de servicios que evolucionan durante años, hace falta un contrato tipado, versionado y compartido del que se genere el código de ambos lados. Protobuf lo da y, además, reduce el volumen de los mensajes y el coste de decodificarlos, que importa en el cliente WASM, de un solo hilo. Es el momento más barato para cambiarlo: el API tiene un único recurso y aún no se ha publicado ninguna versión. Es el issue #2.

## What Changes

- **Contrato en Protobuf**: ficheros `.proto` versionados en el repositorio (paquete `guita.v1`) con los mensajes y los servicios; son la única fuente de verdad del API entre cliente y backend.
- **Dos generadores a partir del mismo `.proto`** (decidido con el usuario): `protoc` genera el código C++ del backend, sin Qt, y Qt Protobuf (`qtprotobufgen`) el del cliente, en nativo y en WebAssembly. El contrato es el `.proto`; los dos lados producen los mismos bytes.
- **Transporte RPC sobre HTTP, sin gRPC**: cada método es un `POST /api/<paquete.Servicio>/<Método>` unario sobre el servidor cpp-httplib del backend, siguiendo el protocolo Connect para llamadas unarias, de modo que herramientas compatibles con Connect pueden llamar al API. Funciona con HTTP/1.1, en el navegador sin proxy y en el modo standalone igual que ahora.
- **Despacho independiente del transporte**: el backend resuelve los procedimientos sin saber por dónde llegan; HTTP es el primer transporte, y un canal WebSocket persistente (issue posterior, con eventos push del servidor) podrá reutilizar los mismos handlers.
- **Transporte en proceso para el modo standalone**: el cliente de escritorio standalone entrega los mensajes del contrato directamente al backend embebido, sin red. Deja de abrir un puerto en `127.0.0.1` y de necesitar un token, y se elimina la sobrecarga de HTTP y de TCP en ese modo. Web y conectado siguen usando HTTP.
- **Principio revisado**: el cliente ya no "habla siempre HTTP/JSON", sino que siempre habla con el backend a través del contrato RPC y nunca sabe qué transporte lo lleva.
- **Dos codificaciones del mismo contrato**: `application/proto` (binario, la que usa el cliente) y `application/json` con el mapeo JSON canónico de proto3, para depurar con `curl` y las herramientas del navegador.
- **Modelo de errores común** para todos los métodos, el de Connect (`code` y `message`), que sustituye a `{"error": ...}`.
- **BREAKING**: `GET`/`POST /api/invoices` se sustituyen por `InvoiceService.ListInvoices` y `InvoiceService.CreateInvoice`. No hay consumidores externos ni versiones publicadas: solo el propio cliente.
- **NIF ausente explícito**: el NIF del cliente pasa a ser un campo opcional del contrato, así que "sin NIF" ya no se representa como cadena vacía.
- **Backend**: un despachador de procedimientos en `backend/api`, que sustituye a las rutas REST y a sus structs JSON. Los servicios y stores de `backend/core` mantienen sus reglas de negocio, validaciones y SQL, y no dependen del contrato. glaze deja de usarse: la codificación JSON la da protobuf.
- **Cliente**: `ApiConnection` ofrece la llamada RPC genérica sobre un transporte intercambiable (HTTP o en proceso) e `InvoiceRepository` trabaja con los mensajes generados en lugar de `QVariantMap`, y entrega a QML valores tipados.
- **Build**: generación desde los mismos `.proto` para los dos lados, en los presets `native` y `wasm`.
- **Gobierno del contrato**: reglas de evolución (no reutilizar números de campo, `reserved` para los eliminados, añadir en vez de modificar), comprobadas con `buf lint` y `buf breaking`.
- **Medición**: comparación de JSON, JSON comprimido y Protobuf (tamaño y tiempo de codificación y decodificación, también en WebAssembly) con un listado grande, documentada en `docs/`.
- Fuera de alcance: gRPC/HTTP2, streaming (si hace falta, los WebSockets de cpp-httplib) e integraciones con terceros (formatos de la AEAT como VeriFactu o SII, Facturae), que irán por adaptadores propios.

## Capabilities

### New Capabilities
- `api-rpc-protobuf`: contrato tipado en Protobuf entre cliente y backend y su transporte RPC sobre HTTP (procedimientos, codificaciones binaria y JSON, modelo de errores, autenticación Bearer, compatibilidad con Connect y reglas de evolución del contrato).

### Modified Capabilities
- `suite-de-pruebas`: la cobertura exigida pasa del contrato REST al contrato RPC y añade la interoperabilidad entre los dos generadores.

## Impact

- Código: nuevo directorio `src/proto/` con el contrato; `src/backend/api` (despachador RPC en lugar de `InvoiceRoutes` y de los helpers JSON de `Http`, transporte HTTP opcional y llamada en proceso en `Backend`); `src/client` (`ApiConnection` y sus transportes, `InvoiceRepository`, `Main.qml`, el arranque standalone en `main.cpp`); CMake de ambos lados; pruebas del API y del cliente.
- Modo standalone: deja de escuchar en la red y de generar un token por arranque.
- Dependencias de producción (requieren esta aprobación):
  - Backend: librería C++ de protobuf y `protoc`, compiladas con `scripts/build-deps.sh` como libpqxx (el paquete de EL9 es la versión 3.14, de 2020).
  - Cliente: módulo `qtgrpc` de Qt 6.12, que contiene Qt Protobuf, para los kits de escritorio y WASM (`scripts/setup-qt.sh`).
  - Se retira glaze.
- Herramientas de desarrollo: `buf` para lint y detección de cambios incompatibles.
- API: incompatible con la actual; no afecta al esquema de la base de datos ni a las migraciones.
- Documentación: `docs/architecture.md` (la sección del API REST pasa a describir el contrato RPC y los transportes, y el modo standalone el transporte en proceso), AGENTS.md (principios, reglas del backend y del contrato), README y CHANGELOG.
