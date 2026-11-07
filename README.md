# Vivero Sol Puntano — controlador de vivero (ESP32 + MQTT + app)

Controla temperatura/humedad, venteo (actuador lineal con finales internos, manejado por tiempo con 2 relés) y riego.
Toda la lógica corre en el ESP32; el broker MQTT (EMQX Serverless) y la app son para ver y mandar órdenes desde cualquier lado.

```
ESP32 ──TLS 8883──►  Broker EMQX (nube)  ◄──TLS──  App / MQTTX
 (sale hacia afuera)                        (sale hacia afuera)
```
Nadie necesita abrir puertos: los dos *salen* hacia el broker.

## Estructura
| Carpeta | Qué hay |
|---|---|
| `firmware/vivero/` | Código para ESP32 y ESP8266 (`vivero.ino`, `cultivos.h`, `config.h`, `ca_cert.h`, `secrets.example.h`, `prefs8266.h`) |
| `wokwi/` | Circuito para el simulador (`diagram.json`, `libraries.txt`) |
| `docs/GUIA_ARMADO.md` | Guía de armado para el equipo real |

## Topics MQTT (base `vivero/nodo1`)

**Lo que publica el ESP32**
| Topic | Retenido | Contenido |
|---|---|---|
| `vivero/nodo1/online` | sí | `online` / `offline` (este último lo publica el broker solo si el ESP32 se cae) |
| `vivero/nodo1/estado` | sí | JSON: `temp`, `hum`, `sensor_ok`, `venteo`, `riego`, `modo`, `rssi`, `uptime_s` |
| `vivero/nodo1/config` | sí | JSON con los setpoints actuales |
| `vivero/nodo1/alarma` | no | Texto con el problema (sensor caído, riego cortado por tiempo, etc.) |
| `vivero/nodo1/historial` | no | Responde a `cmd/historial`: últimas 48 h (un promedio cada 10 min) en partes de 48 puntos. JSON `p`, `de`, `n`, `dt`, `edad`, `desde`, `v` = `[[temp×10, hum×10], …]` (`null` = hueco) |

`venteo` puede ser: `cerrado`, `abierto`, `abriendo`, `cerrando`, `desconocido` (parado a mitad de camino).

**Órdenes que recibe** (`vivero/nodo1/cmd/...`)
| Topic | Mensaje | Nota |
|---|---|---|
| `cmd/modo` | `auto` / `manual` | Queda guardado |
| `cmd/venteo` | `abrir` / `cerrar` / `parar` | `abrir`/`cerrar` solo en manual. `parar` vale siempre |
| `cmd/riego` | `on` / `off` | `on` solo en manual. `off` vale siempre |
| `cmd/config/temp_abrir` | número | °C (por defecto 28) |
| `cmd/config/temp_cerrar` | número | °C (por defecto 24), tiene que ser menor que `temp_abrir` |
| `cmd/config/hum_riego_on` | número | % (por defecto 40) |
| `cmd/config/hum_riego_off` | número | % (por defecto 60) |
| `cmd/config/motor_max_s` | número | tiempo de recorrido del actuador, s (por defecto 40) |
| `cmd/config/riego_max_min` | número | min (por defecto 10) |
| `cmd/config/riego_pausa_min` | número | min (por defecto 30) |
| `cmd/estado` | cualquier cosa | Pide el estado al instante |
| `cmd/historial` | cualquier cosa | Pide el historial para el gráfico de la app |
| `cmd/cultivo/<n>/regar` | `on` / `off` (+ `;usuario`) | Cultivo n (1 a 5): `on` solo en manual, `off` siempre |
| `cmd/cultivo/<n>/config` | `n=Lechuga,m=0,on=35,off=60,l=50,t=15,a=1` (+ `;usuario`) | Ajustes del cultivo (administrador). m: 0 humedad, 1 litros, 2 lo que pase primero |
| `cmd/riegos` | cualquier cosa | Pide el registro de riegos (responde en `riegos`, en partes) |
| `cmd/registro` | cualquier cosa | Pide el registro de cambios (responde en `registro`, en partes) |
| `cmd/usuario/alta` | `nombre;rol;hash;quien` | Alta o cambio de usuario (rol `admin` u `operario`). Solo un administrador (el primero siempre es admin) |
| `cmd/usuario/baja` | `nombre;quien` | Baja de usuario (no se puede borrar al único administrador) |
| `cmd/usuario/pin` | `nombre;hash;quien` | Cambio de PIN (el administrador o el mismo usuario) |
| `cmd/config/clima_cierra` | `1` / `0` | Con alerta del SMN de viento, tormenta o granizo: cerrar el venteo (1, por defecto) o solo avisar (0) |

Los valores fuera de rango se rechazan y se avisa por `alarma`.

## Pantalla de equipos

La app abre en **Equipos**: una tarjeta por cada equipo que se conecta al broker (vivero e hibernáculos), con nombre, temperatura, humedad y estado (en línea, regando, venteo abierto, alerta, falla de sensor). Al tocar una se entra a las pantallas de ese equipo; **← Equipos** (en la PC, *Equipos* en el menú) vuelve a la grilla. Cada equipo tiene sus propios usuarios: el PIN se pide al entrar, una vez por sesión.

El **nombre** de cada equipo lo cambia un administrador en *Ajustes → Nombre del equipo* (orden `cmd/nombre`, queda guardado en la placa y se publica retenido en `nombre`). Por ahora solo el firmware del vivero lo permite; los hibernáculos usan el nombre fijo de su `config.h`.

## Modo demostración (para mostrar la app sin equipo)

En **Conexión → Probar en modo demostración** la app se conecta a un vivero simulado que vive dentro de ella (`app/demo.js`): sin broker, sin placa y sin internet. Trae el Vivero con 5 cultivos, el Hibernáculo 1, historial de 48 h, riegos, registro de cambios, clima y una alerta de ejemplo. Usuarios de prueba: **Admin (PIN 1234)** y **Operario (PIN 0000)**.
Todo lo que se toca en la demo (usuarios, PINs, ajustes, registro) queda guardado aparte (`demo.sim` y `demo.dm.*`) en ese celular o PC, así que no se mezcla con el equipo real y sigue ahí al volver a abrirla. El cartel amarillo de arriba tiene **Reiniciar** (vuelve todo a fábrica) y **Salir**.

**Para sacarlo** (cuando se venda): borrar `app/demo.js` y la línea `<script src="demo.js">` de `app/app.html`. Sin ese archivo el botón no aparece.

## Probar en el simulador (Wokwi)
1. Entrar a **wokwi.com** → *New Project* → **ESP32**.
2. Reemplazar `diagram.json` por el de `wokwi/` y crear `libraries.txt` con el de `wokwi/`.
3. Copiar el contenido de `vivero.ino` en `sketch.ino`.
4. Con el botón **+** (al lado de las pestañas) crear `config.h` (con `MODO_WOKWI 1`), `ca_cert.h` y `secrets.h` y pegar su contenido
   (`secrets.h` = copia de `secrets.example.h` con la clave real de `vivero-esp`).
5. Pegar el certificado en `ca_cert.h` (EMQX → Overview → CA Certificate).
6. Play. En el monitor serie tiene que aparecer `[MQTT] conectando al broker... OK`.
7. En MQTTX suscribirse a `vivero/#` y ver llegar el estado.

### El venteo (sin finales de carrera)
- Va **por tiempo**: el relé queda prendido el *tiempo de recorrido* (40 s por defecto) y se corta solo. En el equipo real el actuador tiene finales internos.
- Al arrancar **cierra** (LED rojo 40 s) para saber dónde está.
- Si lo parás a mitad de camino, el estado queda `desconocido`.

### Prueba del automático
1. Sensor DHT a 30 °C → LED verde, `abriendo` → a los 40 s `abierto`.
2. Sensor a 20 °C → LED rojo, `cerrando` → a los 40 s `cerrado`.
3. Entre 24 y 28 °C no hace nada (histéresis).

### Si se desconecta del broker
El monitor serie dice `[MQTT] se cayo la conexion, codigo X, duro Ys`:
`-4` = el broker no recibió el "sigo vivo" a tiempo, `-3` = se cortó la conexión de red, `-1` = desconexión normal.

## Antes de pasar al equipo real
En `config.h` cambiar una sola línea: `MODO_WOKWI 0` (pone DHT11, keepalive 30, relés activos en bajo y activa el **portal de WiFi**). En `secrets.h`: la clave del broker y `PORTAL_CLAVE`. El WiFi se carga desde el celular: ver `docs/GUIA_ARMADO.md`, punto 3 bis.

## App Android (carpeta `app/`)
Cada push que toque `app/` compila el APK y lo publica en **Releases** (etiqueta `ultima`): bajar `vivero-sol-puntano.apk` desde el celu e instalar.

La primera vez la app abre en **Conexión**: completar usuario y clave de `vivero-app` (la dirección del broker ya viene cargada). Los datos quedan guardados solo en el celular, nunca en el código.

| Pantalla | Qué hace |
|---|---|
| Inicio | Temperatura, humedad, estado del equipo/venteo/riego/sensor; Auto/Manual; Abrir, Cerrar, Parar, Regar, Cortar riego |
| Ajustes | Los 7 setpoints, con las mismas validaciones que el ESP32 |
| Alarmas | Últimas 50 alarmas con fecha y hora |
| Conexión | Broker, puerto, equipo (`nodo1`), usuario y clave |

La app se conecta por **WebSocket con TLS (puerto 8084)**.

> El hibernáculo y la oficina (LoRa) están en su propio repo: [vivero-hibernaculo-oficina](https://github.com/alexis37268-cpu/vivero-hibernaculo-oficina). La app sirve para los dos.

## Clima de afuera y avisos en el celular (REM + SMN)

La app trae un revisor que **Android ejecuta cada 15 min o más, aunque la app esté cerrada** (`app/runner-clima.js`, plugin Capacitor Background Runner):
- **REM** (Red de Estaciones Meteorológicas, Gobierno de San Luis): datos medidos de la estación **Las Chacras** (n.º 56, zona de la Escuela de Policía / Juana Koslay). Como Las Chacras no mide viento ni suelo, esos datos (y el aviso de ráfagas) salen de **San Luis Rural** (n.º 46). Pronóstico de 3 días de **La Punta** (n.º 20): las otras dos no tienen. Las alertas del SMN se filtran para el punto de la estación 56 (-33,262 / -66,243). Se muestran en Inicio, tarjeta *Afuera*.
- **SMN**: alertas oficiales que cubren la zona.
- **Notificación** en el celular con: alerta del SMN, helada pronosticada o medida (umbral en Ajustes, 2 °C por defecto) y ráfaga medida (60 km/h por defecto). Cada aviso llega una sola vez.

Al abrir la app por primera vez pide los permisos: notificaciones y que Android no la frene por batería (y en Xiaomi, Samsung, etc. hay un botón para el *inicio automático*).
La REM no tiene API: se leen sus páginas. Si cambian el sitio hay que ajustar `runner-clima.js` (`node prueba-clima/prueba.js` prueba el lector con páginas guardadas).
Los avisos del celular **no mueven el venteo**: para eso está el robot de abajo (hoy apagado).

## Alertas meteorológicas (SMN) por robot — apagado

Un robot en GitHub Actions (`robot/alertas_smn.py`, workflow **Alertas SMN**) corre cada 15 min:
lee los avisos oficiales del SMN (feed CAP, datos abiertos CC BY 4.0), se queda con los que cubren la
ubicación del vivero y publica en el broker, retenidos:

| Topic | Contenido | Lo usa |
|---|---|---|
| `vivero/clima/alerta` | JSON con las alertas vigentes (evento, nivel amarillo/naranja/rojo, desde, hasta, texto) | la app: cartel en Inicio y lista de Alarmas |
| `vivero/clima/cierre` | hora (epoch) hasta la que hay que cerrar el venteo, `0` = no | el ESP: en automático cierra y no abre hasta esa hora |

Cierran el venteo: tormentas, viento, Zonda, granizo y nevadas, desde 30 min antes de que empiecen.

**Para que publique** (sin esto corre igual, pero solo muestra el resultado en el registro):
1. En EMQX, crear un usuario `vivero-robot` con su clave.
2. En GitHub → Settings → Secrets and variables → Actions:
   - *Secrets*: `MQTT_USER` = `vivero-robot`, `MQTT_PASS` = la clave.
   - *Variables*: `VIVERO_LAT` y `VIVERO_LON` = coordenadas del vivero (por defecto: ciudad de San Luis).

Prueba con avisos reales guardados: `python3 robot/prueba_alertas.py`.
GitHub pausa los workflows programados de un repo público después de 60 días sin cambios: si pasa, se reactiva desde la pestaña Actions.

## Usuarios y registro de cambios

- Las órdenes van **firmadas**: `abrir;Juan` (valor`;`usuario). Sin `;` el equipo la toma como "sin usuario" (así siguen andando MQTTX y las apps viejas).
- La lista de usuarios vive en el ESP y se publica retenida en `vivero/nodo1/usuarios` (`[{"n":"Juan","r":"admin","h":"…"}]`).
  Del PIN solo se guarda `h` = primeros 16 caracteres hex de SHA-256 de `nodo1:nombre-en-minúsculas:PIN`.
- **Operario**: modo, venteo y riego. **Administrador**: además ajustes y usuarios (lo controla la app y también el ESP).
- **Registro**: el ESP anota `fecha|quién|qué` (incluye `panel` = pulsadores, `automatico` = lógica sola, `equipo` = encendido) en la flash, ~300 líneas (2 archivos de 150). Cada acción nueva sale al instante por `registro/nuevo`.
- **Olvido del PIN del administrador**: en el tablero, RIEGO + VENTEO apretados juntos 10 s borran la lista de usuarios (ajustes y registro no se tocan) y la app vuelve a pedir crear el administrador. Solo se puede hacer estando frente al equipo.
- Seguridad: sirve para uso interno. Quien tenga la clave del broker puede mandar órdenes a mano con cualquier nombre.

## Versión para PC (página web, solo administradores)

La misma app publicada en GitHub Pages: **https://alexis37268-cpu.github.io/vivero--movil/** (workflow *Pagina PC*).
- Se abre en Chrome o Edge; con **Instalar app** queda como programa en el escritorio. Se actualiza sola.
- En pantallas anchas: barra lateral, resumen de todos los equipos, gráfico grande, registro con filtros.
- **Solo entran administradores** (los operarios usan la app del celular).
- La pestaña Clima usa el archivo del **robot del clima** (workflow *Clima (robot)*, cada 15 min, rama `datos`), porque el navegador no deja leer la REM directo. Si el robot se para, la página avisa "Datos del clima de hace X".
- Activarla una sola vez: Settings → Pages → Source: **GitHub Actions**.

## Riego por cultivos (`firmware/vivero/cultivos.h`)

5 cultivos (configurable, solo ESP32), cada uno con su válvula y su sensor de suelo; **de a uno por vez** con **un solo caudalímetro**; bomba o válvula general en el relé de RIEGO.
Corte por humedad, litros o lo que pase primero; tiempo máximo; **marcha en seco** (sin caudal 10 s = corta); aviso de pérdida.
Topics: `cultivos` (estado: humedad, litros hoy/mes/total, quién riega, caudal), `cultivos/config` (ajustes), `riegos` y `riegos/nuevo` (`inicio|fin|cultivo|litros|segundos|quien|motivo`).
`DEMO_CULTIVOS 1` simula suelo y caudal para mostrarlo sin sensores. Pines y calibración: `docs/GUIA_ARMADO.md`, punto 7.

**Exportar a Excel**: la app arma un `.xlsx` con las hojas Temp y humedad (con el suelo de cada cultivo), Riegos (con litros), Litros por cultivo, Cambios y Clima.
