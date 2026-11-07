// =============================================================
//  Controlador de vivero - ESP32 / ESP8266 (el mismo archivo para los dos)
//  Temperatura/humedad, venteo (actuador con finales internos, por tiempo) y riego.
//
//  Idea central: TODA la logica corre en el ESP32. Internet y la
//  app son un extra: si se cae la conexion, el vivero sigue solo.
// =============================================================
//
//  Anda en ESP32 y en ESP8266: el compilador sabe para cual es
//  (segun la placa elegida en el Arduino IDE) y usa lo que corresponde.
// =============================================================
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <WiFiClientSecure.h> // TLS (BearSSL)
  #include "prefs8266.h"        // reemplazo de Preferences
#else
  #include <WiFi.h>
  #include <WiFiClientSecure.h> // TLS
  #include <Preferences.h>      // memoria no volatil (sobrevive a cortes de luz)
  #include <LittleFS.h>         // archivos en la flash (registro de cambios)
#endif
#include <PubSubClient.h>       // cliente MQTT
#include <DHT.h>
#include <time.h>

#include "config.h"
#include "secrets.h"
#include "ca_cert.h"

#if !MODO_WOKWI
  #include <WiFiManager.h>      // portal de WiFi (libreria de tzapu)
#endif

// -------------------------------------------------------------
//  Tipos y estado global
// -------------------------------------------------------------
enum EstadoVenteo { V_DESCONOCIDO, V_CERRADO, V_ABIERTO, V_ABRIENDO, V_CERRANDO };

const char* nombreVenteo(EstadoVenteo e) {
  switch (e) {
    case V_CERRADO:  return "cerrado";
    case V_ABIERTO:  return "abierto";
    case V_ABRIENDO: return "abriendo";
    case V_CERRANDO: return "cerrando";
    default:         return "desconocido";
  }
}

struct Config {
  float    tempAbrir, tempCerrar;
  float    humRiegoOn, humRiegoOff;
  uint16_t recorridoS, riegoMaxMin, riegoPausaMin;
  uint16_t climaCierra;          // 1 = con alerta del SMN de viento/tormenta se cierra el venteo   // recorridoS: lo que tarda el actuador de punta a punta (+20 %)
};

DHT              dht(PIN_DHT, DHT_TIPO);
WiFiClientSecure red;          // el "cable" cifrado
PubSubClient     mqtt(red);    // MQTT viaja por ese cable
#if defined(ESP8266)
  Prefs8266      prefs;
  BearSSL::X509List certCA(CA_CERT);   // en el 8266 el certificado va en esta lista
  #define ID_CHIP  ESP.getChipId()
#else
  Preferences    prefs;
  #define ID_CHIP  ((uint32_t)ESP.getEfuseMac())
#endif

Config       cfg;
String       nombreEquipo = NODO_NOMBRE;   // se cambia desde la app (cmd/nombre)
bool         modoAuto = true;
EstadoVenteo venteo   = V_DESCONOCIDO;
unsigned long motorDesde = 0;

bool          riegoOn = false;
unsigned long riegoDesde = 0, riegoFinUltimo = 0;
bool          riegoAlgunaVez = false;

float   temp = NAN, hum = NAN;
float   sumT = 0, sumH = 0;   // suma de lecturas para el promedio del historial
uint16_t nProm = 0;
bool    sensorOk = false;
uint8_t fallosSensor = 0;

bool caOk = false;
unsigned long ultLectura = 0, ultPublicacion = 0, ultIntentoMqtt = 0;

// Datos del broker que se usan de verdad. Arrancan con lo de config.h/secrets.h;
// en el equipo real se pueden cambiar desde el portal y quedan guardados.
char mqttHost[64] = MQTT_HOST;
char mqttUser[40] = MQTT_USER;
char mqttPass[64] = MQTT_PASS;
bool hayCambios = false;       // algo cambio -> publicar estado ya

// Alerta meteorologica: el robot del SMN publica hasta cuando cerrar (0 = no hay)
uint32_t cierreHasta = 0;
bool     cierreAvisado = false;
bool     climaCierreActivo();
void     registrar(const String& quien, const String& que);
#if N_CULTIVOS > 0
int8_t   cultivoSueloProm(uint8_t i);   // en cultivos.h
#endif

// Arma un topic: t("estado") -> "vivero/nodo1/estado"
String t(const char* sub) { return String(TOPIC_BASE "/") + sub; }

// -------------------------------------------------------------
//  Publicaciones MQTT
// -------------------------------------------------------------
void publicar(const String& topic, const String& msg, bool retener) {
  if (mqtt.connected()) mqtt.publish(topic.c_str(), msg.c_str(), retener);
}

void alarma(const String& texto) {
  Serial.println("[ALARMA] " + texto);
  publicar(t("alarma"), texto, false);   // no retenido: es un aviso, no un estado
}

String num(float v) { return isnan(v) ? String("null") : String(v, 1); }

// Un solo JSON con todo el estado: menos mensajes = menos cuota usada.
void publicarEstado() {
  String j = "{";
  j += "\"temp\":"      + num(temp);
  j += ",\"hum\":"      + num(hum);
  j += ",\"sensor_ok\":" + String(sensorOk ? "true" : "false");
  j += ",\"venteo\":\"" + String(nombreVenteo(venteo)) + "\"";
  j += ",\"riego\":\""  + String(riegoOn ? "on" : "off") + "\"";
  j += ",\"modo\":\""   + String(modoAuto ? "auto" : "manual") + "\"";
  j += ",\"rssi\":"     + String(WiFi.RSSI());
  j += ",\"uptime_s\":" + String(millis() / 1000);
  j += ",\"clima_cierre\":" + String(climaCierreActivo() ? "true" : "false");
  j += "}";
  publicar(t("estado"), j, true);        // retenido: la app ve el ultimo al abrir
  hayCambios = false;
}

void publicarConfig() {
  String j = "{";
  j += "\"temp_abrir\":"       + String(cfg.tempAbrir, 1);
  j += ",\"temp_cerrar\":"     + String(cfg.tempCerrar, 1);
  j += ",\"hum_riego_on\":"    + String(cfg.humRiegoOn, 1);
  j += ",\"hum_riego_off\":"   + String(cfg.humRiegoOff, 1);
  j += ",\"motor_max_s\":"     + String(cfg.recorridoS);   // la clave queda igual para no tocar la app
  j += ",\"riego_max_min\":"   + String(cfg.riegoMaxMin);
  j += ",\"riego_pausa_min\":" + String(cfg.riegoPausaMin);
  j += ",\"recorrido\":true";                // la app muestra "Tiempo de recorrido del actuador"
  j += ",\"hist\":true";                     // este equipo guarda historial para el grafico
  j += ",\"clima_cierra\":" + String(cfg.climaCierra);
  j += ",\"usuarios\":true";                  // este equipo maneja usuarios y registro
  j += ",\"renombrar\":true";                 // el nombre se cambia desde la app
  j += ",\"cultivos\":" + String(N_CULTIVOS);   // riego por cultivos (0 = riego unico)
  j += "}";
  publicar(t("config"), j, true);
}

// -------------------------------------------------------------
//  Memoria no volatil
// -------------------------------------------------------------
void cargarConfig() {
  prefs.begin("vivero", true);           // true = solo lectura
  cfg.tempAbrir     = prefs.getFloat("tAbrir",  DEF_TEMP_ABRIR);
  cfg.tempCerrar    = prefs.getFloat("tCerrar", DEF_TEMP_CERRAR);
  cfg.humRiegoOn    = prefs.getFloat("hOn",     DEF_HUM_RIEGO_ON);
  cfg.humRiegoOff   = prefs.getFloat("hOff",    DEF_HUM_RIEGO_OFF);
  cfg.recorridoS    = prefs.getUShort("recorr", DEF_RECORRIDO_S);
  cfg.riegoMaxMin   = prefs.getUShort("rMax",   DEF_RIEGO_MAX_MIN);
  cfg.riegoPausaMin = prefs.getUShort("rPausa", DEF_RIEGO_PAUSA_MIN);
  cfg.climaCierra   = prefs.getUShort("climaC", 1);
  modoAuto          = prefs.getBool("auto", true);
  nombreEquipo      = prefs.getString("nombre", NODO_NOMBRE);
  if (!nombreEquipo.length()) nombreEquipo = NODO_NOMBRE;
  prefs.end();
}

void guardarConfig() {
  prefs.begin("vivero", false);
  prefs.putFloat("tAbrir",  cfg.tempAbrir);
  prefs.putFloat("tCerrar", cfg.tempCerrar);
  prefs.putFloat("hOn",     cfg.humRiegoOn);
  prefs.putFloat("hOff",    cfg.humRiegoOff);
  prefs.putUShort("recorr", cfg.recorridoS);
  prefs.putUShort("rMax",   cfg.riegoMaxMin);
  prefs.putUShort("rPausa", cfg.riegoPausaMin);
  prefs.putUShort("climaC", cfg.climaCierra);
  prefs.putBool("auto",     modoAuto);
  prefs.putString("nombre", nombreEquipo.c_str());
  prefs.end();
}

// -------------------------------------------------------------
//  Salidas: reles
// -------------------------------------------------------------
void escribirRele(uint8_t pin, bool encendido) {
  // Traduce "encendido" al nivel que necesita el modulo (activo alto o bajo)
  digitalWrite(pin, (encendido == RELE_ACTIVO_ALTO) ? HIGH : LOW);
}

// -------------------------------------------------------------
//  Entradas con antirrebote (sin delay)
//
//  Un contacto mecanico "rebota": al cerrarse abre y cierra varias
//  veces en pocos milisegundos. Esta clase solo acepta un cambio si
//  la entrada quedo ESTABLE durante ANTIRREBOTE_MS. Usa millis(),
//  asi que nunca frena el loop.
// -------------------------------------------------------------
class Entrada {
  uint8_t pin;
  bool crudo, estable;           // lo que lee el pin ahora / lo ultimo aceptado
  unsigned long cambio = 0;      // cuando cambio por ultima vez lo crudo
public:
  explicit Entrada(uint8_t p) : pin(p), crudo(HIGH), estable(HIGH) {}
  void begin() {
    pinMode(pin, INPUT_PULLUP);
    crudo = estable = digitalRead(pin);
    cambio = millis();
  }
  // Devuelve true solo en el momento en que se acepta un cambio
  bool actualizar() {
    bool r = digitalRead(pin);
    if (r != crudo) { crudo = r; cambio = millis(); }        // rebote: reinicia la cuenta
    if (estable != crudo && millis() - cambio >= ANTIRREBOTE_MS) { estable = crudo; return true; }
    return false;
  }
  bool activo() const { return estable == LOW; }              // contacto cerrado a GND
};

Entrada bModo(PIN_BOTON_MODO);
Entrada bRiego(PIN_BOTON_RIEGO);
Entrada bVenteo(PIN_BOTON_VENTEO);
#if !MODO_WOKWI && (PIN_BOTON_PORTAL != PIN_BOTON_MODO)
  Entrada bPortal(PIN_BOTON_PORTAL);
#endif

// -------------------------------------------------------------
//  Venteo por tiempo (actuador lineal con finales de carrera INTERNOS)
//
//  El ESP no sabe donde esta el actuador: solo sabe cuanto tiempo lo
//  tuvo prendido. Abrir = rele ABRIR prendido "recorrido" segundos;
//  el actuador se corta solo al llegar a su tope y nosotros cortamos el
//  rele despues, para no dejarlo alimentado de mas.
//  Por eso al arrancar CIERRA: es la unica forma de saber donde esta.
//  Si se para a mitad de camino queda "desconocido".
// -------------------------------------------------------------
bool motorMoviendo() { return venteo == V_ABRIENDO || venteo == V_CERRANDO; }

// Al invertir el giro (de abrir a cerrar o al reves) el motor queda
// parado TIEMPO_MUERTO_MS antes de arrancar para el otro lado.
// En vez de delay(), se anota que rele hay que prender y cuando.
int           arranquePin = -1;
unsigned long arranqueDesde = 0;

void motorParar() {
  escribirRele(PIN_VENTEO_ABRIR,  false);
  escribirRele(PIN_VENTEO_CERRAR, false);
  arranquePin = -1;                      // cancela un arranque pendiente
}

void motorArrancar(uint8_t pin, bool invertir) {
  motorParar();
  if (invertir) { arranquePin = pin; arranqueDesde = millis(); }   // lo prende venteoTick
  else escribirRele(pin, true);
  motorDesde = millis() + (invertir ? TIEMPO_MUERTO_MS : 0);       // el tiempo cuenta desde que arranca de verdad
}

void setVenteo(EstadoVenteo e) {
  if (e == venteo) return;
  venteo = e;
  Serial.printf("[VENTEO] %s\n", nombreVenteo(e));
  hayCambios = true;
}

void venteoAbrir() {
  if (venteo == V_ABIERTO || venteo == V_ABRIENDO) return;
  motorArrancar(PIN_VENTEO_ABRIR, venteo == V_CERRANDO);
  setVenteo(V_ABRIENDO);
}

void venteoCerrar() {
  if (venteo == V_CERRADO || venteo == V_CERRANDO) return;
  motorArrancar(PIN_VENTEO_CERRAR, venteo == V_ABRIENDO);
  setVenteo(V_CERRANDO);
}

void venteoParar() {
  bool seMovia = motorMoviendo();
  motorParar();
  if (seMovia) setVenteo(V_DESCONOCIDO);   // quedo a mitad de camino
}

// Se llama en cada vuelta del loop: arranque pendiente y fin del recorrido
void venteoTick() {
  if (arranquePin >= 0 && millis() - arranqueDesde >= TIEMPO_MUERTO_MS) {
    escribirRele((uint8_t)arranquePin, true);
    arranquePin = -1;
  }
  if (!motorMoviendo() || (long)(millis() - motorDesde) < 0) return;   // todavia en tiempo muerto
  if (millis() - motorDesde >= (unsigned long)cfg.recorridoS * 1000UL) {
    motorParar();
    setVenteo(venteo == V_ABRIENDO ? V_ABIERTO : V_CERRADO);
  }
}

// -------------------------------------------------------------
//  Riego
// -------------------------------------------------------------
void riegoSet(bool encender, const char* motivo) {
  if (encender == riegoOn) return;
  riegoOn = encender;
  escribirRele(PIN_RIEGO, encender);
  if (encender) riegoDesde = millis();
  else { riegoFinUltimo = millis(); riegoAlgunaVez = true; }
  Serial.printf("[RIEGO] %s (%s)\n", encender ? "ON" : "OFF", motivo);
  hayCambios = true;
}

void riegoTick() {
#if N_CULTIVOS > 0
  return;                            // con cultivos, los cortes los hace cultivosTick()
#endif
  // Corte de seguridad: vale en automatico y en manual
  if (riegoOn && millis() - riegoDesde > (unsigned long)cfg.riegoMaxMin * 60000UL) {
    riegoSet(false, "tiempo maximo");
    alarma("riego: se corto por tiempo maximo");
    registrar("automatico", "corta el riego por tiempo maximo");
  }
}

// -------------------------------------------------------------
//  Sensor
// -------------------------------------------------------------
void leerSensor() {
  float tt = dht.readTemperature();
  float hh = dht.readHumidity();
  if (isnan(tt) || isnan(hh)) {
    if (fallosSensor < 255) fallosSensor++;
    if (fallosSensor >= FALLOS_SENSOR_MAX && sensorOk) {
      sensorOk = false;
      temp = NAN; hum = NAN;
      alarma("sensor sin lectura");
      hayCambios = true;
    }
    return;
  }
  fallosSensor = 0;
  temp = tt; hum = hh;
  sumT += tt; sumH += hh; nProm++;        // para el promedio del historial
  if (!sensorOk) { sensorOk = true; hayCambios = true; }
}

// -------------------------------------------------------------
//  Historial para el grafico de la app (ultimas 48 h)
//
//  Cada 10 min se guarda el PROMEDIO de las lecturas de ese rato
//  (una cada 5 s = ~120 lecturas), asi la curva sale prolija.
//  Es un "buffer circular": cuando se llena, el punto nuevo pisa
//  al mas viejo. 288 puntos x 4 bytes = ~1,2 KB de RAM.
//  Se graba en la flash cada 1 h y no cada punto, porque la flash
//  aguanta una cantidad limitada de escrituras.
// -------------------------------------------------------------
const int16_t SIN_DATO = INT16_MIN;        // hueco (sensor caido o equipo apagado)
struct PuntoHist {                         // en decimas: 234 = 23,4
  int16_t t, h;
#if N_CULTIVOS > 0
  int8_t  s[N_CULTIVOS];                   // humedad de suelo de cada cultivo (%), -128 = sin dato
#endif
};
struct CabHist   { uint16_t cab, cant; uint32_t epoch; };

PuntoHist hist[HIST_N];
uint16_t histCab = 0, histCant = 0;        // proximo lugar a escribir / cuantos hay
uint32_t histEpoch = 0;                    // fecha y hora del ultimo punto (0 = no se sabe)
unsigned long histDesde = 0;               // millis() del ultimo punto
uint8_t  sinGrabar = 0;
bool     huecoRevisado = false;

uint32_t ahoraEpoch() {                    // 0 si todavia no hay hora de internet
  time_t a = time(nullptr);
  return a > 1700000000 ? (uint32_t)a : 0;
}

void histAgregar(int16_t t, int16_t h) {
  hist[histCab].t = t; hist[histCab].h = h;
#if N_CULTIVOS > 0
  for (uint8_t i = 0; i < N_CULTIVOS; i++) hist[histCab].s[i] = (t == SIN_DATO) ? -128 : cultivoSueloProm(i);
#endif
  histCab = (histCab + 1) % HIST_N;
  if (histCant < HIST_N) histCant++;
}

void histGrabar() {
  CabHist c = { histCab, histCant, histEpoch };
#if defined(ESP8266)
  File f = LittleFS.open("/hist.bin", "w");
  if (f) { f.write((uint8_t*)&c, sizeof(c)); f.write((uint8_t*)hist, sizeof(hist)); f.close(); }
#else
  prefs.begin("hist", false);
  prefs.putBytes("cab", &c, sizeof(c));
  prefs.putBytes("datos", hist, sizeof(hist));
  prefs.end();
#endif
}

void histCargar() {
  CabHist c = { 0, 0, 0 };
  bool ok = false;
#if defined(ESP8266)
  File f = LittleFS.open("/hist.bin", "r");
  if (f && f.size() == sizeof(c) + sizeof(hist)) {
    f.read((uint8_t*)&c, sizeof(c)); f.read((uint8_t*)hist, sizeof(hist)); ok = true;
  }
  if (f) f.close();
#else
  prefs.begin("hist", true);
  ok = prefs.getBytesLength("cab") == sizeof(c) && prefs.getBytesLength("datos") == sizeof(hist);
  if (ok) { prefs.getBytes("cab", &c, sizeof(c)); prefs.getBytes("datos", hist, sizeof(hist)); }
  prefs.end();
#endif
  if (!ok || c.cab >= HIST_N || c.cant > HIST_N) return;   // primera vez, o cambio el tamano
  histCab = c.cab; histCant = c.cant; histEpoch = c.epoch;
  Serial.printf("[HIST] %u puntos recuperados de la flash\n", histCant);
}

void histTick() {
  uint32_t ahora = ahoraEpoch();

  // Si el equipo estuvo apagado, se marcan esas horas como hueco
  // (solo se puede saber con la hora de internet).
  if (!huecoRevisado && ahora) {
    huecoRevisado = true;
    if (histEpoch && ahora > histEpoch) {
      uint32_t faltan = (ahora - histEpoch) / HIST_PASO_S;
      if (faltan > HIST_N) faltan = HIST_N;
      for (uint32_t i = 0; i < faltan; i++) histAgregar(SIN_DATO, SIN_DATO);
      if (faltan) Serial.printf("[HIST] estuvo apagado: %u puntos sin datos\n", (unsigned)faltan);
    }
  }

  if (millis() - histDesde < HIST_PASO_S * 1000UL) return;
  histDesde = millis();
  huecoRevisado = true;                     // si a esta altura no hubo hora, no se revisa mas

  if (nProm) histAgregar((int16_t)lroundf(sumT / nProm * 10), (int16_t)lroundf(sumH / nProm * 10));
  else       histAgregar(SIN_DATO, SIN_DATO);   // 10 min sin lecturas buenas
  sumT = sumH = 0; nProm = 0;
  histEpoch = ahora;

  if (++sinGrabar >= HIST_GRABAR_CADA) { sinGrabar = 0; histGrabar(); }
}

// Se manda en partes chicas (48 puntos, ~600 bytes) para no necesitar
// un buffer MQTT grande: en el 8266 la memoria es justa.
// "edad" = segundos desde el ultimo punto; con eso la app calcula la
// hora de cada punto aunque el equipo no tenga hora de internet.
void publicarHistorial() {
  if (!mqtt.connected()) return;
  uint16_t partes = histCant ? (histCant + HIST_POR_MENSAJE - 1) / HIST_POR_MENSAJE : 1;
  uint32_t edad   = (millis() - histDesde) / 1000;
  uint16_t inicio = (histCab + HIST_N - histCant) % HIST_N;   // el mas viejo
  for (uint16_t p = 0; p < partes; p++) {
    uint16_t desde = p * HIST_POR_MENSAJE;
    uint16_t hasta = min<uint16_t>(histCant, desde + HIST_POR_MENSAJE);
    String j = "{\"p\":" + String(p) + ",\"de\":" + String(partes) + ",\"n\":" + String(histCant) +
               ",\"dt\":" + String(HIST_PASO_S) + ",\"edad\":" + String(edad) +
               ",\"desde\":" + String(desde) + ",\"v\":[";
    for (uint16_t k = desde; k < hasta; k++) {
      const PuntoHist& q = hist[(inicio + k) % HIST_N];
      if (k > desde) j += ',';
      if (q.t == SIN_DATO) j += "null";
      else {
        j += "[" + String(q.t) + "," + String(q.h);
#if N_CULTIVOS > 0
        for (uint8_t i = 0; i < N_CULTIVOS; i++) j += q.s[i] == -128 ? String(",null") : "," + String(q.s[i]);
#endif
        j += "]";
      }
    }
    j += "]}";
    publicar(t("historial"), j, false);
  }
}

// -------------------------------------------------------------
//  Usuarios (nombre + PIN) y registro de cambios
//
//  La lista la carga el administrador desde la app y queda guardada
//  aca, asi es la misma en todos los celulares. Del PIN solo se guarda
//  un "hash" (un resumen que no permite leer el PIN).
//  Cada orden llega "firmada": "abrir;Juan". El ESP anota quien,
//  que y cuando en el registro (~300 acciones, en la flash).
//  Tambien anota lo que se hace con los pulsadores ("panel") y lo
//  que hace solo ("automatico").
// -------------------------------------------------------------
struct Usuario { String nombre; bool admin; String hash; };
Usuario  usuarios[MAX_USUARIOS];
uint8_t  nUsuarios = 0;
bool     fsOk = false;
uint16_t regLineas = 0;                    // lineas en el archivo actual
const char* REG_ACTUAL   = "/reg0.txt";
const char* REG_ANTERIOR = "/reg1.txt";

int buscarUsuario(const String& n) {
  for (uint8_t i = 0; i < nUsuarios; i++) if (usuarios[i].nombre.equalsIgnoreCase(n)) return i;
  return -1;
}
// Sin usuarios cargados todo vale (como antes). Con usuarios: solo un administrador.
bool esAdmin(const String& q) {
  if (nUsuarios == 0) return true;
  int i = buscarUsuario(q);
  return i >= 0 && usuarios[i].admin;
}
String quienTxt(const String& q) { return q.length() ? q : String("sin usuario"); }

// Saca lo que romperia el formato (separadores, comillas, saltos de linea)
String limpiar(String s, uint8_t max) {
  s.replace("|", "/"); s.replace(";", ","); s.replace("\"", "'"); s.replace("\\", "/");
  s.replace("\n", " "); s.replace("\r", " "); s.trim();
  if (s.length() > max) s = s.substring(0, max);
  return s;
}

void usuariosGuardar() {
  String l;
  for (uint8_t i = 0; i < nUsuarios; i++) {
    if (i) l += ';';
    l += usuarios[i].nombre + ',' + (usuarios[i].admin ? 'a' : 'o') + ',' + usuarios[i].hash;
  }
  prefs.begin("usuarios", false);
  prefs.putString("lista", l.c_str());
  prefs.end();
}

void usuariosCargar() {
  prefs.begin("usuarios", true);
  String l = prefs.getString("lista", "");
  prefs.end();
  nUsuarios = 0;
  while (l.length() && nUsuarios < MAX_USUARIOS) {
    int pc = l.indexOf(';');
    String u = pc >= 0 ? l.substring(0, pc) : l;
    l = pc >= 0 ? l.substring(pc + 1) : "";
    int c1 = u.indexOf(','), c2 = u.indexOf(',', c1 + 1);
    if (c1 <= 0 || c2 <= c1) continue;
    usuarios[nUsuarios++] = { u.substring(0, c1), u.substring(c1 + 1, c2) == "a", u.substring(c2 + 1) };
  }
  Serial.printf("[USUARIOS] %u cargados\n", nUsuarios);
}

// Retenido: la app lo recibe al conectarse (para la pantalla de ingreso)
void publicarUsuarios() {
  String j = "[";
  for (uint8_t i = 0; i < nUsuarios; i++) {
    if (i) j += ',';
    j += "{\"n\":\"" + usuarios[i].nombre + "\",\"r\":\"" + (usuarios[i].admin ? "admin" : "operario") +
         "\",\"h\":\"" + usuarios[i].hash + "\"}";
  }
  j += "]";
  publicar(t("usuarios"), j, true);
}

// Archivos de lineas con rotacion: 2 archivos de REG_POR_ARCHIVO lineas.
// Cuando el actual se llena, el viejo se borra y el actual pasa a ser el viejo.
// (Lo usan el registro de cambios y el de riegos.)
void anotarLinea(const char* actual, const char* anterior, uint16_t& lineas, const String& linea) {
  if (!fsOk) return;
  if (lineas >= REG_POR_ARCHIVO) {
    LittleFS.remove(anterior);
    LittleFS.rename(actual, anterior);
    lineas = 0;
  }
  File f = LittleFS.open(actual, "a");
  if (f) { f.println(linea); f.close(); lineas++; }
}

uint16_t contarLineas(const char* archivo) {
  uint16_t n = 0;
  File f = LittleFS.open(archivo, "r");
  if (f) { while (f.available()) if (f.read() == '\n') n++; f.close(); }
  return n;
}

// Manda los dos archivos en partes chicas (del mas viejo al mas nuevo)
void publicarLineas(const char* anterior, const char* actual, const String& topic) {
  if (!mqtt.connected()) return;
  const char* archivos[2] = { anterior, actual };
  uint16_t total = fsOk ? contarLineas(anterior) + contarLineas(actual) : 0;
  uint16_t partes = total ? (total + REG_POR_MENSAJE - 1) / REG_POR_MENSAJE : 1;
  uint16_t p = 0, enParte = 0;
  String j = "{\"p\":0,\"de\":" + String(partes) + ",\"l\":[";
  if (fsOk) for (auto a : archivos) {
    File f = LittleFS.open(a, "r");
    if (!f) continue;
    while (f.available()) {
      String linea = f.readStringUntil('\n'); linea.trim();
      if (!linea.length()) continue;
      if (enParte) j += ',';
      j += "\"" + linea + "\"";
      if (++enParte >= REG_POR_MENSAJE) {
        j += "]}"; publicar(topic, j, false);
        p++; enParte = 0;
        j = "{\"p\":" + String(p) + ",\"de\":" + String(partes) + ",\"l\":[";
      }
    }
    f.close();
  }
  if (enParte || total == 0) { j += "]}"; publicar(topic, j, false); }
}

void registrar(const String& quien, const String& que) {
  String linea = String(ahoraEpoch()) + "|" + limpiar(quienTxt(quien), 20) + "|" + limpiar(que, 60);
  Serial.println("[REG] " + linea);
  publicar(t("registro/nuevo"), linea, false);     // la app lo ve al instante
  anotarLinea(REG_ACTUAL, REG_ANTERIOR, regLineas, linea);
}

void registroIniciar() {
#if defined(ESP8266)
  fsOk = LittleFS.begin();                          // ya montada por prefs8266 (no pasa nada si se repite)
#else
  fsOk = LittleFS.begin(true);                      // true = la formatea la primera vez
#endif
  if (!fsOk) { Serial.println("[REG] no se pudo montar la flash: el registro no se guarda"); return; }
  regLineas = contarLineas(REG_ACTUAL);
}

void publicarRegistro() { publicarLineas(REG_ANTERIOR, REG_ACTUAL, t("registro")); }

// Reseteo desde el tablero (RIEGO + VENTEO 10 s): para cuando nadie recuerda
// el PIN del administrador. Solo lo puede hacer alguien parado frente al equipo.
void usuariosBorrar() {
  nUsuarios = 0;
  usuariosGuardar();
  publicarUsuarios();
  registrar("panel", "borro todos los usuarios (reseteo desde el tablero)");
  alarma("usuarios: borrados desde el tablero. Crear el administrador de nuevo en la app");
}

// cmd/usuario/alta  "nombre;rol;hash;quien"   (rol = admin | operario)
// cmd/usuario/baja  "nombre;quien"
// cmd/usuario/pin   "nombre;hash;quien"       (el admin, o el mismo usuario)
void ordenUsuario(const String& accion, const String& crudo) {
  String c[4]; uint8_t n = 0; int desde = 0;
  while (n < 4) {
    int p = crudo.indexOf(';', desde);
    c[n++] = (p >= 0 ? crudo.substring(desde, p) : crudo.substring(desde));
    if (p < 0) break;
    desde = p + 1;
  }
  for (uint8_t i = 0; i < n; i++) c[i].trim();
  String quien = n ? c[n - 1] : "";
  String nombre = limpiar(c[0], 16);
  bool hashOk = false;

  if (accion == "alta" && n == 4) {
    hashOk = c[2].length() == 16;
    bool admin = c[1] == "admin" || nUsuarios == 0;           // el primero siempre es administrador
    if (!esAdmin(quien)) { registrar(quien, "rechazado: alta de " + nombre + " (no es administrador)"); alarma("usuarios: solo un administrador puede dar de alta"); return; }
    if (!nombre.length() || !hashOk) { alarma("usuarios: datos invalidos"); return; }
    int i = buscarUsuario(nombre);
    if (i < 0) {
      if (nUsuarios >= MAX_USUARIOS) { alarma("usuarios: no entran mas de " + String(MAX_USUARIOS)); return; }
      usuarios[nUsuarios++] = { nombre, admin, c[2] };
      registrar(nUsuarios == 1 ? nombre : quien, "alta de usuario " + nombre + (admin ? " (administrador)" : " (operario)"));
    } else {
      usuarios[i].admin = admin; usuarios[i].hash = c[2];
      registrar(quien, "modifica usuario " + nombre + (admin ? " (administrador)" : " (operario)"));
    }
  }
  else if (accion == "baja" && n == 2) {
    int i = buscarUsuario(nombre);
    if (!esAdmin(quien)) { registrar(quien, "rechazado: baja de " + nombre); alarma("usuarios: solo un administrador puede dar de baja"); return; }
    if (i < 0) return;
    uint8_t admins = 0;
    for (uint8_t k = 0; k < nUsuarios; k++) admins += usuarios[k].admin;
    if (usuarios[i].admin && admins <= 1) { alarma("usuarios: no se puede borrar al unico administrador"); return; }
    for (uint8_t k = i; k + 1 < nUsuarios; k++) usuarios[k] = usuarios[k + 1];
    nUsuarios--;
    registrar(quien, "baja de usuario " + nombre);
  }
  else if (accion == "pin" && n == 3) {
    int i = buscarUsuario(nombre);
    if (i < 0 || c[1].length() != 16) return;
    if (!esAdmin(quien) && !usuarios[i].nombre.equalsIgnoreCase(quien)) { registrar(quien, "rechazado: cambio de PIN de " + nombre); return; }
    usuarios[i].hash = c[1];
    registrar(quien, "cambia el PIN de " + usuarios[i].nombre);
  }
  else return;
  usuariosGuardar();
  publicarUsuarios();
}

#include "cultivos.h"      // riego por cultivos (si N_CULTIVOS > 0)

// -------------------------------------------------------------
//  Alerta meteorologica (la manda el robot del SMN por MQTT)
// -------------------------------------------------------------
bool climaCierreActivo() {
  if (!cfg.climaCierra || !cierreHasta) return false;
  uint32_t ahora = ahoraEpoch();
  return ahora == 0 || ahora < cierreHasta;   // sin hora de internet: se confia en el robot
}

// -------------------------------------------------------------
//  Logica automatica (con histeresis)
// -------------------------------------------------------------
void controlAuto() {
  // Alerta del SMN (viento, tormenta, granizo): manda sobre la temperatura.
  // En automatico cierra; en manual solo avisa (decide el operario).
  bool cierre = climaCierreActivo();
  if (cierre && !cierreAvisado) {
    cierreAvisado = true;
    time_t h = cierreHasta; struct tm tl; localtime_r(&h, &tl);
    char txt[96];
    snprintf(txt, sizeof(txt), modoAuto ? "alerta SMN: se cierra el venteo hasta las %02d:%02d"
                                        : "alerta SMN: conviene cerrar el venteo (esta en manual) hasta las %02d:%02d",
             tl.tm_hour, tl.tm_min);
    alarma(txt);
    registrar("automatico", txt);
    hayCambios = true;
  }
  if (!cierre && cierreAvisado) { cierreAvisado = false; hayCambios = true; Serial.println("[CLIMA] termino la alerta"); }

  if (!modoAuto) return;
  if (cierre && venteo != V_CERRADO && venteo != V_CERRANDO) { venteoCerrar(); registrar("automatico", "cierra el venteo por alerta SMN"); }

  // Sin sensor no se decide nada: riego apagado, venteo queda donde esta
  if (!sensorOk) { if (riegoOn) registrar("automatico", "corta el riego: sensor caido"); riegoSet(false, "sensor caido"); return; }

  // Venteo: abre arriba de tempAbrir, cierra abajo de tempCerrar.
  // Entre medio no hace nada (eso es la histeresis: evita que tiemble).
  if (cierre) { /* con alerta no se abre */ }
  else if (temp >= cfg.tempAbrir && venteo != V_ABIERTO && venteo != V_ABRIENDO) {
    venteoAbrir();  registrar("automatico", "abre el venteo (" + String(temp, 1) + " C)");
  }
  else if (temp <= cfg.tempCerrar && venteo != V_CERRADO && venteo != V_CERRANDO) {
    venteoCerrar(); registrar("automatico", "cierra el venteo (" + String(temp, 1) + " C)");
  }

#if N_CULTIVOS == 0
  // Riego unico (sin cultivos): usa la humedad del aire
  bool pausaCumplida = !riegoAlgunaVez ||
      (millis() - riegoFinUltimo >= (unsigned long)cfg.riegoPausaMin * 60000UL);
  if (!riegoOn && hum <= cfg.humRiegoOn && pausaCumplida) { riegoSet(true,  "auto"); registrar("automatico", "empieza a regar (" + String(hum, 0) + " %)"); }
  else if (riegoOn && hum >= cfg.humRiegoOff)             { riegoSet(false, "auto"); registrar("automatico", "termina de regar (" + String(hum, 0) + " %)"); }
#endif   // con cultivos, el riego lo maneja cultivosTick()
}

// -------------------------------------------------------------
//  Ordenes que llegan por MQTT  (topics vivero/nodo1/cmd/...)
// -------------------------------------------------------------
bool aplicarConfig(const String& clave, float v, const String& quien) {
  if (!esAdmin(quien)) {
    registrar(quien, "rechazado: ajuste " + clave + " (no es administrador)");
    alarma("config: " + quienTxt(quien) + " no es administrador");
    return false;
  }
  Config n = cfg;                        // probamos sobre una copia
  if      (clave == "temp_abrir")      n.tempAbrir = v;
  else if (clave == "temp_cerrar")     n.tempCerrar = v;
  else if (clave == "hum_riego_on")    n.humRiegoOn = v;
  else if (clave == "hum_riego_off")   n.humRiegoOff = v;
  else if (clave == "motor_max_s")     n.recorridoS = (uint16_t)v;   // = tiempo de recorrido
  else if (clave == "riego_max_min")   n.riegoMaxMin = (uint16_t)v;
  else if (clave == "riego_pausa_min") n.riegoPausaMin = (uint16_t)v;
  else if (clave == "clima_cierra")    n.climaCierra = (uint16_t)v;
  else { alarma("config: clave desconocida " + clave); return false; }

  // Validaciones: un valor absurdo no entra
  bool ok = n.tempAbrir  >= -10 && n.tempAbrir  <= 60 &&
            n.tempCerrar >= -10 && n.tempCerrar <= 60 &&
            n.tempCerrar <  n.tempAbrir - 0.5 &&
            n.humRiegoOn >= 0 && n.humRiegoOff <= 100 &&
            n.humRiegoOn <  n.humRiegoOff &&
            n.recorridoS >= 5 && n.recorridoS <= 600 &&
            n.riegoMaxMin >= 1 && n.riegoMaxMin <= 120 &&
            n.riegoPausaMin <= 1440 &&
            n.climaCierra <= 1;
  if (!ok) { alarma("config: valor rechazado para " + clave); registrar(quien, "rechazado: " + clave + " = " + String(v, 1)); return false; }

  cfg = n;
  guardarConfig();
  publicarConfig();
  if (clave == "clima_cierra") { cierreAvisado = false; controlAuto(); hayCambios = true; }
  Serial.printf("[CONFIG] %s = %.1f\n", clave.c_str(), v);
  registrar(quien, "ajuste " + clave + " = " + String(v, 1));
  return true;
}

void alRecibir(char* topic, byte* payload, unsigned int largo) {
  String tp(topic);
  String crudo;
  for (unsigned int i = 0; i < largo; i++) crudo += (char)payload[i];
  crudo.trim();
  // Ordenes firmadas: "abrir;Juan" -> valor "abrir", quien "Juan" (sin ";" = sin usuario)
  int sep = crudo.indexOf(';');
  String msg   = sep >= 0 ? crudo.substring(0, sep) : crudo;
  String quien = sep >= 0 ? crudo.substring(sep + 1) : "";
  msg.trim(); msg.toLowerCase(); quien.trim();
  Serial.printf("[MQTT] <- %s : %s\n", topic, crudo.c_str());

  if (tp == "vivero/clima/cierre") {        // del robot de alertas del SMN
    cierreHasta = (uint32_t)msg.toInt();
    Serial.printf("[CLIMA] cierre de venteo hasta %lu\n", (unsigned long)cierreHasta);
    controlAuto();
    hayCambios = true;
    return;
  }

  String base = t("cmd/");
  if (!tp.startsWith(base)) return;
  String cmd = tp.substring(base.length());   // ej: "venteo", "config/temp_abrir"

  if (cmd == "nombre") {                    // nombre que se ve en la app (solo administrador)
    String nuevo = limpiar(sep >= 0 ? crudo.substring(0, sep) : crudo, 24);
    if (!esAdmin(quien)) { registrar(quien, "rechazado: cambio de nombre (no es administrador)"); alarma("nombre: solo un administrador lo cambia"); return; }
    if (nuevo.length() < 2 || nuevo == nombreEquipo) return;
    registrar(quien, "cambia el nombre: " + nombreEquipo + " -> " + nuevo);
    nombreEquipo = nuevo;
    guardarConfig();
    publicar(t("nombre"), nombreEquipo, true);
    return;
  }
  if (cmd == "modo") {
    if (msg == "auto" || msg == "manual") {
      modoAuto = (msg == "auto");
      guardarConfig();
      hayCambios = true;
      registrar(quien, modoAuto ? "pasa a automatico" : "pasa a manual");
      if (modoAuto) controlAuto();
    }
  }
  else if (cmd == "venteo") {
    if (msg == "parar") { venteoParar(); hayCambios = true; registrar(quien, "para el venteo"); }   // PARAR siempre vale
    else if (modoAuto) { alarma("venteo: orden ignorada, esta en automatico"); registrar(quien, "pide " + msg + " venteo (ignorado: automatico)"); }
    else if (msg == "abrir")  { venteoAbrir();  registrar(quien, "abre el venteo"); }
    else if (msg == "cerrar") { venteoCerrar(); registrar(quien, "cierra el venteo"); }
  }
#if N_CULTIVOS > 0
  else if (cmd == "riego") {                // con cultivos: "off" corta el que este regando
    if (msg == "off") { if (regando >= 0) cultivoCerrar("cortado a mano", quien); }
    else alarma("riego: elegir el cultivo en la pestana Cultivos");
  }
  else if (cmd.startsWith("cultivo/")) {
    ordenCultivo(cmd.substring(8), crudo);
  }
  else if (cmd == "riegos") {               // la app pide el registro de riegos
    publicarRiegos();
  }
#else
  else if (cmd == "riego") {
    if (msg == "off") { if (riegoOn) registrar(quien, "corta el riego"); riegoSet(false, "orden"); }   // APAGAR siempre vale
    else if (modoAuto) { alarma("riego: orden ignorada, esta en automatico"); registrar(quien, "pide regar (ignorado: automatico)"); }
    else if (msg == "on") { riegoSet(true, "orden manual"); registrar(quien, "riega"); }
  }
#endif
  else if (cmd.startsWith("config/")) {
    aplicarConfig(cmd.substring(7), msg.toFloat(), quien);
  }
  else if (cmd.startsWith("usuario/")) {
    ordenUsuario(cmd.substring(8), crudo);
  }
  else if (cmd == "registro") {           // la app pide el registro de cambios
    publicarRegistro();
  }
  else if (cmd == "estado") {             // la app pide el estado al instante
    publicarEstado();
    publicarConfig();
  }
  else if (cmd == "historial") {          // la app pide los datos para el grafico
    publicarHistorial();
  }
}

// -------------------------------------------------------------
//  Conexion al broker (sin trabar la logica)
// -------------------------------------------------------------
void mqttTick() {
  static bool          estabaConectado = false;
  static unsigned long conectadoDesde  = 0;

  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) { mqtt.loop(); return; }

  // Diagnostico: si se acaba de caer, decir POR QUE y cuanto duro.
  // -4 = el broker no respondio a tiempo (keepalive)  -3 = se corto la conexion
  // -2 = no pudo conectar  -1 = desconexion normal
  if (estabaConectado) {
    estabaConectado = false;
    Serial.printf("[MQTT] se cayo la conexion, codigo %d, duro %lus\n",
                  mqtt.state(), (millis() - conectadoDesde) / 1000);
  }
  if (!caOk) return;
  // Conectar con TLS tarda unos segundos y bloquea:
  // no lo intentamos mientras el motor se mueve.
  if (motorMoviendo()) return;
  if (ultIntentoMqtt != 0 && millis() - ultIntentoMqtt < REINTENTO_CONEXION_MS) return;
#if defined(ESP8266)
  // El 8266 revisa la fecha de vencimiento del certificado: sin hora correcta
  // (la saca de internet al conectar el WiFi) rechaza el broker. Esperamos la hora.
  if (time(nullptr) < 1700000000) return;
#endif
  ultIntentoMqtt = millis();

  String id = String("vivero-" NODO_ID "-") + String(ID_CHIP, HEX);
  Serial.print("[MQTT] conectando al broker... ");

  // Ultimo deseo (LWT): si el ESP32 se cae sin avisar, el broker
  // publica "offline" en su nombre. Asi la app sabe que esta caido.
  if (mqtt.connect(id.c_str(), mqttUser, mqttPass,
                   t("online").c_str(), 1, true, "offline")) {
    Serial.println("OK");
    estabaConectado = true;
    conectadoDesde  = millis();
    publicar(t("online"), "online", true);
    publicar(t("nombre"), nombreEquipo, true);
    mqtt.subscribe(t("cmd/#").c_str(), 1);
    mqtt.subscribe("vivero/clima/cierre", 1);   // alertas del SMN (lo publica el robot)
    publicarConfig();
    publicarEstado();
    publicarUsuarios();
#if N_CULTIVOS > 0
    publicarCultivosCfg();
    publicarCultivos();
#endif
  } else {
    // -2 = no conecta (red/TLS/certificado), 4 = usuario o clave mal, 5 = no autorizado
    Serial.printf("fallo, codigo %d. Reintento en %lus\n",
                  mqtt.state(), REINTENTO_CONEXION_MS / 1000);
  }
}

void wifiTick() {
  static wl_status_t anterior = WL_IDLE_STATUS;
  wl_status_t ahora = WiFi.status();
  if (ahora == anterior) return;
  anterior = ahora;
  if (ahora == WL_CONNECTED) {
    Serial.print("[WIFI] conectado, IP ");
    Serial.println(WiFi.localIP());
    configTime(-3 * 3600, 0, "pool.ntp.org");   // hora (UTC-3), util para TLS y a futuro
  } else {
    Serial.println("[WIFI] sin conexion (reintenta solo)");
  }
}

// -------------------------------------------------------------
//  Datos del broker guardados (los carga el portal)
// -------------------------------------------------------------
void cargarCredenciales() {
  prefs.begin("broker", true);
  String h = prefs.getString("host", ""), u = prefs.getString("user", ""), p = prefs.getString("pass", "");
  prefs.end();
  if (h.length()) strlcpy(mqttHost, h.c_str(), sizeof(mqttHost));
  if (u.length()) strlcpy(mqttUser, u.c_str(), sizeof(mqttUser));
  if (p.length()) strlcpy(mqttPass, p.c_str(), sizeof(mqttPass));
}

#if !MODO_WOKWI
// -------------------------------------------------------------
//  Portal de WiFi (WiFiManager), en modo NO bloqueante:
//  mientras esta abierto, el venteo y el riego siguen funcionando.
// -------------------------------------------------------------
WiFiManager wm;
WiFiManagerParameter* pHost;
WiFiManagerParameter* pUser;
WiFiManagerParameter* pPass;
unsigned long sinWifiDesde = 0;

// Clave de la red del portal: vacia (o menos de 8 caracteres) = red ABIERTA, sin contrasena.
const char* portalClave() { return strlen(PORTAL_CLAVE) >= 8 ? PORTAL_CLAVE : nullptr; }

void portalAbrir(const char* motivo) {
  if (wm.getConfigPortalActive()) return;
  Serial.printf("[PORTAL] abriendo red \"%s\" (%s). Conectate con el celular.\n", PORTAL_NOMBRE, motivo);
  wm.startConfigPortal(PORTAL_NOMBRE, portalClave());
}

// Se llama cuando en el portal tocan "Save"
void portalGuardo() {
  // La clave solo se cambia si escribieron algo (el campo arranca vacio a proposito)
  if (strlen(pHost->getValue())) strlcpy(mqttHost, pHost->getValue(), sizeof(mqttHost));
  if (strlen(pUser->getValue())) strlcpy(mqttUser, pUser->getValue(), sizeof(mqttUser));
  if (strlen(pPass->getValue())) strlcpy(mqttPass, pPass->getValue(), sizeof(mqttPass));
  prefs.begin("broker", false);
  prefs.putString("host", mqttHost);
  prefs.putString("user", mqttUser);
  prefs.putString("pass", mqttPass);
  prefs.end();
  Serial.printf("[PORTAL] guardado: broker %s, usuario %s\n", mqttHost, mqttUser);
  mqtt.disconnect();          // reconecta con los datos nuevos
  ultIntentoMqtt = 0;
}

void portalSetup() {
  pHost = new WiFiManagerParameter("mqtt_host", "Broker MQTT", mqttHost, sizeof(mqttHost) - 1);
  pUser = new WiFiManagerParameter("mqtt_user", "Usuario MQTT", mqttUser, sizeof(mqttUser) - 1);
  pPass = new WiFiManagerParameter("mqtt_pass", "Clave MQTT (vacio = no cambiar)", "", sizeof(mqttPass) - 1,
                                   "type='password'");
  wm.addParameter(pHost);
  wm.addParameter(pUser);
  wm.addParameter(pPass);
  wm.setSaveParamsCallback(portalGuardo);
  wm.setTitle("Vivero Sol Puntano");
  wm.setConfigPortalBlocking(false);         // clave: no frena el loop
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.setConnectTimeout(10);                  // al probar un WiFi nuevo, espera como mucho 10 s
  wm.setShowInfoUpdate(false);               // sin boton de actualizar firmware desde el portal

#if PIN_BOTON_PORTAL != PIN_BOTON_MODO
  bPortal.begin();
#endif

  // Prueba el WiFi guardado; si no hay o no conecta, abre el portal (y sigue de largo)
  if (wm.autoConnect(PORTAL_NOMBRE, portalClave())) Serial.println("[WIFI] conectado con la red guardada");
  else Serial.printf("[PORTAL] sin WiFi conocido: red \"%s\" abierta. Conectate con el celular.\n", PORTAL_NOMBRE);
}

void portalTick() {
  wm.process();               // atiende la pagina del portal si esta abierta

#if PIN_BOTON_PORTAL != PIN_BOTON_MODO
  // ESP32: boton BOOT apretado 5 s -> abrir portal (para cambiar de WiFi o de broker)
  static unsigned long apretadoDesde = 0;
  static bool disparado = false;
  if (bPortal.actualizar()) { apretadoDesde = millis(); disparado = false; }
  if (bPortal.activo() && !disparado && millis() - apretadoDesde > BOTON_PORTAL_MS) {
    disparado = true; portalAbrir("boton BOOT");
  }
#endif
  // (En el 8266 el boton FLASH es MODO y portal a la vez: lo maneja botonesTick)

  // Mucho tiempo sin WiFi (cambiaron el router, la clave...) -> abrir portal solo
  if (WiFi.status() == WL_CONNECTED) sinWifiDesde = 0;
  else if (sinWifiDesde == 0) sinWifiDesde = millis();
  else if (millis() - sinWifiDesde > PORTAL_REABRIR_MIN * 60000UL) { portalAbrir("sin WiFi hace rato"); sinWifiDesde = millis(); }
}
#endif

// -------------------------------------------------------------
//  Pulsadores del tablero (funcionan sin celular y sin internet)
//    MODO:   cada toque alterna automatico / manual
//    RIEGO:  (solo en manual) prende / apaga
//    VENTEO: (solo en manual) cerrado -> abre, abierto -> cierra,
//            moviendose -> PARA
//  En automatico RIEGO y VENTEO no hacen nada (y lo avisa), para que
//  nadie pelee con la logica automatica sin darse cuenta.
// -------------------------------------------------------------
void cambiarModo(const char* origen) {
  modoAuto = !modoAuto;
  guardarConfig();
  hayCambios = true;
  Serial.printf("[MODO] %s (%s)\n", modoAuto ? "automatico" : "manual", origen);
  registrar("panel", modoAuto ? "pasa a automatico" : "pasa a manual");
  if (modoAuto) controlAuto();
}

void pulsoRiego() {
  if (modoAuto) { alarma("pulsador RIEGO ignorado: esta en automatico"); return; }
#if N_CULTIVOS > 0
  if (regando >= 0) cultivoCerrar("pulsador", "panel");      // con cultivos: el pulsador corta el riego
  else alarma("pulsador RIEGO: elegir el cultivo desde la app");
#else
  riegoSet(!riegoOn, "pulsador");
  registrar("panel", riegoOn ? "riega" : "corta el riego");
#endif
}

void pulsoVenteo() {
  if (modoAuto) { alarma("pulsador VENTEO ignorado: esta en automatico"); return; }
  if (motorMoviendo())           { venteoParar(); Serial.println("[VENTEO] parado con pulsador"); registrar("panel", "para el venteo"); }
  else if (venteo == V_CERRADO)  { venteoAbrir();  registrar("panel", "abre el venteo"); }
  else                           { venteoCerrar(); registrar("panel", "cierra el venteo"); }   // abierto o a mitad: cerrar es lo mas seguro
}

void botonesSetup() {
  bModo.begin();
  bRiego.begin();
  bVenteo.begin();
}

void botonesTick() {
  // Se actua al APRETAR (flanco a GND), una sola vez por toque
  if (bRiego.actualizar()  && bRiego.activo())  pulsoRiego();
  if (bVenteo.actualizar() && bVenteo.activo()) pulsoVenteo();

  // RIEGO + VENTEO juntos 10 s: borra los usuarios (recuperar el administrador).
  // Conviene hacerlo en AUTOMATICO, asi los dos toques no mueven nada.
  static unsigned long comboDesde = 0;
  static bool comboHecho = false;
  if (bRiego.activo() && bVenteo.activo()) {
    if (!comboDesde) { comboDesde = millis(); Serial.println("[USUARIOS] RIEGO + VENTEO: mantener 10 s para borrar usuarios"); }
    else if (!comboHecho && millis() - comboDesde >= RESET_USUARIOS_MS) { comboHecho = true; usuariosBorrar(); }
  } else { comboDesde = 0; comboHecho = false; }

#if !MODO_WOKWI && (PIN_BOTON_PORTAL == PIN_BOTON_MODO)
  // 8266: el boton FLASH es MODO (toque corto) y PORTAL (mantener 5 s).
  // Por eso el modo cambia al SOLTAR, y solo si fue un toque corto.
  static unsigned long desde = 0;
  static bool largo = false;
  if (bModo.actualizar()) {
    if (bModo.activo()) { desde = millis(); largo = false; }
    else if (!largo && millis() - desde < 1000) cambiarModo("pulsador");
  }
  if (bModo.activo() && !largo && millis() - desde > BOTON_PORTAL_MS) {
    largo = true;
    portalAbrir("boton FLASH");
  }
#else
  if (bModo.actualizar() && bModo.activo()) cambiarModo("pulsador");
#endif
}

// -------------------------------------------------------------
//  Setup y loop
// -------------------------------------------------------------
void setup() {
#if defined(ESP8266)
  // En el 8266 el pin RX se usa para el pulsador VENTEO: el puerto serie
  // queda solo para transmitir (el monitor serie sigue mostrando todo).
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
#else
  Serial.begin(115200);
#endif
  delay(200);   // unico delay: al arrancar, antes de que haya algo que controlar
  Serial.println("\n=== Controlador de vivero " NODO_ID " ===");

  // 1) Lo primero: todas las salidas APAGADAS
  pinMode(PIN_VENTEO_ABRIR, OUTPUT);
  pinMode(PIN_VENTEO_CERRAR, OUTPUT);
  pinMode(PIN_RIEGO, OUTPUT);
  motorParar();
  escribirRele(PIN_RIEGO, false);

  botonesSetup();

  // 2) Config guardada (o valores por defecto la primera vez)
#if defined(ESP8266)
  if (!Prefs8266::iniciar()) Serial.println("[CONFIG] no se pudo montar la flash: uso valores por defecto");
#endif
  cargarConfig();
  histCargar();
  usuariosCargar();
  registroIniciar();
#if N_CULTIVOS > 0
  cultivosIniciar();
#endif
  Serial.printf("[CONFIG] abrir %.1f / cerrar %.1f °C, riego %.0f-%.0f %%, recorrido %u s, modo %s\n",
                cfg.tempAbrir, cfg.tempCerrar, cfg.humRiegoOn, cfg.humRiegoOff,
                cfg.recorridoS, modoAuto ? "auto" : "manual");

  // Sin finales de carrera no sabemos donde quedo el venteo: lo cerramos
  // de punta a punta para arrancar sabiendo la posicion.
  venteoCerrar();

  dht.begin();

  // 3) Red
  cargarCredenciales();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
#if MODO_WOKWI
  WiFi.begin(WIFI_SSID, WIFI_PASS);          // Wokwi: red fija, sin portal
#else
  portalSetup();                              // equipo real: WiFi guardado o portal
#endif

  // 4) MQTT con TLS
  caOk = String(CA_CERT).indexOf("PEGAR_ACA") < 0;   // ¿pegaron el certificado?
#if defined(ESP8266)
  if (caOk) red.setTrustAnchors(&certCA);
  red.setBufferSizes(16384, 1024);   // TLS en el 8266: lo justo para no quedarse sin memoria
#else
  if (caOk) red.setCACert(CA_CERT);
#endif
  if (!caOk) Serial.println("[MQTT] FALTA el certificado en ca_cert.h: no se conecta al broker");
  mqtt.setServer(mqttHost, MQTT_PORT);
  mqtt.setCallback(alRecibir);
  mqtt.setBufferSize(1024);             // alcanza para una parte del historial
  mqtt.setKeepAlive(MQTT_KEEPALIVE_S);   // ver config.h
  mqtt.setSocketTimeout(10);
}

void loop() {
  unsigned long ahora = millis();

  if (ahora - ultLectura >= INTERVALO_LECTURA_MS) {
    ultLectura = ahora;
    leerSensor();
    controlAuto();
  }

  venteoTick();
  riegoTick();
#if N_CULTIVOS > 0
  cultivosTick();
#endif
  botonesTick();
  histTick();

  // Se anota el encendido cuando ya hay hora de internet (o al minuto, sin hora)
  static bool encendidoAnotado = false;
  if (!encendidoAnotado && (ahoraEpoch() || ahora > 60000UL)) { encendidoAnotado = true; registrar("equipo", "se encendio"); }

  wifiTick();
#if !MODO_WOKWI
  portalTick();
#endif
  mqttTick();

  if (hayCambios || ahora - ultPublicacion >= INTERVALO_PUBLICACION_MS) {
    ultPublicacion = ahora;
    publicarEstado();
  }
}
