// =============================================================
//  config.h  -  Todo lo que se ajusta por instalacion
//  (pines, tipo de sensor, valores por defecto, broker)
//  Las claves NO van aca: van en secrets.h
// =============================================================
#pragma once

// ---------- Identidad del equipo ----------
// Cada controlador tiene su ID. Si manana hay otro vivero: "nodo2".
#define NODO_ID     "nodo1"
#define TOPIC_BASE  "vivero/" NODO_ID      // => vivero/nodo1/...

// ---------- Broker MQTT (EMQX Serverless) ----------
#define MQTT_HOST   "p64138d1.ala.us-east-1.emqxsl.com"
#define MQTT_PORT   8883                    // MQTT sobre TLS
// Keepalive: cada cuanto el ESP32 le avisa al broker "sigo vivo".
// Si el broker no recibe nada en 1,5 x este tiempo, corta la conexion.
//   Equipo real: 30 s.
//   Wokwi: 120 s (el simulador va mas lento que el reloj real y no llega a avisar a tiempo).
#define MQTT_KEEPALIVE_S  120

// ---------- Sensor de temperatura y humedad ----------
#define PIN_DHT     4
// Wokwi solo trae DHT22. En el equipo real de Matias va DHT11.
#define DHT_TIPO    DHT22                   // cambiar a DHT11 en el real

// ---------- Salidas (reles) ----------
#define PIN_VENTEO_ABRIR   25
#define PIN_VENTEO_CERRAR  26
#define PIN_RIEGO          27
// Los LEDs de Wokwi se prenden con HIGH  -> true
// Los modulos de rele chinos comunes se activan con LOW -> false
#define RELE_ACTIVO_ALTO   true

// ---------- Entradas (finales de carrera del venteo) ----------
// Contacto a GND cuando esta pisado (se usa pull-up interno).
#define PIN_FIN_ABIERTO    13
#define PIN_FIN_CERRADO    14

// Pines reservados para el futuro (NO usar):
//   32-39 -> sensores de humedad de suelo (ADC1, andan con WiFi)
//   5, 18, 19, 23 -> modulo LoRa (SPI)

// ---------- Tiempos ----------
#define INTERVALO_LECTURA_MS     5000UL    // cada cuanto lee el sensor
#define INTERVALO_PUBLICACION_MS 30000UL   // cada cuanto publica (cuida la cuota gratis)
#define REINTENTO_CONEXION_MS    10000UL   // espera entre intentos de conectar al broker
#define FALLOS_SENSOR_MAX        3         // lecturas fallidas seguidas = sensor caido

// ---------- Valores por defecto (se cambian desde MQTT y quedan guardados) ----------
#define DEF_TEMP_ABRIR      28.0   // °C: abre el venteo
#define DEF_TEMP_CERRAR     24.0   // °C: cierra el venteo (histeresis = 4 °C)
#define DEF_HUM_RIEGO_ON    40.0   // %: debajo de esto empieza a regar
#define DEF_HUM_RIEGO_OFF   60.0   // %: arriba de esto deja de regar
#define DEF_MOTOR_MAX_S     60     // s: si el motor no llega al final de carrera -> ERROR
#define DEF_RIEGO_MAX_MIN   10     // min: corte de seguridad del riego
#define DEF_RIEGO_PAUSA_MIN 30     // min: espera minima entre riegos automaticos
