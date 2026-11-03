// =============================================================
//  Controlador de vivero - ESP32
//  Temperatura/humedad, venteo (motor DC con 2 reles) y riego.
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
enum EstadoVenteo { V_DESCONOCIDO, V_CERRADO, V_ABIERTO, V_ABRIENDO, V_CERRANDO, V_ERROR };

const char* nombreVenteo(EstadoVenteo e) {
  switch (e) {
    case V_CERRADO:  return "cerrado";
    case V_ABIERTO:  return "abierto";
    case V_ABRIENDO: return "abriendo";
    case V_CERRANDO: return "cerrando";
    case V_ERROR:    return "error";
    default:         return "desconocido";
  }
}

struct Config {
  float    tempAbrir, tempCerrar;
  float    humRiegoOn, humRiegoOff;
  uint16_t motorMaxS, riegoMaxMin, riegoPausaMin;
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
bool         modoAuto = true;
EstadoVenteo venteo   = V_DESCONOCIDO;
unsigned long motorDesde = 0;

bool          riegoOn = false;
unsigned long riegoDesde = 0, riegoFinUltimo = 0;
bool          riegoAlgunaVez = false;

float   temp = NAN, hum = NAN;
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
  j += ",\"motor_max_s\":"     + String(cfg.motorMaxS);
  j += ",\"riego_max_min\":"   + String(cfg.riegoMaxMin);
  j += ",\"riego_pausa_min\":" + String(cfg.riegoPausaMin);
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
  cfg.motorMaxS     = prefs.getUShort("motMax", DEF_MOTOR_MAX_S);
  cfg.riegoMaxMin   = prefs.getUShort("rMax",   DEF_RIEGO_MAX_MIN);
  cfg.riegoPausaMin = prefs.getUShort("rPausa", DEF_RIEGO_PAUSA_MIN);
  modoAuto          = prefs.getBool("auto", true);
  prefs.end();
}

void guardarConfig() {
  prefs.begin("vivero", false);
  prefs.putFloat("tAbrir",  cfg.tempAbrir);
  prefs.putFloat("tCerrar", cfg.tempCerrar);
  prefs.putFloat("hOn",     cfg.humRiegoOn);
  prefs.putFloat("hOff",    cfg.humRiegoOff);
  prefs.putUShort("motMax", cfg.motorMaxS);
  prefs.putUShort("rMax",   cfg.riegoMaxMin);
  prefs.putUShort("rPausa", cfg.riegoPausaMin);
  prefs.putBool("auto",     modoAuto);
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
//  Venteo
// -------------------------------------------------------------
bool finAbierto() { return digitalRead(PIN_FIN_ABIERTO) == LOW; }
bool finCerrado() { return digitalRead(PIN_FIN_CERRADO) == LOW; }
bool motorMoviendo() { return venteo == V_ABRIENDO || venteo == V_CERRANDO; }

void motorParar() {
  escribirRele(PIN_VENTEO_ABRIR,  false);
  escribirRele(PIN_VENTEO_CERRAR, false);
}

void setVenteo(EstadoVenteo e) {
  if (e == venteo) return;
  venteo = e;
  Serial.printf("[VENTEO] %s\n", nombreVenteo(e));
  hayCambios = true;
}

void venteoError(const String& motivo) {
  motorParar();
  if (venteo != V_ERROR) alarma("venteo: " + motivo);
  setVenteo(V_ERROR);
}

void venteoAbrir() {
  if (finAbierto()) { motorParar(); setVenteo(V_ABIERTO); return; }
  if (venteo == V_ABRIENDO) return;
  bool invertir = (venteo == V_CERRANDO);
  motorParar();
  if (invertir) delay(300);              // tiempo muerto antes de invertir el giro
  escribirRele(PIN_VENTEO_ABRIR, true);
  motorDesde = millis();
  setVenteo(V_ABRIENDO);
}

void venteoCerrar() {
  if (finCerrado()) { motorParar(); setVenteo(V_CERRADO); return; }
  if (venteo == V_CERRANDO) return;
  bool invertir = (venteo == V_ABRIENDO);
  motorParar();
  if (invertir) delay(300);
  escribirRele(PIN_VENTEO_CERRAR, true);
  motorDesde = millis();
  setVenteo(V_CERRANDO);
}

void venteoParar() {
  motorParar();
  if (finAbierto())      setVenteo(V_ABIERTO);
  else if (finCerrado()) setVenteo(V_CERRADO);
  else if (venteo != V_ERROR) setVenteo(V_DESCONOCIDO);
}

// Se llama en cada vuelta del loop: vigila finales de carrera y tiempos
void venteoTick() {
  if (finAbierto() && finCerrado()) {    // imposible fisicamente: cable o switch mal
    venteoError("los dos finales de carrera activos");
    return;
  }
  unsigned long limite = (unsigned long)cfg.motorMaxS * 1000UL;
  if (venteo == V_ABRIENDO) {
    if (finAbierto()) { motorParar(); setVenteo(V_ABIERTO); }
    else if (millis() - motorDesde > limite) venteoError("no llego a ABIERTO en el tiempo maximo");
  } else if (venteo == V_CERRANDO) {
    if (finCerrado()) { motorParar(); setVenteo(V_CERRADO); }
    else if (millis() - motorDesde > limite) venteoError("no llego a CERRADO en el tiempo maximo");
  } else if (venteo == V_DESCONOCIDO) {
    if (finAbierto())      setVenteo(V_ABIERTO);
    else if (finCerrado()) setVenteo(V_CERRADO);
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
  // Corte de seguridad: vale en automatico y en manual
  if (riegoOn && millis() - riegoDesde > (unsigned long)cfg.riegoMaxMin * 60000UL) {
    riegoSet(false, "tiempo maximo");
    alarma("riego: se corto por tiempo maximo");
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
  if (!sensorOk) { sensorOk = true; hayCambios = true; }
}

// -------------------------------------------------------------
//  Logica automatica (con histeresis)
// -------------------------------------------------------------
void controlAuto() {
  if (!modoAuto) return;

  // Sin sensor no se decide nada: riego apagado, venteo queda donde esta
  if (!sensorOk) { riegoSet(false, "sensor caido"); return; }

  // Venteo: abre arriba de tempAbrir, cierra abajo de tempCerrar.
  // Entre medio no hace nada (eso es la histeresis: evita que tiemble).
  if (venteo != V_ERROR) {
    if (temp >= cfg.tempAbrir && venteo != V_ABIERTO && venteo != V_ABRIENDO)
      venteoAbrir();
    else if (temp <= cfg.tempCerrar && venteo != V_CERRADO && venteo != V_CERRANDO)
      venteoCerrar();
  }

  // Riego (PROVISORIO: usa humedad del aire hasta tener sensor de suelo)
  bool pausaCumplida = !riegoAlgunaVez ||
      (millis() - riegoFinUltimo >= (unsigned long)cfg.riegoPausaMin * 60000UL);
  if (!riegoOn && hum <= cfg.humRiegoOn && pausaCumplida) riegoSet(true,  "auto");
  else if (riegoOn && hum >= cfg.humRiegoOff)             riegoSet(false, "auto");
}

// -------------------------------------------------------------
//  Ordenes que llegan por MQTT  (topics vivero/nodo1/cmd/...)
// -------------------------------------------------------------
bool aplicarConfig(const String& clave, float v) {
  Config n = cfg;                        // probamos sobre una copia
  if      (clave == "temp_abrir")      n.tempAbrir = v;
  else if (clave == "temp_cerrar")     n.tempCerrar = v;
  else if (clave == "hum_riego_on")    n.humRiegoOn = v;
  else if (clave == "hum_riego_off")   n.humRiegoOff = v;
  else if (clave == "motor_max_s")     n.motorMaxS = (uint16_t)v;
  else if (clave == "riego_max_min")   n.riegoMaxMin = (uint16_t)v;
  else if (clave == "riego_pausa_min") n.riegoPausaMin = (uint16_t)v;
  else { alarma("config: clave desconocida " + clave); return false; }

  // Validaciones: un valor absurdo no entra
  bool ok = n.tempAbrir  >= -10 && n.tempAbrir  <= 60 &&
            n.tempCerrar >= -10 && n.tempCerrar <= 60 &&
            n.tempCerrar <  n.tempAbrir - 0.5 &&
            n.humRiegoOn >= 0 && n.humRiegoOff <= 100 &&
            n.humRiegoOn <  n.humRiegoOff &&
            n.motorMaxS >= 5 && n.motorMaxS <= 600 &&
            n.riegoMaxMin >= 1 && n.riegoMaxMin <= 120 &&
            n.riegoPausaMin <= 1440;
  if (!ok) { alarma("config: valor rechazado para " + clave); return false; }

  cfg = n;
  guardarConfig();
  publicarConfig();
  Serial.printf("[CONFIG] %s = %.1f\n", clave.c_str(), v);
  return true;
}

void alRecibir(char* topic, byte* payload, unsigned int largo) {
  String tp(topic);
  String msg;
  for (unsigned int i = 0; i < largo; i++) msg += (char)payload[i];
  msg.trim(); msg.toLowerCase();
  Serial.printf("[MQTT] <- %s : %s\n", topic, msg.c_str());

  String base = t("cmd/");
  if (!tp.startsWith(base)) return;
  String cmd = tp.substring(base.length());   // ej: "venteo", "config/temp_abrir"

  if (cmd == "modo") {
    if (msg == "auto" || msg == "manual") {
      modoAuto = (msg == "auto");
      guardarConfig();
      hayCambios = true;
      if (modoAuto) controlAuto();
    }
  }
  else if (cmd == "venteo") {
    if (msg == "parar") { venteoParar(); hayCambios = true; }   // PARAR siempre vale
    else if (modoAuto) alarma("venteo: orden ignorada, esta en automatico");
    else if (msg == "abrir")  venteoAbrir();    // en manual tambien saca del ERROR
    else if (msg == "cerrar") venteoCerrar();
  }
  else if (cmd == "riego") {
    if (msg == "off") riegoSet(false, "orden");              // APAGAR siempre vale
    else if (modoAuto) alarma("riego: orden ignorada, esta en automatico");
    else if (msg == "on") riegoSet(true, "orden manual");
  }
  else if (cmd.startsWith("config/")) {
    aplicarConfig(cmd.substring(7), msg.toFloat());
  }
  else if (cmd == "estado") {             // la app pide el estado al instante
    publicarEstado();
    publicarConfig();
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
    mqtt.subscribe(t("cmd/#").c_str(), 1);
    publicarConfig();
    publicarEstado();
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

void portalAbrir(const char* motivo) {
  if (wm.getConfigPortalActive()) return;
  Serial.printf("[PORTAL] abriendo red \"%s\" (%s). Conectate con el celular.\n", PORTAL_NOMBRE, motivo);
  wm.startConfigPortal(PORTAL_NOMBRE, PORTAL_CLAVE);
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
  wm.setTitle("Vivero DoMoVil");
  wm.setConfigPortalBlocking(false);         // clave: no frena el loop
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.setConnectTimeout(10);                  // al probar un WiFi nuevo, espera como mucho 10 s
  wm.setShowInfoUpdate(false);               // sin boton de actualizar firmware desde el portal

  pinMode(PIN_BOTON_PORTAL, INPUT_PULLUP);

  // Prueba el WiFi guardado; si no hay o no conecta, abre el portal (y sigue de largo)
  if (wm.autoConnect(PORTAL_NOMBRE, PORTAL_CLAVE)) Serial.println("[WIFI] conectado con la red guardada");
  else Serial.printf("[PORTAL] sin WiFi conocido: red \"%s\" abierta. Conectate con el celular.\n", PORTAL_NOMBRE);
}

void portalTick() {
  wm.process();               // atiende la pagina del portal si esta abierta

  // Boton BOOT apretado 5 s -> abrir portal (para cambiar de WiFi o de broker)
  static unsigned long apretadoDesde = 0;
  if (digitalRead(PIN_BOTON_PORTAL) == LOW) {
    if (apretadoDesde == 0) apretadoDesde = millis();
    else if (millis() - apretadoDesde > BOTON_PORTAL_MS) { portalAbrir("boton BOOT"); apretadoDesde = 0; }
  } else apretadoDesde = 0;

  // Mucho tiempo sin WiFi (cambiaron el router, la clave...) -> abrir portal solo
  if (WiFi.status() == WL_CONNECTED) sinWifiDesde = 0;
  else if (sinWifiDesde == 0) sinWifiDesde = millis();
  else if (millis() - sinWifiDesde > PORTAL_REABRIR_MIN * 60000UL) { portalAbrir("sin WiFi hace rato"); sinWifiDesde = millis(); }
}
#endif

// -------------------------------------------------------------
//  Setup y loop
// -------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Controlador de vivero " NODO_ID " ===");

  // 1) Lo primero: todas las salidas APAGADAS
  pinMode(PIN_VENTEO_ABRIR, OUTPUT);
  pinMode(PIN_VENTEO_CERRAR, OUTPUT);
  pinMode(PIN_RIEGO, OUTPUT);
  motorParar();
  escribirRele(PIN_RIEGO, false);

  pinMode(PIN_FIN_ABIERTO, INPUT_PULLUP);
  pinMode(PIN_FIN_CERRADO, INPUT_PULLUP);

  // 2) Config guardada (o valores por defecto la primera vez)
#if defined(ESP8266)
  if (!Prefs8266::iniciar()) Serial.println("[CONFIG] no se pudo montar la flash: uso valores por defecto");
#endif
  cargarConfig();
  Serial.printf("[CONFIG] abrir %.1f / cerrar %.1f °C, riego %.0f-%.0f %%, modo %s\n",
                cfg.tempAbrir, cfg.tempCerrar, cfg.humRiegoOn, cfg.humRiegoOff,
                modoAuto ? "auto" : "manual");

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
  mqtt.setBufferSize(512);
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
