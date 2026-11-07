# Guía de armado — Controlador de vivero (nodo1)

Para Matías. El código ya está probado en simulación; esta guía es para armarlo en la placa real.
Se arma en **dos etapas**: primero con LEDs/relés sin carga (no se rompe nada si algo está mal), después con motor y válvula.

---

## 1. Materiales

**Etapa 1 (banco de prueba)**
- ESP32-WROOM-32U en placa de expansión (la que tenés) + **antena 2,4 GHz con conector u.FL** (sin antena casi no hay WiFi)
- Sensor DHT11 (módulo de 3 pines)
- Módulo de 3 relés (o 3 módulos de 1) de 5 V con optoacoplador, que acepte disparo de 3,3 V
- Cables dupont, fuente para la placa (6,5–16 V por el jack, o USB)

**Etapa 2 (con carga)**
- Fuente 12 V con corriente suficiente para el motor (mirar la chapa del motor, sobredimensionar ×1,5)
- **Actuador lineal 12 V con finales de carrera internos** para el venteo (sin finales externos: el ESP32 lo maneja por tiempo)
- Electroválvula o bomba de riego (anotar tensión: 12 V DC, 24 VAC o 220 V)
- Fusible en la línea del motor, diodo/varistor según la carga, gabinete estanco

---

## 2. Conexiones (etapa 1)

![Diagrama de conexiones](conexiones.png)

| Elemento | Pin del ESP32 | Notas |
|---|---|---|
| DHT11 `+` | 3V3 | Jumper amarillo de la placa en **3,3 V** |
| DHT11 `out` | **GPIO 4** | |
| DHT11 `−` | GND | |
| Relé 1 — **ABRIR venteo** | **GPIO 25** | |
| Relé 2 — **CERRAR venteo** | **GPIO 26** | |
| Relé 3 — **RIEGO** | **GPIO 27** | |
| Módulo relés VCC / GND | 5V / GND | |
| Pulsador **MODO** | **GPIO 16** ↔ GND | Pulsador NA, sin resistencia |
| Pulsador **RIEGO** | **GPIO 17** ↔ GND | Ídem |
| Pulsador **VENTEO** | **GPIO 33** ↔ GND | Ídem |

**No usar** (quedan reservados): GPIO 32 a 39 (futuros sensores de humedad de suelo) y GPIO 5, 18, 19, 23 (futuro LoRa).

---

## 2 bis. Si probás con el NodeMCU ESP8266

El mismo código anda en el ESP8266: los pines se eligen solos según la placa.
Conexiones:

![Conexiones con NodeMCU ESP8266](conexiones-esp8266.png)

| Función | NodeMCU |
|---|---|
| DHT11 out | D5 |
| Relé ABRIR / CERRAR / RIEGO | D1 / D2 / D0 |
| Pulsador RIEGO / VENTEO | D4 / RX |
| Pulsador MODO | el botón FLASH de la placa |
| Relés VCC | VIN (5 V por USB) |

No usar D3, D4 ni D8 (con algo conectado, a veces la placa no arranca).
El botón **FLASH** hace dos cosas: **toque corto = cambia el modo**, **mantenerlo 5 s = abre el portal**.
**No arrancar la placa con un pulsador apretado** (D4 y FLASH deciden el arranque).

Limitaciones (por eso el equipo final va con ESP32): una sola entrada analógica, sin pines para LoRa y poca memoria para TLS.

---

## 3. Cargar el programa

1. Instalar **Arduino IDE 2**.
2. *Archivo → Preferencias → URLs adicionales*:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
   Para el ESP8266 agregar también (separada por coma): `https://arduino.esp8266.com/stable/package_esp8266com_index.json`
3. *Herramientas → Placa → Gestor de placas*: instalar **esp32** (de Espressif) y/o **esp8266**.
4. *Gestor de librerías*: instalar **PubSubClient** (Nick O'Leary), **DHT sensor library** (Adafruit, acepta instalar Adafruit Unified Sensor) y **WiFiManager** (tzapu).
5. Abrir la carpeta `firmware/vivero/`.
6. Copiar `secrets.example.h` como **`secrets.h`** y completar:
   - `MQTT_PASS`: la clave del usuario `vivero-esp` (te la paso por privado, no por grupo).
   - `PORTAL_CLAVE`: dejarla vacía (`""`) para que la red del portal sea **abierta**, o poner una clave de 8 caracteres o más.
   - El WiFi **no** se pone acá: se carga desde el celular (punto 3 bis).
7. En `config.h` cambiar **una sola línea**: `#define MODO_WOKWI 0`
   (eso solo pone DHT11, keepalive 30, relés activos en bajo y activa el portal de WiFi).
   Si los relés quedan **al revés** (prendidos cuando deberían estar apagados), en `config.h` cambiar el `RELE_ACTIVO_ALTO false` por `true`.
8. Placa: **ESP32 Dev Module** (o **NodeMCU 1.0 (ESP-12E Module)** si es el 8266). Puerto: el COM que aparezca. Subir.
   Si la PC no ve la placa: falta el driver USB (**CH340** o **CP2102**, según el chip que tenga al lado del USB).
9. Abrir el **Monitor serie a 115200**. La primera vez tiene que verse:
   ```
   === Controlador de vivero nodo1 ===
   [PORTAL] sin WiFi conocido: red "Vivero-nodo1" abierta. Conectate con el celular.
   ```

## 3 bis. Cargar el WiFi con el celular (portal)

1. En el celular, ir a WiFi y conectarse a la red **Vivero-nodo1** (abierta, o con la `PORTAL_CLAVE` si le pusiste una).
2. Se abre sola una página (si no, entrar a **192.168.4.1** en el navegador).
3. Tocar **Configure WiFi**, elegir el WiFi del lugar y poner su clave.
   Abajo están los datos del broker: ya vienen cargados, **dejarlos como están** (la clave MQTT vacía = no se cambia).
4. **Save**. El ESP32 se conecta y en el monitor serie aparece:
   ```
   [WIFI] conectado, IP 192.168.x.x
   [MQTT] conectando al broker... OK
   ```
   Si dice `fallo, codigo 4` → usuario o clave del broker mal. `codigo -2` → certificado, antena o internet.

**Queda guardado**: después de un corte de luz se conecta solo. Para cambiar de WiFi (otro router, otro lugar):
- **Mantener apretado el botón BOOT de la placa 5 segundos** → vuelve a abrir el portal.
- O solo: si pasa **10 minutos sin WiFi**, abre el portal por su cuenta.
- El portal se cierra solo a los 5 minutos si nadie entra.

Mientras el portal está abierto, **la lógica sigue funcionando** (venteo y riego en automático).
Única excepción: cuando tocás Save, el ESP32 tarda hasta 10 s probando el WiFi nuevo y en ese rato no atiende el venteo.
No pasa nada: el actuador se corta solo con sus finales internos aunque el ESP32 esté ocupado.

---

## 4. Pruebas de la etapa 1 (en este orden)

Usar **MQTTX** conectado al broker y suscripto a `vivero/#`.

1. **Sensor**: cada 30 s llega `vivero/nodo1/estado` con `temp` y `hum`. Soplar o calentar el DHT y ver que cambie.
2. **Pasar a manual**: publicar `manual` en `vivero/nodo1/cmd/modo`.
3. **Riego**: `on` en `cmd/riego` → hace clic el relé 3. `off` → se suelta.
4. **Arranque**: al prender, el relé 2 (CERRAR) queda prendido el tiempo de recorrido (40 s por defecto) → estado `cerrado`. Así sabe dónde está el venteo.
5. **Venteo abrir**: `abrir` en `cmd/venteo` → relé 1 prendido 40 s → se suelta solo, estado `abierto`.
6. **Venteo cerrar**: `cerrar` → relé 2 prendido 40 s → estado `cerrado`. Un `parar` a mitad de camino deja el estado `desconocido`.
7. **Automático**: `auto` en `cmd/modo`. Calentar el DHT por encima de 28 °C → abre. Bajar de 24 °C → cierra.
8. **Corte de luz**: desenchufar y enchufar → tiene que volver en el mismo modo y con los mismos setpoints.
9. **Sin internet**: apagar el router → la lógica automática sigue funcionando (mirar los relés), y al volver se reconecta solo.
10. **Tablero (pulsadores)**, con o sin internet:
    - **MODO**: cada toque alterna automático / manual (se ve en la app y en el monitor serie: `[MODO] manual (pulsador)`).
    - **RIEGO** (en manual): un toque prende, otro apaga.
    - **VENTEO** (en manual): cerrado → abre; abierto → cierra; **moviéndose → para**.
    - En automático, RIEGO y VENTEO no hacen nada y llega una alarma "pulsador ignorado".
    - Tocar rápido varias veces: cada toque tiene que contar **una sola vez** (antirrebote por software de 50 ms, sin `delay`).
    - **RIEGO + VENTEO apretados juntos 10 segundos** (mejor en automático): borra todos los usuarios. Sirve si nadie recuerda el PIN del administrador; después la app pide crear el administrador de nuevo. Queda anotado en el registro.

Si todo esto anda, recién ahí etapa 2.

---

## 5. Etapa 2 — potencia y seguridad (importante)

**Puente con 2 relés para el actuador** (relés inversores, con COM / NA / NC):
```
            +12V                     +12V
              │                        │
   Relé ABRIR: NA            Relé CERRAR: NA
              COM ─── MOTOR ─── COM
   Relé ABRIR: NC            Relé CERRAR: NC
              │                        │
             GND                      GND
```
- Con los dos relés sueltos, los dos bornes del motor quedan a GND → motor parado (y frenado).
- Si por un error se activan los dos a la vez, los dos bornes quedan a +12 V → **no hay cortocircuito**. Por eso este armado es seguro.
- Los **finales internos del actuador** lo cortan al llegar a cada punta, **aunque el ESP32 se cuelgue**. Por eso no hacen falta finales externos.
  Ojo: tiene que ser un actuador que los traiga. Un motor común sin finales, manejado por tiempo, fuerza contra el tope.
- **Medir el recorrido**: cronometrar cuánto tarda de punta a punta y cargar ese tiempo + 20 % en la app (Ajustes → *Tiempo de recorrido del actuador*).
- Verificar que la corriente del actuador (sobre todo la de arranque) no supere la de los contactos del relé. Si la supera: contactor o driver (BTS7960).
- Fusible en la línea de +12 V del motor.

**Riego**
- Electroválvula de 12 V DC: diodo en antiparalelo sobre la bobina.
- 24 VAC o 220 V: relé con contactos adecuados y varistor/snubber sobre la bobina. 220 V siempre en gabinete y con térmica.

**Ruido**: separar los cables de potencia de los de señal (sensor y pulsadores). Si los pulsadores quedan a más de 2–3 m, conviene un capacitor de 100 nF entre el pin y GND.

---

## 6. Valores que se pueden cambiar sin reprogramar

Publicando en `vivero/nodo1/cmd/config/<clave>` (quedan guardados en el ESP32):

| Clave | Por defecto | Qué hace |
|---|---|---|
| `temp_abrir` | 28 °C | Abre el venteo |
| `temp_cerrar` | 24 °C | Cierra el venteo |
| `hum_riego_on` | 40 % | Empieza a regar |
| `hum_riego_off` | 60 % | Deja de regar |
| `motor_max_s` | 40 s | Tiempo de recorrido del actuador — **medir el recorrido real y poner un 20 % más** |
| `riego_max_min` | 10 min | Corte de seguridad del riego |
| `riego_pausa_min` | 30 min | Pausa mínima entre riegos automáticos |

> **Ojo:** por ahora el riego automático usa la **humedad del aire** (DHT11). Es provisorio: para regar bien hace falta el sensor capacitivo de humedad de suelo (va en GPIO 32–39).

---

## 7. Riego por cultivos (5 sectores, de a uno, con un caudalímetro)

Archivo nuevo: **`cultivos.h`** (va en la misma carpeta que `vivero.ino`; en el Arduino IDE aparece como otra pestaña).
Solo en **ESP32** (el 8266 tiene una sola entrada analógica: ahí los cultivos se desactivan solos).

| Cultivo | Válvula (relé) | Sensor de suelo capacitivo |
|---|---|---|
| 1 | GPIO 13 | GPIO 32 |
| 2 | GPIO 14 | GPIO 34 |
| 3 | GPIO 18 | GPIO 35 |
| 4 | GPIO 19 | GPIO 36 (VP) |
| 5 | GPIO 23 | GPIO 39 (VN) |
| Caudalímetro (señal) | GPIO 22 | ⚠ si se alimenta con 5 V, bajar la señal a 3,3 V (divisor 10 kΩ / 20 kΩ) |
| Bomba o válvula general | el relé de RIEGO (GPIO 27) | se prende con cualquier cultivo (`USAR_BOMBA_GENERAL 1`) |

**Cómo funciona**
- Se riega **de a un cultivo por vez**; los litros del caudalímetro se le cuentan al que tiene la válvula abierta.
- En automático riega primero al **más seco** que esté por debajo de su "riega debajo de".
- Corta por **humedad**, por **litros** o por **lo que pase primero** (se elige por cultivo en la app). Siempre corta por **tiempo máximo**.
- A mano (modo manual) no corta por humedad: corta por litros, tiempo máximo o "Cortar".
- **Marcha en seco**: si la bomba anda y el caudalímetro no cuenta agua en **10 s** (`SIN_CAUDAL_S`), corta todo y avisa.
- **Pérdida**: si pasa agua con todas las válvulas cerradas, avisa.
- Litros de hoy, del mes y total por cultivo (se guardan aunque se corte la luz).

**Modo demostración** (`DEMO_CULTIVOS 1` en `config.h`, viene así): simula los sensores de suelo y el caudal para mostrarlo sin instalar nada. Para el equipo real: `DEMO_CULTIVOS 0`.

**Calibrar los sensores de suelo** (equipo real): mirar en el monitor serie la lectura con el sensor **al aire** (`SUELO_SECO_RAW`) y **en un vaso de agua** (`SUELO_MOJADO_RAW`) y poner esos números en `config.h`.
