# canal-websocket Specification

## Purpose

Canal persistente entre el cliente y el backend sobre WebSocket: lleva las llamadas a los procedimientos del contrato y los eventos del servidor por una única conexión de larga vida, autenticada en su primer mensaje, y el cliente lo mantiene abierto reconectando y reenviando solo lo que es seguro repetir.

## Requirements

### Requirement: Canal en el servidor
Cuando el backend sirve HTTP, SHALL aceptar conexiones WebSocket en `/api/channel`, en el mismo puerto que el API y los ficheros del cliente web. Cada mensaje binario del WebSocket SHALL ser un frame del sobre definido en el contrato (paquete `guita.channel.v1`). Un mensaje de texto o un frame que no se pueda decodificar MUST cerrar la conexión con un error de protocolo.

#### Scenario: Conexión al canal
- **WHEN** un cliente abre un WebSocket en `/api/channel` del servidor
- **THEN** la conexión se acepta y queda a la espera del primer frame

#### Scenario: Frame ilegible
- **WHEN** un cliente autenticado envía un mensaje de texto o bytes que no son un frame del sobre
- **THEN** el servidor cierra la conexión con un código de error de protocolo

### Requirement: Autenticación en el primer mensaje
El primer frame del cliente SHALL ser el de saludo, con la misma autorización que se envía por HTTP en la cabecera `Authorization` (`Bearer <token>`), y el servidor SHALL comprobarla con la misma autorización configurada que el API HTTP. Hasta aceptar el saludo, el servidor MUST NOT atender ninguna llamada. Un saludo rechazado SHALL responderse con un error `unauthenticated` antes de cerrar la conexión, y una conexión que no salude en 10 segundos SHALL cerrarse.

#### Scenario: Saludo aceptado
- **WHEN** el primer frame es un saludo con un token válido
- **THEN** el servidor confirma el saludo y atiende las llamadas siguientes de esa conexión

#### Scenario: Token incorrecto
- **WHEN** el saludo lleva un token incorrecto o ninguno, con el servidor configurado con `API_TOKEN`
- **THEN** el servidor responde con un error `unauthenticated` y cierra la conexión sin ejecutar ninguna llamada

#### Scenario: Llamada sin saludo
- **WHEN** el primer frame de la conexión es una llamada en lugar del saludo
- **THEN** el servidor cierra la conexión sin ejecutar la llamada

#### Scenario: Sin saludo
- **WHEN** una conexión abierta no envía ningún frame en 10 segundos
- **THEN** el servidor la cierra

### Requirement: Llamadas correlacionadas y concurrentes
Cada llamada del canal SHALL llevar un identificador de correlación elegido por el cliente, el nombre completo del procedimiento (`guita.v1.InvoiceService/CreateInvoice`) y el mensaje de petición en binario, y su respuesta SHALL llevar el mismo identificador. Las respuestas y los errores SHALL ser los mismos que por HTTP: el mensaje de respuesta del método, o un error con su código de Connect (`invalid_argument`, `unauthenticated`, `unimplemented`, `internal`) y el mensaje del backend. Una conexión SHALL admitir varias llamadas en vuelo a la vez, respondidas en el orden en que terminan, sin que una llamada lenta retrase a las demás. Un identificador que ya está en vuelo en la misma conexión MUST rechazarse con `invalid_argument`.

#### Scenario: Llamada correcta
- **WHEN** un cliente autenticado llama por el canal a `ListInvoices`
- **THEN** recibe una respuesta con el mismo identificador de correlación y el listado de facturas

#### Scenario: Mismos errores que por HTTP
- **WHEN** se crea una factura sin cliente, o se llama a un procedimiento inexistente, por el canal
- **THEN** la respuesta es un error con código `invalid_argument` y el mensaje `Customer is required`, o con código `unimplemented`, como por HTTP

#### Scenario: Llamadas concurrentes
- **WHEN** un cliente envía una llamada lenta y, sin esperar su respuesta, otra rápida por la misma conexión
- **THEN** la respuesta de la rápida llega antes que la de la lenta, y cada una con su identificador

### Requirement: Eventos por el canal
Una llamada del canal a un procedimiento de eventos del contrato SHALL abrir una suscripción: el servidor SHALL enviar cada mensaje del procedimiento con el identificador de correlación de esa llamada, en orden, hasta que el cliente la cancele o se cierre la conexión. Al cancelarla o cerrar la conexión, el servidor MUST dejar de enviar sus mensajes y liberar la suscripción. Un error al abrirla (sin autorización, procedimiento inexistente, petición mal formada) SHALL responderse como el de cualquier otra llamada.

#### Scenario: Suscripción a eventos
- **WHEN** un cliente se suscribe a `WatchInvoices` por el canal y otro cliente crea una factura
- **THEN** el primero recibe, con el identificador de su suscripción, el aviso de inicio y después el de la factura creada

#### Scenario: Cancelación
- **WHEN** el cliente cancela su suscripción y después se crea otra factura
- **THEN** no recibe ningún mensaje más de esa suscripción

### Requirement: Límites del servidor
El servidor SHALL limitar el número de canales abiertos a la vez (`WS_MAX_CHANNELS`, por defecto 64) de modo que las peticiones HTTP, incluidos los ficheros del cliente web, siempre puedan atenderse; una conexión por encima del límite MUST rechazarse indicando que se reintente más tarde. Un canal cuyo cliente no lee lo que el servidor le envía y acumula más de 1 MiB pendiente MUST cerrarse sin bloquear a los demás canales ni las llamadas que lo produjeron. Los frames de más de 16 MiB MUST rechazarse, como los cuerpos HTTP.

#### Scenario: Demasiados canales
- **WHEN** hay tantos canales abiertos como el límite y llega uno más
- **THEN** la conexión nueva se rechaza y una petición HTTP al servidor se sigue atendiendo

#### Scenario: Cliente que no lee
- **WHEN** un cliente suscrito deja de leer del canal mientras se crean facturas
- **THEN** el servidor cierra ese canal, y la creación de facturas y los demás canales siguen funcionando

### Requirement: Cliente sobre el canal
En los modos web y conectado, el cliente SHALL hacer todas sus llamadas y suscripciones por el canal del servidor: en web, el del mismo origen que la página (`wss` si la página es `https`); en conectado, el que corresponde a `API_URL`. El cliente SHALL abrir el canal con la primera llamada o suscripción y SHALL indicar a la interfaz si el backend está disponible.

#### Scenario: Modo conectado
- **WHEN** la aplicación de escritorio arranca en modo conectado contra un servidor y lista facturas
- **THEN** las llamadas viajan por un único WebSocket con el servidor y ninguna por `POST`

#### Scenario: Backend no disponible
- **WHEN** se pierde la conexión con el servidor
- **THEN** la interfaz indica que el backend no está disponible hasta que el canal se restablece

### Requirement: Reconexión con reenvío seguro
Al perder la conexión, el cliente SHALL reconectar con espera exponencial (de 0,5 s a 30 s como máximo, con variación aleatoria) mientras tenga llamadas pendientes o suscripciones activas, y SHALL volver a saludar. Tras reconectar, el cliente SHALL enviar las llamadas que aún no había enviado y SHALL reenviar las que estaban en vuelo solo si es seguro repetirlas: lecturas y operaciones con clave de idempotencia. Las demás llamadas en vuelo MUST fallar con un error de conexión perdida, sin reenviarse. Una llamada sin respuesta 30 segundos después de hacerse SHALL fallar con un error de backend no disponible.

#### Scenario: Creación reenviada sin duplicar
- **WHEN** la conexión se corta después de que el servidor reciba una creación con clave de idempotencia y antes de que el cliente reciba la respuesta
- **THEN** el cliente reconecta, reenvía la creación y recibe la misma factura, y en la base de datos hay una sola

#### Scenario: Llamada no repetible en vuelo
- **WHEN** la conexión se corta con una llamada en vuelo que no es una lectura ni lleva clave de idempotencia
- **THEN** esa llamada falla con un error de conexión perdida y no se reenvía

#### Scenario: Backend inalcanzable
- **WHEN** se hace una llamada y el servidor no está accesible durante 30 segundos
- **THEN** la llamada falla con un error de backend no disponible

### Requirement: Suscripciones restablecidas
Tras reconectar, el cliente SHALL restablecer las suscripciones a eventos que estaban activas, y cada suscripción restablecida SHALL empezar de nuevo con su aviso de inicio, de modo que la vista sepa que puede haber perdido eventos y vuelva a cargar lo que muestra.

#### Scenario: Eventos tras una reconexión
- **WHEN** la conexión se corta, se crea una factura desde otro cliente mientras tanto y el canal se restablece
- **THEN** la suscripción vuelve a empezar con su aviso de inicio, la lista recargada contiene la factura creada durante el corte y los eventos siguientes llegan con normalidad

### Requirement: Autenticación rechazada en el cliente
Si el servidor rechaza el saludo, el cliente SHALL dar por fallidas las llamadas pendientes con el mensaje del backend y MUST NOT reintentar la conexión hasta la siguiente llamada o suscripción.

#### Scenario: Token incorrecto en el cliente
- **WHEN** el cliente está configurado con un token que el servidor no acepta y lista facturas
- **THEN** la llamada falla con el mensaje del backend y el cliente no vuelve a conectar por su cuenta
