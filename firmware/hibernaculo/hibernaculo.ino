// =============================================================
//  Hibernaculo - ESP32 con LoRa
//  Temperatura/humedad (DHT22), venteo con actuador lineal (por tiempo,
//  los finales de carrera estan adentro del actuador), riego, pantalla
//  LCD 20x4, palanca AUTO/MANUAL, pulsadores y LEDs.
//
//  No usa WiFi: le habla por LoRa a la oficina, y la oficina lo sube
//  a internet. Si se corta el LoRa, el hibernaculo sigue funcionando solo.
//  En todo el loop no hay delay(): nada frena la logica.
// =============================================================
#include <SPI.h>
#include <LoRa.h>                  // libreria "LoRa" de Sandeep Mistry
#include <Wire.h>
#include <LiquidCrystal_I2C.h>     // libreria "LiquidCrystal I2C" de Frank de Brabander
#include <DHT.h>
#include <Preferences.h>

#include "config.h"
#include "protocolo.h"

// -------------------------------------------------------------
//  Entradas con antirrebote (igual que en el vivero)
// -------------------------------------------------------------
class Entrada {
  uint8_t pin;
  bool crudo, estable;
  unsigned long cambio = 0;
public:
  explicit Entrada(uint8_t p) : pin(p), crudo(HIGH), estable(HIGH) {}
  void begin() { pinMode(pin, INPUT_PULLUP); crudo = estable = digitalRead(pin); cambio = millis(); }
  bool actualizar() {                     // true solo cuando se acepta un cambio
    bool r = digitalRead(pin);
    if (r != crudo) { crudo = r; cambio = millis(); }
    if (estable != crudo && millis() - cambio >= ANTIRREBOTE_MS) { estable = crudo; return true; }
    return false;
  }
  bool activo() const { return estable == LOW; }
  bool apretado() { return actualizar() && activo(); }  // flanco: recien apretado
};

Entrada palancaManual(PIN_PALANCA_MANUAL), palancaAuto(PIN_PALANCA_AUTO);
Entrada botonRiego(PIN_BOTON_RIEGO), botonVenteo(PIN_BOTON_VENTEO);

// -------------------------------------------------------------
//  Estado
// -------------------------------------------------------------
struct Config {
  float    tempAbrir, tempCerrar, humRiegoOn, humRiegoOff;
  uint16_t recorridoS, riegoMaxMin, riegoPausaMin;
};
Config cfg;
Preferences prefs;
DHT dht(PIN_DHT, DHT22);
LiquidCrystal_I2C lcd(LCD_DIRECCION, 20, 4);

bool    modoAuto = true;
uint8_t venteo = VT_DESCONOCIDO;
bool    riegoOn = false;
unsigned long riegoDesde = 0, riegoFinUltimo = 0; bool riegoAlgunaVez = false;
float   temp = NAN, hum = NAN; bool sensorOk = false; uint8_t fallosSensor = 0;

bool loraOk = false;
uint16_t seqSalida = 0;          // numero de cada mensaje que mandamos
long     ultSeqOrden = -1;       // ultima orden recibida (para no aplicarla dos veces)
int      rssiUltimo = 0;         // senal del ultimo paquete recibido de la oficina
unsigned long ultOrdenRecibida = 0;

bool hayCambios = false;         // algo cambio -> mandar estado pronto
unsigned long ultLectura = 0, ultEstado = 0, ultNombre = 0, ultEnvio = 0, proximoEstado = 0;
String ultimaAlarma; unsigned long alarmaDesde = 0;

// -------------------------------------------------------------
//  Memoria (sobrevive a cortes de luz)
// -------------------------------------------------------------
void cargarConfig() {
  prefs.begin("hiber", true);
  cfg.tempAbrir     = prefs.getFloat("tAbrir",  DEF_TEMP_ABRIR);
  cfg.tempCerrar    = prefs.getFloat("tCerrar", DEF_TEMP_CERRAR);
  cfg.humRiegoOn    = prefs.getFloat("hOn",     DEF_HUM_RIEGO_ON);
  cfg.humRiegoOff   = prefs.getFloat("hOff",    DEF_HUM_RIEGO_OFF);
  cfg.recorridoS    = prefs.getUShort("recorr", DEF_RECORRIDO_S);
  cfg.riegoMaxMin   = prefs.getUShort("rMax",   DEF_RIEGO_MAX_MIN);
  cfg.riegoPausaMin = prefs.getUShort("rPausa", DEF_RIEGO_PAUSA_MIN);
  modoAuto          = prefs.getBool("auto", true);
  prefs.end();
}
void guardarConfig() {
  prefs.begin("hiber", false);
  prefs.putFloat("tAbrir", cfg.tempAbrir);   prefs.putFloat("tCerrar", cfg.tempCerrar);
  prefs.putFloat("hOn", cfg.humRiegoOn);     prefs.putFloat("hOff", cfg.humRiegoOff);
  prefs.putUShort("recorr", cfg.recorridoS); prefs.putUShort("rMax", cfg.riegoMaxMin);
  prefs.putUShort("rPausa", cfg.riegoPausaMin);
  prefs.putBool("auto", modoAuto);
  prefs.end();
}

// -------------------------------------------------------------
//  LoRa: mandar
// -------------------------------------------------------------
void loraMandar(const String& msg) {
  if (!loraOk) return;
  LoRa.beginPacket();
  LoRa.print(msg);
  LoRa.endPacket();               // tarda unos cientos de ms (lo que dura la transmision)
  ultEnvio = millis();
  Serial.println("[LORA] -> " + msg);
}
String cabecera(char tipo) {
  return String(LORA_RED "|") + tipo + "|" + NODO_NUM + "|" + (seqSalida++) + "|";
}
String n1(float v) { return isnan(v) ? String("x") : String(v, 1); }

void mandarEstado() {
  loraMandar(cabecera('S') + n1(temp) + "|" + n1(hum) + "|" + (sensorOk ? 1 : 0) + "|" +
             venteo + "|" + (riegoOn ? 1 : 0) + "|" + (modoAuto ? 1 : 0) + "|" + rssiUltimo);
  ultEstado = millis();
  proximoEstado = INTERVALO_ESTADO_MS + random(0, 4000);   // un poco al azar: si hay varios, no se pisan
  hayCambios = false;
}
void mandarConfig() {
  loraMandar(cabecera('C') + String(cfg.tempAbrir, 1) + "|" + String(cfg.tempCerrar, 1) + "|" +
             String(cfg.humRiegoOn, 0) + "|" + String(cfg.humRiegoOff, 0) + "|" + cfg.recorridoS + "|" +
             cfg.riegoMaxMin + "|" + cfg.riegoPausaMin);
}
void mandarNombre() { loraMandar(cabecera('N') + NODO_NOMBRE); ultNombre = millis(); }

void alarma(const String& texto) {
  Serial.println("[ALARMA] " + texto);
  ultimaAlarma = texto; alarmaDesde = millis();
  loraMandar(cabecera('A') + texto);
}

// -------------------------------------------------------------
//  Reles
// -------------------------------------------------------------
void escribirRele(uint8_t pin, bool on) { digitalWrite(pin, (on == RELE_ACTIVO_ALTO) ? HIGH : LOW); }

// -------------------------------------------------------------
//  Venteo por tiempo (actuador lineal con finales internos)
//  Abrir = rele ABRIR prendido durante "recorrido" segundos.
//  El actuador se corta solo al llegar; nosotros cortamos el rele un
//  rato despues para no dejarlo alimentado de mas.
// -------------------------------------------------------------
int arranquePin = -1; unsigned long arranqueDesde = 0, movDesde = 0;

void motorParar() { escribirRele(PIN_VENTEO_ABRIR, false); escribirRele(PIN_VENTEO_CERRAR, false); arranquePin = -1; }
void motorArrancar(uint8_t pin, bool invertir) {
  motorParar();
  if (invertir) { arranquePin = pin; arranqueDesde = millis(); }   // espera el tiempo muerto
  else escribirRele(pin, true);
  movDesde = millis() + (invertir ? TIEMPO_MUERTO_MS : 0);
}
void setVenteo(uint8_t e) {
  if (e == venteo) return;
  venteo = e; hayCambios = true;
  Serial.printf("[VENTEO] %s\n", nombreVenteoCodigo(e));
}
bool moviendo() { return venteo == VT_ABRIENDO || venteo == VT_CERRANDO; }
void venteoAbrir()  { if (venteo == VT_ABIERTO || venteo == VT_ABRIENDO) return; motorArrancar(PIN_VENTEO_ABRIR,  venteo == VT_CERRANDO); setVenteo(VT_ABRIENDO); }
void venteoCerrar() { if (venteo == VT_CERRADO || venteo == VT_CERRANDO) return; motorArrancar(PIN_VENTEO_CERRAR, venteo == VT_ABRIENDO); setVenteo(VT_CERRANDO); }
void venteoParar()  { bool m = moviendo(); motorParar(); if (m) setVenteo(VT_DESCONOCIDO); }   // quedo a mitad de camino

void venteoTick() {
  if (arranquePin >= 0 && millis() - arranqueDesde >= TIEMPO_MUERTO_MS) { escribirRele((uint8_t)arranquePin, true); arranquePin = -1; }
  if (!moviendo() || (long)(millis() - movDesde) < 0) return;
  if (millis() - movDesde >= (unsigned long)cfg.recorridoS * 1000UL) {
    motorParar();
    setVenteo(venteo == VT_ABRIENDO ? VT_ABIERTO : VT_CERRADO);
  }
}

// -------------------------------------------------------------
//  Riego
// -------------------------------------------------------------
void riegoSet(bool on, const char* motivo) {
  if (on == riegoOn) return;
  riegoOn = on; escribirRele(PIN_RIEGO, on);
  if (on) riegoDesde = millis(); else { riegoFinUltimo = millis(); riegoAlgunaVez = true; }
  Serial.printf("[RIEGO] %s (%s)\n", on ? "ON" : "OFF", motivo);
  hayCambios = true;
}
void riegoTick() {
  if (riegoOn && millis() - riegoDesde > (unsigned long)cfg.riegoMaxMin * 60000UL) {
    riegoSet(false, "tiempo maximo"); alarma("riego: se corto por tiempo maximo");
  }
}

// -------------------------------------------------------------
//  Sensor y logica automatica (con histeresis)
// -------------------------------------------------------------
void leerSensor() {
  float tt = dht.readTemperature(), hh = dht.readHumidity();
  if (isnan(tt) || isnan(hh)) {
    if (fallosSensor < 255) fallosSensor++;
    if (fallosSensor >= FALLOS_SENSOR_MAX && sensorOk) { sensorOk = false; temp = hum = NAN; alarma("sensor sin lectura"); hayCambios = true; }
    return;
  }
  fallosSensor = 0; temp = tt; hum = hh;
  if (!sensorOk) { sensorOk = true; hayCambios = true; }
}

void controlAuto() {
  if (!modoAuto) return;
  if (!sensorOk) { riegoSet(false, "sensor caido"); return; }
  if (temp >= cfg.tempAbrir && venteo != VT_ABIERTO && venteo != VT_ABRIENDO) venteoAbrir();
  else if (temp <= cfg.tempCerrar && venteo != VT_CERRADO && venteo != VT_CERRANDO) venteoCerrar();
  bool pausaOk = !riegoAlgunaVez || millis() - riegoFinUltimo >= (unsigned long)cfg.riegoPausaMin * 60000UL;
  if (!riegoOn && hum <= cfg.humRiegoOn && pausaOk) riegoSet(true, "auto");
  else if (riegoOn && hum >= cfg.humRiegoOff)      riegoSet(false, "auto");
}

void setModo(bool aAuto, const char* origen) {
  if (modoAuto == aAuto) return;          // ya estaba: no hace nada
  modoAuto = aAuto; guardarConfig(); hayCambios = true;
  Serial.printf("[MODO] %s (%s)\n", aAuto ? "automatico" : "manual", origen);
  if (aAuto) controlAuto();
}

// -------------------------------------------------------------
//  Ordenes (las mismas que en el vivero: llegan de la app por la oficina)
// -------------------------------------------------------------
bool aplicarConfig(const String& k, float v) {
  Config n = cfg;
  if      (k == "temp_abrir")      n.tempAbrir = v;
  else if (k == "temp_cerrar")     n.tempCerrar = v;
  else if (k == "hum_riego_on")    n.humRiegoOn = v;
  else if (k == "hum_riego_off")   n.humRiegoOff = v;
  else if (k == "motor_max_s")     n.recorridoS = (uint16_t)v;    // en el hibernaculo = tiempo de recorrido
  else if (k == "riego_max_min")   n.riegoMaxMin = (uint16_t)v;
  else if (k == "riego_pausa_min") n.riegoPausaMin = (uint16_t)v;
  else { alarma("config: clave desconocida " + k); return false; }
  bool ok = n.tempAbrir >= -10 && n.tempAbrir <= 60 && n.tempCerrar >= -10 && n.tempCerrar <= 60 &&
            n.tempCerrar < n.tempAbrir - 0.5 && n.humRiegoOn >= 0 && n.humRiegoOff <= 100 &&
            n.humRiegoOn < n.humRiegoOff && n.recorridoS >= 5 && n.recorridoS <= 600 &&
            n.riegoMaxMin >= 1 && n.riegoMaxMin <= 120 && n.riegoPausaMin <= 1440;
  if (!ok) { alarma("config: valor rechazado para " + k); return false; }
  cfg = n; guardarConfig(); mandarConfig();
  return true;
}

void aplicarOrden(const String& orden, String valor) {
  valor.trim(); valor.toLowerCase();
  Serial.printf("[ORDEN] %s = %s\n", orden.c_str(), valor.c_str());
  if (orden == "modo") { if (valor == "auto") setModo(true, "app"); else if (valor == "manual") setModo(false, "app"); }
  else if (orden == "venteo") {
    if (valor == "parar") { venteoParar(); hayCambios = true; }
    else if (modoAuto) alarma("venteo: orden ignorada, esta en automatico");
    else if (valor == "abrir") venteoAbrir();
    else if (valor == "cerrar") venteoCerrar();
  }
  else if (orden == "riego") {
    if (valor == "off") riegoSet(false, "orden");
    else if (modoAuto) alarma("riego: orden ignorada, esta en automatico");
    else if (valor == "on") riegoSet(true, "orden");
  }
  else if (orden.startsWith("config/")) aplicarConfig(orden.substring(7), valor.toFloat());
  else if (orden == "estado") { hayCambios = true; mandarConfig(); }
}

// Parte un mensaje "a|b|c" en pedazos
int partir(const String& s, String* out, int max) {
  int n = 0, ini = 0;
  for (int i = 0; i <= (int)s.length() && n < max; i++)
    if (i == (int)s.length() || s[i] == '|') { out[n++] = s.substring(ini, i); ini = i + 1; }
  return n;
}

void loraTick() {
  if (!loraOk) return;
  int tam = LoRa.parsePacket();
  if (!tam) return;
  String msg; while (LoRa.available()) msg += (char)LoRa.read();
  int rssi = LoRa.packetRssi();
  String p[6]; int n = partir(msg, p, 6);
  // SP|M|<nodo>|<seq>|<orden>|<valor>
  if (n < 6 || p[0] != LORA_RED || p[1] != "M" || p[2].toInt() != NODO_NUM) return;   // no es para mi
  rssiUltimo = rssi; ultOrdenRecibida = millis();
  long seq = p[3].toInt();
  loraMandar(String(LORA_RED "|K|") + NODO_NUM + "|" + seq);    // confirmo que la recibi
  if (seq == ultSeqOrden) return;                               // repetida: ya la habia aplicado
  ultSeqOrden = seq;
  aplicarOrden(p[4], p[5]);
}

// -------------------------------------------------------------
//  Mandos fisicos
// -------------------------------------------------------------
void mandosTick() {
  if (palancaManual.apretado()) setModo(false, "palanca");
  if (palancaAuto.apretado())   setModo(true,  "palanca");
  if (botonRiego.apretado()) {
    if (modoAuto) alarma("pulsador RIEGO ignorado: esta en automatico");
    else riegoSet(!riegoOn, "pulsador");
  }
  if (botonVenteo.apretado()) {
    if (modoAuto) alarma("pulsador VENTEO ignorado: esta en automatico");
    else if (moviendo())          venteoParar();
    else if (venteo == VT_ABIERTO) venteoCerrar();
    else                           venteoAbrir();      // cerrado o a mitad de camino: abre
  }
}

// -------------------------------------------------------------
//  LEDs y pantalla
// -------------------------------------------------------------
void ledsTick() {
  bool titila = (millis() / 400) % 2;
  digitalWrite(PIN_LED_AUTO,   modoAuto);
  digitalWrite(PIN_LED_MANUAL, !modoAuto);
  digitalWrite(PIN_LED_RIEGO,  riegoOn);
  digitalWrite(PIN_LED_VENTEO, venteo == VT_ABIERTO || (moviendo() && titila));
}

String a20(String s) { if (s.length() > 20) s = s.substring(0, 20); while (s.length() < 20) s += ' '; return s; }
String lineas[4];

void pantallaTick() {
  static unsigned long ult = 0;
  if (millis() - ult < 500) return;
  ult = millis();
  String l[4];
  // Linea 1: modo + temperatura + humedad   "AUTOMATICO 23.4C 81%"
  String med = sensorOk ? (String(temp, 1) + "C " + String((int)round(hum)) + "%") : String("SENSOR --");
  String modo = modoAuto ? "AUTOMATICO" : "MANUAL";
  String l1 = modo; while (l1.length() + med.length() < 20) l1 += ' '; l[0] = l1 + med;
  // Linea 2: venteo
  String v = nombreVenteoCodigo(venteo); v.toUpperCase();
  if (moviendo() && (long)(millis() - movDesde) >= 0) {
    long falta = (long)cfg.recorridoS - (long)((millis() - movDesde) / 1000);
    v += " " + String(falta > 0 ? falta : 0) + "s";
  }
  l[1] = "Venteo: " + v;
  // Linea 3: riego
  if (riegoOn) { unsigned long s = (millis() - riegoDesde) / 1000; char b[16]; snprintf(b, sizeof(b), "%02lu:%02lu", (s / 60) % 100, s % 60); l[2] = String("Riego:  REGANDO ") + b; }
  else l[2] = "Riego:  APAGADO";
  // Linea 4: alarma reciente (30 s) o estado de la radio
  if (ultimaAlarma.length() && millis() - alarmaDesde < 30000) l[3] = "! " + ultimaAlarma;
  else if (!loraOk) l[3] = "LoRa: NO RESPONDE";
  else l[3] = String("Est.") + NODO_NUM + (rssiUltimo ? "  " + String(rssiUltimo) + "dBm" : String("  LoRa listo"));
  for (int i = 0; i < 4; i++) {
    String s = a20(l[i]);
    if (s != lineas[i]) { lcd.setCursor(0, i); lcd.print(s); lineas[i] = s; }   // solo reescribe lo que cambio
  }
}

// -------------------------------------------------------------
//  Setup y loop
// -------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.printf("\n=== Hibernaculo - estacion %d ===\n", NODO_NUM);

  // Salidas apagadas lo primero
  pinMode(PIN_VENTEO_ABRIR, OUTPUT); pinMode(PIN_VENTEO_CERRAR, OUTPUT); pinMode(PIN_RIEGO, OUTPUT);
  motorParar(); escribirRele(PIN_RIEGO, false);
  pinMode(PIN_LED_AUTO, OUTPUT); pinMode(PIN_LED_MANUAL, OUTPUT); pinMode(PIN_LED_RIEGO, OUTPUT); pinMode(PIN_LED_VENTEO, OUTPUT);

  palancaManual.begin(); palancaAuto.begin(); botonRiego.begin(); botonVenteo.begin();

  cargarConfig();
  Serial.printf("[CONFIG] abrir %.1f / cerrar %.1f C, riego %.0f-%.0f %%, recorrido %u s, modo %s\n",
                cfg.tempAbrir, cfg.tempCerrar, cfg.humRiegoOn, cfg.humRiegoOff, cfg.recorridoS, modoAuto ? "auto" : "manual");

  dht.begin();
  Wire.begin();
  lcd.init(); lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Hibernaculo  Est."); lcd.print(NODO_NUM);
  lcd.setCursor(0, 1); lcd.print("Iniciando...");

  // LoRa
  LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RST, -1);
  loraOk = LoRa.begin(LORA_FRECUENCIA);
  if (loraOk) {
    LoRa.setSpreadingFactor(LORA_SF); LoRa.setSignalBandwidth(LORA_BW); LoRa.setCodingRate4(LORA_CR);
    LoRa.setSyncWord(LORA_SYNC); LoRa.setTxPower(LORA_POTENCIA_DBM); LoRa.enableCrc();
    Serial.println("[LORA] listo");
  } else Serial.println("[LORA] NO RESPONDE: revisar cables SPI, NSS y alimentacion 3,3 V del Ra-02");

  randomSeed(esp_random());
  // Al arrancar no sabemos donde quedo la ventana: la llevamos a cerrado.
  // Es seguro porque el actuador se corta solo en el final. Si esta en auto y hace calor, despues abre.
  venteoCerrar();

  mandarNombre(); mandarConfig(); mandarEstado();
}

void loop() {
  unsigned long ahora = millis();
  if (ahora - ultLectura >= INTERVALO_LECTURA_MS) { ultLectura = ahora; leerSensor(); controlAuto(); }

  venteoTick();
  riegoTick();
  mandosTick();
  loraTick();
  ledsTick();
  pantallaTick();

  // Estado: cada ~20 s, o enseguida si algo cambio (sin pasar de 1 cada 2 s para no tapar la radio)
  if ((hayCambios && ahora - ultEnvio > 2000) || ahora - ultEstado >= proximoEstado) mandarEstado();
  if (ahora - ultNombre >= INTERVALO_NOMBRE_MS) { mandarNombre(); mandarConfig(); }
}
