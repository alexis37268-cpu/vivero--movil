// =============================================================
//  Estacion de oficina - ESP8266 (tambien compila en ESP32)
//
//  Hace de puente: escucha por LoRa a los hibernaculos y publica
//  sus datos en el broker MQTT (vivero/nodoN/...), como si fueran
//  equipos con WiFi. Las ordenes de la app las baja por LoRa y
//  espera la confirmacion, con reintentos.
//
//  Pantalla 16x2: el boton FLASH pasa de una estacion a otra.
//  LED de error: prendido si alguna estacion tiene un problema.
// =============================================================
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include "prefs8266.h"
#else
  #include <WiFi.h>
  #include <Preferences.h>
#endif
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

#include "config.h"
#include "secrets.h"
#include "ca_cert.h"
#include "protocolo.h"

// -------------------------------------------------------------
//  Objetos y estado general
// -------------------------------------------------------------
WiFiClientSecure red;
PubSubClient mqtt(red);
WiFiManager wm;
LiquidCrystal_I2C lcd(LCD_DIRECCION, 16, 2);
#if defined(ESP8266)
  Prefs8266 prefs;
  BearSSL::X509List certCA(CA_CERT);
  #define ID_CHIP ESP.getChipId()
#else
  Preferences prefs;
  #define ID_CHIP ((uint32_t)ESP.getEfuseMac())
#endif

char mqttHost[64] = MQTT_HOST, mqttUser[40] = MQTT_USER, mqttPass[64] = MQTT_PASS;
WiFiManagerParameter *pHost, *pUser, *pPass;
bool loraOk = false, caOk = false;
unsigned long ultIntentoMqtt = 0, sinWifiDesde = 0;

// -------------------------------------------------------------
//  Lo que sabemos de cada hibernaculo
// -------------------------------------------------------------
struct Estacion {
  bool     usada = false;
  uint8_t  num = 0;
  char     nombre[24] = "";
  unsigned long ultimo = 0;        // ultimo paquete recibido
  bool     enLinea = false;
  float    temp = NAN, hum = NAN;
  bool     sensorOk = false, riego = false, autoMode = true;
  uint8_t  venteo = VT_DESCONOCIDO;
  int      rssi = 0;               // senal con la que la escuchamos aca
  bool     tieneEstado = false;
};
Estacion est[MAX_ESTACIONES];

Estacion* buscar(uint8_t num, bool crear) {
  for (auto& e : est) if (e.usada && e.num == num) return &e;
  if (!crear) return nullptr;
  for (auto& e : est) if (!e.usada) { e = Estacion(); e.usada = true; e.num = num; snprintf(e.nombre, sizeof(e.nombre), "Estacion %u", num); return &e; }
  return nullptr;                  // tabla llena
}
bool conProblema(const Estacion& e) {
  return !e.enLinea || (e.tieneEstado && (!e.sensorOk || e.venteo == VT_ERROR));
}
String problema(const Estacion& e) {
  if (!e.enLinea) return "SIN SENAL";
  if (!e.sensorOk) return "SENSOR";
  if (e.venteo == VT_ERROR) return "VENTEO";
  return "";
}

// -------------------------------------------------------------
//  MQTT
// -------------------------------------------------------------
String topicNodo(uint8_t n, const char* sub) { return String("vivero/nodo") + n + "/" + sub; }
void publicar(const String& t, const String& m, bool ret) { if (mqtt.connected()) mqtt.publish(t.c_str(), m.c_str(), ret); }
String jnum(float v) { return isnan(v) ? String("null") : String(v, 1); }

void publicarEstado(const Estacion& e) {
  if (!e.tieneEstado) return;
  String j = "{\"temp\":" + jnum(e.temp) + ",\"hum\":" + jnum(e.hum) +
             ",\"sensor_ok\":" + (e.sensorOk ? "true" : "false") +
             ",\"venteo\":\"" + nombreVenteoCodigo(e.venteo) + "\"" +
             ",\"riego\":\"" + (e.riego ? "on" : "off") + "\"" +
             ",\"modo\":\"" + (e.autoMode ? "auto" : "manual") + "\"" +
             ",\"rssi\":" + e.rssi + ",\"via\":\"lora\"}";
  publicar(topicNodo(e.num, "estado"), j, true);
}
void publicarOnline(const Estacion& e) { publicar(topicNodo(e.num, "online"), e.enLinea ? "online" : "offline", true); }
void publicarNombre(const Estacion& e) { publicar(topicNodo(e.num, "nombre"), e.nombre, true); }

void alarmaEstacion(uint8_t n, const String& texto) {
  Serial.printf("[ALARMA est.%u] %s\n", n, texto.c_str());
  publicar(topicNodo(n, "alarma"), texto, false);
}

// -------------------------------------------------------------
//  Cola de ordenes hacia los hibernaculos
// -------------------------------------------------------------
struct Orden { bool usada = false; uint8_t nodo; uint16_t seq; String orden, valor; uint8_t intentos = 0; unsigned long enviada = 0; };
Orden cola[8];
uint16_t seqOrden = 1;

void encolar(uint8_t nodo, const String& orden, const String& valor) {
  for (auto& o : cola) if (!o.usada) {
    o.usada = true; o.nodo = nodo; o.seq = seqOrden++; o.orden = orden; o.valor = valor; o.intentos = 0; o.enviada = 0;
    Serial.printf("[ORDEN] encolada est.%u %s=%s\n", nodo, orden.c_str(), valor.c_str());
    return;
  }
  alarmaEstacion(nodo, "orden descartada: cola llena");
}

void loraMandar(const String& m) {
  if (!loraOk) return;
  LoRa.beginPacket(); LoRa.print(m); LoRa.endPacket();
  Serial.println("[LORA] -> " + m);
}

// Una orden a la vez: se manda, se espera la K, y si no llega se reintenta
void colaTick() {
  Orden* o = nullptr;
  for (auto& x : cola) if (x.usada) { o = &x; break; }
  if (!o) return;
  if (o->enviada && millis() - o->enviada < ORDEN_ESPERA_MS + (unsigned long)random(0, 500)) return;
  if (o->intentos >= ORDEN_REINTENTOS) {
    alarmaEstacion(o->nodo, "orden sin respuesta: " + o->orden + " " + o->valor);
    o->usada = false; return;
  }
  o->intentos++; o->enviada = millis();
  loraMandar(String(LORA_RED "|M|") + o->nodo + "|" + o->seq + "|" + o->orden + "|" + o->valor);
}
void confirmar(uint8_t nodo, uint16_t seq) {
  for (auto& o : cola) if (o.usada && o.nodo == nodo && o.seq == seq) { o.usada = false; Serial.printf("[ORDEN] confirmada est.%u #%u\n", nodo, seq); }
}

// Llega una orden de la app: vivero/nodoN/cmd/<orden>
void alRecibir(char* topic, byte* payload, unsigned int largo) {
  String tp(topic), msg;
  for (unsigned int i = 0; i < largo; i++) msg += (char)payload[i];
  if (!tp.startsWith("vivero/nodo")) return;
  int barra = tp.indexOf('/', 11);
  uint8_t n = tp.substring(11, barra).toInt();
  String sub = tp.substring(barra + 1);
  if (!sub.startsWith("cmd/")) return;
  // Solo las estaciones que escuchamos por LoRa (el vivero, nodo1, tiene su propio WiFi)
  if (!buscar(n, false)) return;
  encolar(n, sub.substring(4), msg);
}

// -------------------------------------------------------------
//  LoRa: lo que llega de los hibernaculos
// -------------------------------------------------------------
int partir(const String& s, String* out, int max) {
  int n = 0, ini = 0;
  for (int i = 0; i <= (int)s.length() && n < max; i++)
    if (i == (int)s.length() || s[i] == '|') { out[n++] = s.substring(ini, i); ini = i + 1; }
  return n;
}
float fnum(const String& s) { return s == "x" ? NAN : s.toFloat(); }

void loraTick() {
  if (!loraOk) return;
  if (!LoRa.parsePacket()) return;
  String m; while (LoRa.available()) m += (char)LoRa.read();
  int rssi = LoRa.packetRssi();
  String p[12]; int n = partir(m, p, 12);
  if (n < 4 || p[0] != LORA_RED) return;                 // no es de nuestra red
  char tipo = p[1][0];
  uint8_t num = p[2].toInt();
  if (tipo == 'M') return;                               // es una orden (nuestra u otra oficina)
  Serial.println("[LORA] <- " + m + "  (" + rssi + " dBm)");
  Estacion* e = buscar(num, true);
  if (!e) return;
  e->ultimo = millis(); e->rssi = rssi;
  if (!e->enLinea) { e->enLinea = true; publicarOnline(*e); publicarNombre(*e); }

  if (tipo == 'S' && n >= 10) {                          // SP|S|n|seq|t|h|sok|v|r|auto|rssiUlt
    e->temp = fnum(p[4]); e->hum = fnum(p[5]); e->sensorOk = p[6] == "1";
    e->venteo = p[7].toInt(); e->riego = p[8] == "1"; e->autoMode = p[9] == "1";
    e->tieneEstado = true;
    publicarEstado(*e);
  } else if (tipo == 'C' && n >= 11) {                   // SP|C|n|seq|tA|tC|hOn|hOff|recorr|rMax|pausa
    String j = "{\"temp_abrir\":" + p[4] + ",\"temp_cerrar\":" + p[5] + ",\"hum_riego_on\":" + p[6] +
               ",\"hum_riego_off\":" + p[7] + ",\"motor_max_s\":" + p[8] + ",\"riego_max_min\":" + p[9] +
               ",\"riego_pausa_min\":" + p[10] + ",\"recorrido\":true}";
    publicar(topicNodo(num, "config"), j, true);
  } else if (tipo == 'A' && n >= 5) {
    String texto = m.substring(m.indexOf('|', m.indexOf('|', m.indexOf('|', 3) + 1) + 1) + 1);  // todo despues de la 4a barra
    alarmaEstacion(num, texto);
  } else if (tipo == 'N' && n >= 5) {
    strlcpy(e->nombre, p[4].c_str(), sizeof(e->nombre));
    publicarNombre(*e);
  } else if (tipo == 'K') {
    confirmar(num, p[3].toInt());
  }
}

// Estaciones que dejaron de hablar
void vigilarTick() {
  for (auto& e : est) if (e.usada && e.enLinea && millis() - e.ultimo > SIN_SENAL_MS) {
    e.enLinea = false;
    publicarOnline(e);
    alarmaEstacion(e.num, "sin senal LoRa hace mas de 1 minuto");
  }
}

// -------------------------------------------------------------
//  Conexion al broker
// -------------------------------------------------------------
void mqttTick() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) { mqtt.loop(); return; }
  if (!caOk) return;
  if (ultIntentoMqtt && millis() - ultIntentoMqtt < REINTENTO_CONEXION_MS) return;
#if defined(ESP8266)
  if (time(nullptr) < 1700000000) return;              // el 8266 necesita la hora para validar el certificado
#endif
  ultIntentoMqtt = millis();
  String id = String("oficina-") + String(ID_CHIP, HEX);
  Serial.print("[MQTT] conectando al broker... ");
  if (mqtt.connect(id.c_str(), mqttUser, mqttPass, "vivero/oficina/online", 1, true, "offline")) {
    Serial.println("OK");
    publicar("vivero/oficina/online", "online", true);
    mqtt.subscribe("vivero/+/cmd/#", 1);
    for (auto& e : est) if (e.usada) { publicarOnline(e); publicarNombre(e); publicarEstado(e); }
  } else Serial.printf("fallo, codigo %d\n", mqtt.state());
}

void wifiTick() {
  static wl_status_t ant = WL_IDLE_STATUS;
  wl_status_t a = WiFi.status();
  if (a == ant) return;
  ant = a;
  if (a == WL_CONNECTED) { Serial.print("[WIFI] conectado, IP "); Serial.println(WiFi.localIP()); configTime(-3 * 3600, 0, "pool.ntp.org"); }
  else Serial.println("[WIFI] sin conexion");
}

// -------------------------------------------------------------
//  Portal de WiFi (igual que en el vivero)
// -------------------------------------------------------------
const char* portalClave() { return strlen(PORTAL_CLAVE) >= 8 ? PORTAL_CLAVE : nullptr; }
void portalAbrir(const char* motivo) {
  if (wm.getConfigPortalActive()) return;
  Serial.printf("[PORTAL] abriendo red \"%s\" (%s)\n", PORTAL_NOMBRE, motivo);
  wm.startConfigPortal(PORTAL_NOMBRE, portalClave());
}
void portalGuardo() {
  if (strlen(pHost->getValue())) strlcpy(mqttHost, pHost->getValue(), sizeof(mqttHost));
  if (strlen(pUser->getValue())) strlcpy(mqttUser, pUser->getValue(), sizeof(mqttUser));
  if (strlen(pPass->getValue())) strlcpy(mqttPass, pPass->getValue(), sizeof(mqttPass));
  prefs.begin("broker", false);
  prefs.putString("host", mqttHost); prefs.putString("user", mqttUser); prefs.putString("pass", mqttPass);
  prefs.end();
  mqtt.disconnect(); ultIntentoMqtt = 0;
}
void cargarCredenciales() {
  prefs.begin("broker", true);
  String h = prefs.getString("host", ""), u = prefs.getString("user", ""), p = prefs.getString("pass", "");
  prefs.end();
  if (h.length()) strlcpy(mqttHost, h.c_str(), sizeof(mqttHost));
  if (u.length()) strlcpy(mqttUser, u.c_str(), sizeof(mqttUser));
  if (p.length()) strlcpy(mqttPass, p.c_str(), sizeof(mqttPass));
}
void portalSetup() {
  pHost = new WiFiManagerParameter("mqtt_host", "Broker MQTT", mqttHost, sizeof(mqttHost) - 1);
  pUser = new WiFiManagerParameter("mqtt_user", "Usuario MQTT", mqttUser, sizeof(mqttUser) - 1);
  pPass = new WiFiManagerParameter("mqtt_pass", "Clave MQTT (vacio = no cambiar)", "", sizeof(mqttPass) - 1, "type='password'");
  wm.addParameter(pHost); wm.addParameter(pUser); wm.addParameter(pPass);
  wm.setSaveParamsCallback(portalGuardo);
  wm.setTitle("Vivero Sol Puntano - Oficina");
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.setConnectTimeout(10);
  wm.setShowInfoUpdate(false);
  if (wm.autoConnect(PORTAL_NOMBRE, portalClave())) Serial.println("[WIFI] conectado con la red guardada");
  else Serial.printf("[PORTAL] sin WiFi conocido: red \"%s\" abierta\n", PORTAL_NOMBRE);
}
void portalTick() {
  wm.process();
  if (WiFi.status() == WL_CONNECTED) sinWifiDesde = 0;
  else if (!sinWifiDesde) sinWifiDesde = millis();
  else if (millis() - sinWifiDesde > PORTAL_REABRIR_MIN * 60000UL) { portalAbrir("sin WiFi hace rato"); sinWifiDesde = millis(); }
}

// -------------------------------------------------------------
//  Boton FLASH: toque = siguiente pagina, 5 s = portal
// -------------------------------------------------------------
int pagina = 0;                  // 0 = oficina, 1.. = estaciones
unsigned long ultCambioPagina = 0;

int cantidad() { int c = 0; for (auto& e : est) if (e.usada) c++; return c; }
Estacion* estacionDePagina(int p) { int i = 0; for (auto& e : est) if (e.usada && ++i == p) return &e; return nullptr; }

void botonTick() {
  static bool crudo = HIGH, estable = HIGH, largo = false;
  static unsigned long cambio = 0, desde = 0;
  bool r = digitalRead(PIN_BOTON);
  if (r != crudo) { crudo = r; cambio = millis(); }
  if (estable != crudo && millis() - cambio >= ANTIRREBOTE_MS) {
    estable = crudo;
    if (estable == LOW) { desde = millis(); largo = false; }
    else if (!largo) { pagina = (pagina + 1) % (cantidad() + 1); ultCambioPagina = millis(); }   // toque corto al soltar
  }
  if (estable == LOW && !largo && millis() - desde > BOTON_PORTAL_MS) { largo = true; portalAbrir("boton FLASH"); }
}

// -------------------------------------------------------------
//  Pantalla 16x2 y LED de error
// -------------------------------------------------------------
String a16(String s) { if (s.length() > 16) s = s.substring(0, 16); while (s.length() < 16) s += ' '; return s; }
String lin[2];

void pantallaTick() {
  static unsigned long ult = 0;
  if (millis() - ult < 500) return;
  ult = millis();
  if (pagina > cantidad()) pagina = 0;
  // Pasa sola de pagina, salvo que el operario este mirando una con problema
  Estacion* actual = estacionDePagina(pagina);
  if (PAGINA_AUTO_MS && millis() - ultCambioPagina > PAGINA_AUTO_MS && !(actual && conProblema(*actual))) {
    pagina = (pagina + 1) % (cantidad() + 1); ultCambioPagina = millis(); actual = estacionDePagina(pagina);
  }
  String l0, l1;
  if (!actual) {
    int errores = 0; for (auto& e : est) if (e.usada && conProblema(e)) errores++;
    l0 = "OFICINA Est:" + String(cantidad()) + (errores ? " E" + String(errores) : String(""));
    l1 = String(WiFi.status() == WL_CONNECTED ? "WiFi OK " : "WiFi -- ") + (mqtt.connected() ? "MQTT OK" : "MQTT --");
    if (wm.getConfigPortalActive()) l1 = "PORTAL ABIERTO";
    if (!loraOk) l1 = "LoRa NO RESPONDE";
  } else {
    Estacion& e = *actual;
    String med = (e.tieneEstado && e.sensorOk) ? String(e.temp, 1) + "C " + String((int)round(e.hum)) + "%" : String("--");
    l0 = "E" + String(e.num) + " " + med;
    while (l0.length() < 14) l0 += ' ';
    l0 += e.autoMode ? "AU" : "MA";
    String prob = problema(e);
    if (prob.length()) l1 = "ERROR: " + prob;
    else {
      String v = nombreVenteoCodigo(e.venteo); v.toUpperCase();
      l1 = "V:" + v.substring(0, 7) + " R:" + (e.riego ? "ON" : "OFF");
    }
  }
  String s0 = a16(l0), s1 = a16(l1);
  if (s0 != lin[0]) { lcd.setCursor(0, 0); lcd.print(s0); lin[0] = s0; }
  if (s1 != lin[1]) { lcd.setCursor(0, 1); lcd.print(s1); lin[1] = s1; }
}

void ledTick() {
  bool hay = false; for (auto& e : est) if (e.usada && conProblema(e)) hay = true;
  digitalWrite(PIN_LED_ERROR, hay ? HIGH : LOW);
}

// -------------------------------------------------------------
//  Setup y loop
// -------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Oficina - puente LoRa / MQTT ===");
  pinMode(PIN_LED_ERROR, OUTPUT); digitalWrite(PIN_LED_ERROR, LOW);
  pinMode(PIN_BOTON, INPUT_PULLUP);

  Wire.begin();
  lcd.init(); lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Oficina");
  lcd.setCursor(0, 1); lcd.print("Iniciando...");

#if defined(ESP8266)
  if (!Prefs8266::iniciar()) Serial.println("[CONFIG] no se pudo montar la flash");
#endif
  cargarCredenciales();

  LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RST, -1);
  loraOk = LoRa.begin(LORA_FRECUENCIA);
  if (loraOk) {
    LoRa.setSpreadingFactor(LORA_SF); LoRa.setSignalBandwidth(LORA_BW); LoRa.setCodingRate4(LORA_CR);
    LoRa.setSyncWord(LORA_SYNC); LoRa.setTxPower(LORA_POTENCIA_DBM); LoRa.enableCrc();
    Serial.println("[LORA] listo");
  } else Serial.println("[LORA] NO RESPONDE: revisar cables SPI, NSS (D8) y 3,3 V del Ra-02");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  portalSetup();

  caOk = String(CA_CERT).indexOf("PEGAR_ACA") < 0;
#if defined(ESP8266)
  if (caOk) red.setTrustAnchors(&certCA);
  red.setBufferSizes(16384, 1024);
#else
  if (caOk) red.setCACert(CA_CERT);
#endif
  if (!caOk) Serial.println("[MQTT] FALTA el certificado en ca_cert.h");
  mqtt.setServer(mqttHost, MQTT_PORT);
  mqtt.setCallback(alRecibir);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
  mqtt.setSocketTimeout(10);
  randomSeed(ID_CHIP ^ micros());
}

void loop() {
  loraTick();        // primero la radio: que no se pierda ningun paquete
  colaTick();
  vigilarTick();
  wifiTick();
  portalTick();
  mqttTick();
  botonTick();
  pantallaTick();
  ledTick();
}
