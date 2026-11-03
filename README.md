# Vivero Sol Puntano — controlador de vivero (ESP32 + MQTT + app)

Controla temperatura/humedad, venteo (motor DC con 2 relés + finales de carrera) y riego.
Toda la lógica corre en el ESP32; el broker MQTT (EMQX Serverless) y la app son para ver y mandar órdenes desde cualquier lado.

```
ESP32 ──TLS 8883──►  Broker EMQX (nube)  ◄──TLS──  App / MQTTX
 (sale hacia afuera)                        (sale hacia afuera)
```
Nadie necesita abrir puertos: los dos *salen* hacia el broker.

## Estructura
| Carpeta | Qué hay |
|---|---|
| `firmware/vivero/` | Código para ESP32 y ESP8266 (`vivero.ino`, `config.h`, `ca_cert.h`, `secrets.example.h`, `prefs8266.h`) |
| `wokwi/` | Circuito para el simulador (`diagram.json`, `libraries.txt`) |
| `docs/GUIA_ARMADO.md` | Guía de armado para el equipo real |
| `firmware/hibernaculo/` | Hibernáculo: ESP32 con LoRa, pantalla, palanca, pulsadores y LEDs (sin WiFi) |
| `firmware/oficina/` | Oficina: NodeMCU 8266 que recibe los hibernáculos por LoRa y los sube al broker |
| `docs/GUIA_HIBERNACULO.md` | Guía de armado del hibernáculo y de la oficina |

## Topics MQTT (base `vivero/nodo1`)

**Lo que publica el ESP32**
| Topic | Retenido | Contenido |
|---|---|---|
| `vivero/nodo1/online` | sí | `online` / `offline` (este último lo publica el broker solo si el ESP32 se cae) |
| `vivero/nodo1/estado` | sí | JSON: `temp`, `hum`, `sensor_ok`, `venteo`, `riego`, `modo`, `rssi`, `uptime_s` |
| `vivero/nodo1/config` | sí | JSON con los setpoints actuales |
| `vivero/nodo1/alarma` | no | Texto con el problema (sensor caído, motor sin llegar, etc.) |

`venteo` puede ser: `cerrado`, `abierto`, `abriendo`, `cerrando`, `desconocido`, `error`.

**Órdenes que recibe** (`vivero/nodo1/cmd/...`)
| Topic | Mensaje | Nota |
|---|---|---|
| `cmd/modo` | `auto` / `manual` | Queda guardado |
| `cmd/venteo` | `abrir` / `cerrar` / `parar` | `abrir`/`cerrar` solo en manual (y sacan del `error`). `parar` vale siempre |
| `cmd/riego` | `on` / `off` | `on` solo en manual. `off` vale siempre |
| `cmd/config/temp_abrir` | número | °C (por defecto 28) |
| `cmd/config/temp_cerrar` | número | °C (por defecto 24), tiene que ser menor que `temp_abrir` |
| `cmd/config/hum_riego_on` | número | % (por defecto 40) |
| `cmd/config/hum_riego_off` | número | % (por defecto 60) |
| `cmd/config/motor_max_s` | número | s (por defecto 60) |
| `cmd/config/riego_max_min` | número | min (por defecto 10) |
| `cmd/config/riego_pausa_min` | número | min (por defecto 30) |
| `cmd/estado` | cualquier cosa | Pide el estado al instante |

Los valores fuera de rango se rechazan y se avisa por `alarma`.

## Probar en el simulador (Wokwi)
1. Entrar a **wokwi.com** → *New Project* → **ESP32**.
2. Reemplazar `diagram.json` por el de `wokwi/` y crear `libraries.txt` con el de `wokwi/`.
3. Copiar el contenido de `vivero.ino` en `sketch.ino`.
4. Con el botón **+** (al lado de las pestañas) crear `config.h` (con `MODO_WOKWI 1`), `ca_cert.h` y `secrets.h` y pegar su contenido
   (`secrets.h` = copia de `secrets.example.h` con la clave real de `vivero-esp`).
5. Pegar el certificado en `ca_cert.h` (EMQX → Overview → CA Certificate).
6. Play. En el monitor serie tiene que aparecer `[MQTT] conectando al broker... OK`.
7. En MQTTX suscribirse a `vivero/#` y ver llegar el estado.

### Los switches (finales de carrera simulados)
- **Palanquita a la IZQUIERDA = apretado** (el venteo llegó ahí). **A la DERECHA = suelto.**
- Arranca así: arriba (FIN ABIERTO) a la derecha, abajo (FIN CERRADO) a la izquierda → venteo **cerrado**.
- Nunca los dos a la izquierda: el ESP32 lo toma como falla y pasa a `error`.
- En la simulación **vos sos el mecanismo**: cuando se prende un LED, movés los switches como se movería el venteo:
  - **Abriendo (LED verde):** primero abajo → derecha, después arriba → izquierda.
  - **Cerrando (LED rojo):** primero arriba → derecha, después abajo → izquierda.
  - Si no lo hacés en 60 s, salta la protección de tiempo y queda en `error` (se sale con `manual` + `abrir`/`cerrar`).

### Prueba del automático
1. Sensor DHT a 30 °C → LED verde, `abriendo` → mover switches → `abierto`.
2. Sensor a 20 °C → LED rojo, `cerrando` → mover switches → `cerrado`.
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
