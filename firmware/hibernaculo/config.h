// =============================================================
//  config.h  -  Hibernaculo (ESP32). Lo que se ajusta por instalacion.
// =============================================================
#pragma once

// ---------- Identidad ----------
// Cada hibernaculo tiene su numero. El vivero es el nodo 1, asi que
// el primer hibernaculo es el 2, el siguiente el 3, etc.
#define NODO_NUM     2
#define NODO_NOMBRE  "Hibernaculo 1"         // lo que se ve en la app y en la oficina

// ---------- Pines ----------
#define PIN_DHT            4      // DHT22
#define PIN_VENTEO_ABRIR   25     // rele
#define PIN_VENTEO_CERRAR  26     // rele
#define PIN_RIEGO          27     // rele
// LCD 20x4 I2C: SDA 21, SCL 22 (los del ESP32 por defecto)
#define LCD_DIRECCION      0x27   // si la pantalla no muestra nada, probar 0x3F
// LoRa Ra-02 (SPI): SCK 18, MISO 19, MOSI 23 (los del ESP32 por defecto)
#define PIN_LORA_NSS       5
#define PIN_LORA_RST       -1     // sin conectar (si se conecta, poner el pin)
// DIO0 del Ra-02 al 35: no se usa (se consulta la radio), pero queda cableado

// Mandos (todos a GND, sin resistencias: pull-up interna)
#define PIN_PALANCA_MANUAL 16     // palanca sin retencion, lado MANUAL
#define PIN_PALANCA_AUTO   15     // palanca sin retencion, lado AUTOMATICO
#define PIN_BOTON_RIEGO    17     // pulsador
#define PIN_BOTON_VENTEO   32     // pulsador

// Indicadores (LED + resistencia de 220-330 ohm a GND)
#define PIN_LED_AUTO       33
#define PIN_LED_MANUAL     13
#define PIN_LED_RIEGO      2      // tambien prende el LED azul de la placa
#define PIN_LED_VENTEO     14     // fijo = abierto, titila = moviendose, rapido = error

// Modulo de reles chino comun: se activa con LOW -> false. Si queda al reves, true.
#define RELE_ACTIVO_ALTO   false

// ---------- Tiempos ----------
#define ANTIRREBOTE_MS           50UL
#define TIEMPO_MUERTO_MS         300UL     // actuador parado antes de invertir el giro
#define INTERVALO_LECTURA_MS     5000UL    // DHT22: no leer mas seguido que cada 2 s
#define INTERVALO_ESTADO_MS      20000UL   // cada cuanto manda el estado por LoRa
#define INTERVALO_NOMBRE_MS      600000UL  // cada cuanto repite nombre y config (10 min)
#define FALLOS_SENSOR_MAX        3

// ---------- Valores por defecto (se cambian desde la app y quedan guardados) ----------
#define DEF_TEMP_ABRIR      28.0
#define DEF_TEMP_CERRAR     24.0
#define DEF_HUM_RIEGO_ON    40.0
#define DEF_HUM_RIEGO_OFF   60.0
#define DEF_RECORRIDO_S     40     // s que tarda el actuador de punta a punta (medirlo y sumar un 20 %)
#define DEF_RIEGO_MAX_MIN   10
#define DEF_RIEGO_PAUSA_MIN 30
