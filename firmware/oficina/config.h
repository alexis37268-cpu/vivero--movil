// =============================================================
//  config.h  -  Estacion de oficina (ESP8266, tambien compila en ESP32)
//  Recibe por LoRa a los hibernaculos y los sube al broker MQTT.
// =============================================================
#pragma once

// ---------- Broker MQTT ----------
#define MQTT_HOST        "p64138d1.ala.us-east-1.emqxsl.com"
#define MQTT_PORT        8883
#define MQTT_KEEPALIVE_S 30
#define REINTENTO_CONEXION_MS 10000UL

// ---------- Portal de WiFi ----------
#define PORTAL_NOMBRE      "Vivero-oficina"
#define PORTAL_TIMEOUT_S   300
#define PORTAL_REABRIR_MIN 10
#define BOTON_PORTAL_MS    5000     // boton FLASH mantenido 5 s = portal

// ---------- Pines ----------
#if defined(ESP8266)
  // LoRa Ra-02 por SPI: SCK D5, MISO D6, MOSI D7 (los del 8266 por defecto)
  #define PIN_LORA_NSS   15         // D8
  #define PIN_LORA_RST   -1         // sin conectar
  // LCD 16x2 I2C: SDA D2 (GPIO 4), SCL D1 (GPIO 5) (los del 8266 por defecto)
  #define PIN_LED_ERROR  16         // D0 -> LED + resistencia a GND
  #define PIN_BOTON      0          // boton FLASH de la placa
#else
  // Por si la oficina se pasa a un ESP32: SPI 18/19/23, I2C 21/22
  #define PIN_LORA_NSS   5
  #define PIN_LORA_RST   -1
  #define PIN_LED_ERROR  13
  #define PIN_BOTON      0          // boton BOOT
#endif
#define LCD_DIRECCION    0x27       // si no se ve nada, probar 0x3F

// ---------- Estaciones ----------
#define MAX_ESTACIONES       8
#define SIN_SENAL_MS         60000UL   // sin noticias de una estacion en 1 min = "sin senal"
#define ORDEN_ESPERA_MS      2500UL    // espera de la confirmacion de una orden
#define ORDEN_REINTENTOS     3
#define ANTIRREBOTE_MS       50UL
#define PAGINA_AUTO_MS       8000UL    // la pantalla pasa sola de estacion (0 = solo con el boton)
