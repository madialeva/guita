# suite-de-pruebas Specification

## Purpose

Batería de pruebas automáticas de Guita: se ejecuta con una sola orden y sin pantalla, verifica el backend, el API y el cliente contra un PostgreSQL desechable y nunca toca la base de datos de desarrollo, de modo que sirve igual en local y en un CI.

## Requirements

### Requirement: Ejecución con una sola orden y sin pantalla
El proyecto SHALL ofrecer una orden única que compile y ejecute todas las pruebas y SHALL informar del resultado de cada suite. Las pruebas MUST ejecutarse sin servidor gráfico.

#### Scenario: Ejecución completa
- **WHEN** el desarrollador ejecuta la batería en una máquina con los requisitos de compilación y Docker disponible
- **THEN** se ejecutan todas las suites y el resultado indica cuáles pasan y cuáles fallan

#### Scenario: Sin servidor gráfico
- **WHEN** la batería se ejecuta en un entorno sin pantalla (por ejemplo, un CI)
- **THEN** las pruebas del cliente y de QML se ejecutan igualmente, sin depender de un servidor gráfico

### Requirement: Base de datos de pruebas aislada
Las pruebas que necesitan PostgreSQL SHALL usar un servidor efímero creado para la ejecución o el servidor indicado explícitamente en `GUITA_TEST_DB`, y MUST NOT usar la base de datos de desarrollo ni la configuración por defecto de la aplicación. Cada suite SHALL trabajar en una base de datos propia que se elimina al terminar, de forma que las suites puedan ejecutarse en paralelo.

#### Scenario: Servidor efímero
- **WHEN** `GUITA_TEST_DB` no está definida y Docker está disponible
- **THEN** la ejecución levanta un PostgreSQL desechable, lo usa y lo destruye al terminar, también cuando alguna prueba falla

#### Scenario: Servidor indicado
- **WHEN** `GUITA_TEST_DB` contiene una cadena de conexión
- **THEN** las pruebas usan ese servidor y no levantan ningún otro

#### Scenario: Base de datos de desarrollo intacta
- **WHEN** termina la ejecución
- **THEN** la base de datos `guita` de desarrollo no ha recibido ninguna conexión ni cambio, y no queda ninguna base de datos de prueba en el servidor usado

#### Scenario: Ejecución en paralelo
- **WHEN** las suites se ejecutan en paralelo
- **THEN** ninguna interfiere con otra y el resultado es el mismo que en serie

### Requirement: Fallo explícito sin base de datos
Si no hay `GUITA_TEST_DB` ni Docker disponible, las pruebas que necesitan PostgreSQL MUST fallar con un mensaje que explique cómo proporcionar la base de datos; MUST NOT omitirse en silencio. Las pruebas que no necesitan base de datos SHALL ejecutarse igualmente.

#### Scenario: Sin Docker ni variable
- **WHEN** no hay Docker disponible y `GUITA_TEST_DB` no está definida
- **THEN** las pruebas con base de datos fallan indicando que hace falta Docker o `GUITA_TEST_DB`, y las demás se ejecutan

### Requirement: Selección por categorías
Cada prueba SHALL pertenecer a una categoría según lo que necesita (sin base de datos, con base de datos, API, cliente, QML), y la batería SHALL permitir ejecutar o excluir categorías.

#### Scenario: Solo pruebas rápidas
- **WHEN** el desarrollador ejecuta solo la categoría de pruebas sin base de datos
- **THEN** se ejecutan únicamente esas pruebas, sin necesitar Docker ni `GUITA_TEST_DB`

### Requirement: Cobertura de las pruebas
La batería SHALL verificar el comportamiento que hoy ofrece el proyecto:
- las validaciones de negocio y la persistencia exacta de importes;
- las migraciones;
- el contrato RPC del API en su codificación binaria, con bytes de referencia del contrato;
- la autorización por token, por HTTP y en el primer mensaje del canal;
- el canal persistente: sobre, correlación, llamadas concurrentes, suscripciones y límites;
- los eventos del servidor, sea cual sea el transporte que produce la creación y el que entrega el evento;
- la idempotencia de las operaciones que crean, también con peticiones simultáneas;
- el acceso del cliente al API por sus dos transportes (canal y en proceso), incluidos los eventos;
- la reconexión del cliente: reenvío seguro, llamadas no repetibles fallidas y suscripciones restablecidas, con cortes de conexión provocados por las pruebas;
- el análisis y formato de importes en la interfaz.

#### Scenario: Importes exactos
- **WHEN** se guardan y leen facturas con importes de 0,05, 1234,50 y el máximo admitido (9 999 999 999,99)
- **THEN** los importes leídos coinciden exactamente en céntimos con los guardados

#### Scenario: Contrato del API
- **WHEN** se llama a los procedimientos del API con peticiones válidas, sin token, con mensajes mal formados, con un cuerpo JSON u otro tipo de contenido no admitido, con un método HTTP distinto de POST, a un procedimiento inexistente o a un procedimiento de eventos
- **THEN** las respuestas tienen los estados y los errores documentados (200, 401, 400, 415, 405, 404) con sus códigos de error

#### Scenario: Bytes de referencia del contrato
- **WHEN** se codifican mensajes fijos del contrato, con campos opcionales ausentes y presentes y enteros de 64 bits en sus límites, y se decodifican los bytes de referencia, en nativo y en WebAssembly
- **THEN** los bytes coinciden con los de referencia guardados en las pruebas y el contenido decodificado con el original

#### Scenario: Canal persistente
- **WHEN** se usa el canal con y sin saludo, con tokens válidos e incorrectos, con frames ilegibles, con llamadas concurrentes, con suscripciones canceladas, por encima del límite de canales y con un cliente que no lee
- **THEN** el servidor se comporta como describe la capacidad `canal-websocket`

#### Scenario: Idempotencia
- **WHEN** se repite una creación con la misma clave, en serie y a la vez, con los mismos datos y con otros
- **THEN** se crea una sola factura y todas las respuestas válidas la devuelven, y la clave con otros datos se rechaza

#### Scenario: Transportes del cliente
- **WHEN** el cliente crea y lista facturas, recibe un error de validación y recibe los eventos de facturas creadas por otro cliente, por el canal y en proceso
- **THEN** los resultados y los eventos son los mismos con los dos transportes

#### Scenario: Reconexión del cliente
- **WHEN** las pruebas cortan la conexión entre el cliente y el servidor con una creación con clave en vuelo, con una llamada no repetible en vuelo y con una suscripción activa
- **THEN** la creación se completa una sola vez, la llamada no repetible falla con un error de conexión perdida y la suscripción se restablece con su aviso de inicio

#### Scenario: Sentencia incompatible con el esquema
- **WHEN** el backend arranca con una sentencia preparada que no casa con el esquema
- **THEN** el arranque falla con un error, antes de atender ninguna petición

#### Scenario: Importes en la interfaz
- **WHEN** se analizan importes escritos como `1234,5`, `1234.50`, `0`, `1.234,50` o `-1`
- **THEN** los tres primeros dan 123450, 123450 y 0 céntimos, y los dos últimos se rechazan
