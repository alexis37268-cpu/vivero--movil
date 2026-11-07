// =============================================================
//  config.h  -  Todo lo que se ajusta por instalacion
//  (pines, tipo de sensor, valores por defecto, broker)
//  Las claves NO van aca: van en secrets.h
// =============================================================
#pragma once

// =============================================================
//  EL UNICO INTERRUPTOR QUE HAY QUE TOCAR
//    1 = simulador Wokwi   (DHT22, keepalive 120, WiFi fijo "Wokwi-GUEST", LEDs)
//    0 = equipo real       (DHT11, keepalive 30, portal de WiFi, modulo de reles)
// =============================================================
#define MODO_WOKWI  1

// ---------- Identidad del equipo ----------
// Cada controlador tiene su ID. Si manana hay otro vivero: "nodo2".
#define NODO_ID     "nodo1"
#define TOPIC_BASE  "vivero/" NODO_ID      // => vivero/nodo1/...
#define NODO_NOMBRE "Vivero"                // nombre que se ve en la app

// ---------- Broker MQTT (EMQX Serverless) ----------
// Valor por defecto: en el equipo real se puede cambiar desde el portal de WiFi.
#define MQTT_HOST   "p64138d1.ala.us-east-1.emqxsl.com"
#define MQTT_PORT   8883                    // MQTT sobre TLS
// Keepalive: cada cuanto el ESP32 le avisa al broker "sigo vivo".
// Si el broker no recibe nada en 1,5 x este tiempo, corta la conexion.
// En Wokwi va mas largo porque el simulador anda mas lento que el reloj real.
#if MODO_WOKWI
  #define MQTT_KEEPALIVE_S  120
#else
  #define MQTT_KEEPALIVE_S  30
#endif

// ---------- Portal de WiFi (solo equipo real) ----------
// Si no encuentra una red conocida, el ESP32 arma su propia red WiFi.
// Te conectas con el celular, se abre una pagina y cargas el WiFi del lugar
// y los datos del broker. Red abierta o con clave: se elige en secrets.h (PORTAL_CLAVE).
#define PORTAL_NOMBRE      "Vivero-" NODO_ID   // nombre de la red del portal
#define PORTAL_TIMEOUT_S   300                 // se cierra solo si nadie entra en 5 min
#define PORTAL_REABRIR_MIN 10                  // sin WiFi tanto tiempo -> vuelve a abrir el portal
#define PIN_BOTON_PORTAL   0                   // boton BOOT de la placa
#define BOTON_PORTAL_MS    5000                // mantenerlo 5 s abre el portal

// ---------- Pines ----------
// Se eligen solos segun la placa. En el NodeMCU (ESP8266) se evitan
// D3, D4 y D8, que deciden el arranque: con algo conectado ahi a veces no bootea.
#if defined(ESP8266)
  // Van con el numero GPIO (no "D5", "D1"...) para que compile con cualquier
  // placa ESP8266 elegida en el IDE: NodeMCU, "Generic ESP8266 Module", etc.
  #define PIN_DHT            14    // D5
  #define PIN_VENTEO_ABRIR   5     // D1
  #define PIN_VENTEO_CERRAR  4     // D2
  #define PIN_RIEGO          16    // D0
  // D6 y D7 quedan libres (antes eran los finales de carrera)
  // Pulsadores del tablero (a GND, sin resistencias: pull-up interna)
  #define PIN_BOTON_MODO     0     // boton FLASH de la placa: toque = modo, 5 s = portal
  #define PIN_BOTON_RIEGO    2     // D4 - ojo: no arrancar la placa con este apretado
  #define PIN_BOTON_VENTEO   3     // RX (el monitor serie igual funciona)
#else
  // Pulsadores del tablero (a GND, sin resistencias: pull-up interna)
  #define PIN_BOTON_MODO     16
  #define PIN_BOTON_RIEGO    17
  #define PIN_BOTON_VENTEO   33
  // 21 y 22 quedan libres para la pantallita (I2C)
  #define PIN_DHT            4
  #define PIN_VENTEO_ABRIR   25
  #define PIN_VENTEO_CERRAR  26
  #define PIN_RIEGO          27
  // Pines reservados para el futuro en el ESP32 (NO usar):
  //   32, 34, 35, 36, 39 -> sensores de humedad de suelo (ADC1, andan con WiFi)
  //   5, 18, 19, 23 -> modulo LoRa (SPI)
#endif

// ---------- Sensor de temperatura y humedad ----------
#if MODO_WOKWI
  #define DHT_TIPO  DHT22                      // Wokwi solo trae DHT22
#else
  #define DHT_TIPO  DHT11                      // el de Matias
#endif

// ---------- Reles ----------
// Los LEDs de Wokwi se prenden con HIGH -> true.
// Los modulos de rele chinos comunes se activan con LOW -> false.
// Si en el equipo real los reles quedan al reves, cambiar el false por true.
#if MODO_WOKWI
  #define RELE_ACTIVO_ALTO true
#else
  #define RELE_ACTIVO_ALTO false
#endif

// Pulsadores: contacto a GND al apretar (se usa pull-up interno).
// En el ESP8266, D0 no tiene pull-up: por eso ahi va un rele y no un pulsador.

// ---------- Cultivos (riego por sectores, de a uno, con un solo caudalimetro) ----------
#if defined(ESP8266)
  #define N_CULTIVOS 0            // el 8266 tiene una sola entrada analogica: sin cultivos
#else
  #define N_CULTIVOS 5            // cuantos cultivos/sectores (hasta 5 con estos pines)
  #define PINES_VALVULAS { 13, 14, 18, 19, 23 }   // un rele (valvula) por cultivo
  #define PINES_SUELO    { 32, 34, 35, 36, 39 }   // sensores de suelo capacitivos (ADC1: andan con WiFi)
  #define PIN_CAUDAL     22                       // caudalimetro (cable de senal, pulsos)
#endif
#define DEMO_CULTIVOS       1     // 1 = SIMULA suelo y caudal (para mostrar sin sensores). 0 = equipo real
#define USAR_BOMBA_GENERAL  1     // 1 = el rele de RIEGO (bomba o valvula general) se prende con cualquier cultivo
#define PULSOS_POR_LITRO    450.0 // YF-S201 (revisar el dato del caudalimetro que se compre)
#define SIN_CAUDAL_S        10    // bomba andando y sin agua tantos segundos = corta todo (marcha en seco)
#define SUELO_SECO_RAW      3000  // lectura del sensor al aire (calibrar con el sensor real)
#define SUELO_MOJADO_RAW    1300  // lectura con el sensor en un vaso de agua (calibrar)
#define PAUSA_CULTIVO_MIN   5     // espera minima entre dos riegos automaticos del mismo cultivo

// ---------- Tiempos ----------
#define ANTIRREBOTE_MS           50UL      // pulsadores: estable 50 ms = cambio real
#define TIEMPO_MUERTO_MS         300UL     // motor parado antes de invertir el giro
#define INTERVALO_LECTURA_MS     5000UL    // cada cuanto lee el sensor
#define INTERVALO_PUBLICACION_MS 30000UL   // cada cuanto publica (cuida la cuota gratis)
#define REINTENTO_CONEXION_MS    10000UL   // espera entre intentos de conectar al broker
#define FALLOS_SENSOR_MAX        3         // lecturas fallidas seguidas = sensor caido

// ---------- Historial para el grafico de la app ----------
#define HIST_N            288    // puntos guardados: 288 x 10 min = 48 h
#define HIST_PASO_S       600    // un punto (promedio) cada 10 min
#define HIST_GRABAR_CADA  6      // se graba en la flash cada 6 puntos = 1 h
#define HIST_POR_MENSAJE  32     // puntos por mensaje MQTT (se manda en partes)

// ---------- Usuarios y registro de cambios ----------
#define MAX_USUARIOS      12     // usuarios con nombre y PIN (los da de alta el administrador)
#define REG_POR_ARCHIVO   150    // el registro usa 2 archivos de 150 lineas = ~300 acciones
#define REG_POR_MENSAJE   12     // lineas del registro por mensaje MQTT
#define RESET_USUARIOS_MS 10000UL  // RIEGO + VENTEO apretados juntos 10 s = borrar usuarios

// ---------- Valores por defecto (se cambian desde MQTT y quedan guardados) ----------
#define DEF_TEMP_ABRIR      28.0   // °C: abre el venteo
#define DEF_TEMP_CERRAR     24.0   // °C: cierra el venteo (histeresis = 4 °C)
#define DEF_HUM_RIEGO_ON    40.0   // %: debajo de esto empieza a regar
#define DEF_HUM_RIEGO_OFF   60.0   // %: arriba de esto deja de regar
#define DEF_RECORRIDO_S     40     // s: lo que tarda el actuador de punta a punta + 20 % (medirlo)
#define DEF_RIEGO_MAX_MIN   10     // min: corte de seguridad del riego
#define DEF_RIEGO_PAUSA_MIN 30     // min: espera minima entre riegos automaticos
