// =============================================================
//  cultivos.h - Riego por cultivos (sectores)
//
//  Cada cultivo tiene su VALVULA (un rele) y su SENSOR DE SUELO.
//  Se riega DE A UNO POR VEZ, con UN SOLO caudalimetro en la cañeria
//  principal: los litros se le cuentan al cultivo que tiene la valvula
//  abierta. Si hay bomba (o valvula general), va en el rele de RIEGO y
//  se prende mientras cualquier cultivo riega.
//
//  Corte del riego, a eleccion por cultivo:
//    humedad: riega cuando el suelo baja de "on" % y corta al llegar a "off" %
//             (solo en riego automatico; a mano corta por litros/tiempo/"Cortar")
//    litros:  riega cuando baja de "on" % y corta al pasar "litros" L
//    ambos:   corta con lo que pase primero
//  Siempre: tiempo maximo de seguridad y MARCHA EN SECO (bomba andando y
//  el caudalimetro sin contar agua SIN_CAUDAL_S segundos = corta todo).
//
//  DEMO_CULTIVOS 1 simula el suelo y el caudal, para mostrarlo sin sensores.
//  Lo usa vivero.ino (este archivo va en la misma carpeta).
// =============================================================
#pragma once
#if N_CULTIVOS > 0

const uint8_t PIN_VALVULA[N_CULTIVOS] = PINES_VALVULAS;
const uint8_t PIN_SUELO[N_CULTIVOS]   = PINES_SUELO;

enum ModoCorte : uint8_t { CORTE_HUMEDAD = 0, CORTE_LITROS = 1, CORTE_AMBOS = 2 };
const char* nombreCorte(uint8_t m) { return m == CORTE_LITROS ? "litros" : m == CORTE_AMBOS ? "humedad o litros" : "humedad"; }

struct CfgCultivo {                  // lo que se ajusta desde la app (administrador)
  String   nombre;
  uint8_t  modo, humOn, humOff;      // humOn: riega debajo de; humOff: corta arriba de (%)
  uint16_t litros, maxMin;           // corte por litros; tiempo maximo de seguridad (min)
  bool     activo;                   // false = este cultivo no se riega solo
};
struct EstCultivo {
  float    suelo = NAN;              // humedad del suelo (%)
  bool     sensorOk = false;
  float    lHoy = 0, lMes = 0, lTotal = 0;
  uint32_t finUltimo = 0;            // millis() del ultimo riego (para la pausa)
  float    sumS = 0; uint16_t nS = 0;   // promedio para el historial (cada 10 min)
};
CfgCultivo cc[N_CULTIVOS];
EstCultivo ec[N_CULTIVOS];

int8_t   regando = -1;               // cultivo con la valvula abierta (-1 = ninguno)
unsigned long riegoCDesde = 0;       // millis() en que abrio
uint32_t riegoCInicio = 0;           // fecha y hora en que abrio (epoch)
float    litrosRiego = 0;            // litros del riego actual
String   riegoQuien;                 // quien lo largo (usuario, panel, automatico)
unsigned long ultimoAgua = 0;        // ultima vez que el caudalimetro conto algo
volatile uint32_t pulsos = 0;
uint32_t pulsosLeidos = 0;
float    caudalLmin = 0, litrosSinValvula = 0;
uint32_t diaActual = 0;              // aaaammdd, para poner "hoy" y "mes" en cero
bool     cultivosCambio = false;
uint16_t riegosLineas = 0;
const char* RIEGOS_ACTUAL   = "/rie0.txt";
const char* RIEGOS_ANTERIOR = "/rie1.txt";

void IRAM_ATTR alPulso() { pulsos++; }   // cada vuelta de la turbina del caudalimetro

// ---------- memoria (sobrevive a cortes de luz) ----------
void cultivosGuardarCfg() {
  prefs.begin("cultivos", false);
  for (uint8_t i = 0; i < N_CULTIVOS; i++) {
    String v = cc[i].nombre + "|" + cc[i].modo + "|" + cc[i].humOn + "|" + cc[i].humOff + "|" +
               cc[i].litros + "|" + cc[i].maxMin + "|" + (cc[i].activo ? 1 : 0);
    prefs.putString(("c" + String(i)).c_str(), v.c_str());
  }
  prefs.end();
}
void cultivosGuardarLitros() {
  prefs.begin("cultivos", false);
  for (uint8_t i = 0; i < N_CULTIVOS; i++) {
    String v = String(ec[i].lHoy, 1) + "|" + String(ec[i].lMes, 1) + "|" + String(ec[i].lTotal, 1);
    prefs.putString(("l" + String(i)).c_str(), v.c_str());
  }
  prefs.putString("dia", String(diaActual).c_str());
  prefs.end();
}
String campo(const String& s, uint8_t n) {       // campo n de "a|b|c"
  int desde = 0;
  for (uint8_t i = 0; i < n; i++) { desde = s.indexOf('|', desde) + 1; if (desde <= 0) return ""; }
  int hasta = s.indexOf('|', desde);
  return hasta < 0 ? s.substring(desde) : s.substring(desde, hasta);
}
void cultivosCargar() {
  prefs.begin("cultivos", true);
  for (uint8_t i = 0; i < N_CULTIVOS; i++) {
    String c = prefs.getString(("c" + String(i)).c_str(), "");
    if (c.length()) cc[i] = { campo(c, 0), (uint8_t)campo(c, 1).toInt(), (uint8_t)campo(c, 2).toInt(), (uint8_t)campo(c, 3).toInt(),
                              (uint16_t)campo(c, 4).toInt(), (uint16_t)campo(c, 5).toInt(), campo(c, 6) == "1" };
    else cc[i] = { "Cultivo " + String(i + 1), CORTE_HUMEDAD, 35, 60, 50, 15, true };
    String l = prefs.getString(("l" + String(i)).c_str(), "");
    if (l.length()) { ec[i].lHoy = campo(l, 0).toFloat(); ec[i].lMes = campo(l, 1).toFloat(); ec[i].lTotal = campo(l, 2).toFloat(); }
  }
  diaActual = prefs.getString("dia", "0").toInt();
  prefs.end();
}

// ---------- publicaciones ----------
String numN(float v, uint8_t d) { return isnan(v) ? String("null") : String(v, (unsigned int)d); }
void publicarCultivos() {           // estado (retenido): lo que se ve en la app
  String j = "{\"r\":" + String(regando) + ",\"q\":" + String(caudalLmin, 1) + ",\"lr\":" + String(litrosRiego, 1) +
             ",\"rs\":" + String(regando >= 0 ? (millis() - riegoCDesde) / 1000 : 0) + ",\"lsv\":" + String(litrosSinValvula, 1) + ",\"c\":[";
  for (uint8_t i = 0; i < N_CULTIVOS; i++) {
    if (i) j += ',';
    j += "{\"s\":" + numN(ec[i].suelo, 1) + ",\"ok\":" + (ec[i].sensorOk ? "1" : "0") + ",\"lh\":" + String(ec[i].lHoy, 1) +
         ",\"lm\":" + String(ec[i].lMes, 1) + ",\"lt\":" + String(ec[i].lTotal, 1) + "}";
  }
  j += "]}";
  publicar(t("cultivos"), j, true);
  cultivosCambio = false;
}
void publicarCultivosCfg() {        // ajustes (retenido)
  String j = "[";
  for (uint8_t i = 0; i < N_CULTIVOS; i++) {
    if (i) j += ',';
    j += "{\"n\":\"" + cc[i].nombre + "\",\"m\":" + cc[i].modo + ",\"on\":" + cc[i].humOn + ",\"off\":" + cc[i].humOff +
         ",\"l\":" + cc[i].litros + ",\"t\":" + cc[i].maxMin + ",\"a\":" + (cc[i].activo ? 1 : 0) + "}";
  }
  j += "]";
  publicar(t("cultivos/config"), j, true);
}
void publicarRiegos() { publicarLineas(RIEGOS_ANTERIOR, RIEGOS_ACTUAL, t("riegos")); }

// ---------- valvulas y bomba ----------
void bombaSet(bool on) {
#if USAR_BOMBA_GENERAL
  escribirRele(PIN_RIEGO, on);
#endif
  riegoOn = on;                      // el estado general muestra "regando"
  hayCambios = true;
}

void cultivoCerrar(const String& motivo, const String& quien) {
  if (regando < 0) return;
  uint8_t i = regando;
  escribirRele(PIN_VALVULA[i], false);
  bombaSet(false);
  unsigned long dur = (millis() - riegoCDesde) / 1000;
  // Registro de riegos: inicio|fin|cultivo|litros|segundos|quien|motivo
  String linea = String(riegoCInicio) + "|" + String(ahoraEpoch()) + "|" + limpiar(cc[i].nombre, 16) + "|" +
                 String(litrosRiego, 1) + "|" + String(dur) + "|" + limpiar(quienTxt(riegoQuien), 20) + "|" + limpiar(motivo, 40);
  anotarLinea(RIEGOS_ACTUAL, RIEGOS_ANTERIOR, riegosLineas, linea);
  publicar(t("riegos/nuevo"), linea, false);
  registrar(quien.length() ? quien : String("automatico"),
            cc[i].nombre + ": termina de regar, " + String(litrosRiego, 1) + " L en " + String(dur / 60) + " min (" + motivo + ")");
  ec[i].finUltimo = millis();
  regando = -1;
  litrosRiego = 0;
  cultivosGuardarLitros();
  cultivosCambio = true;
}

void cultivoAbrir(uint8_t i, const String& quien, const String& motivo) {
  if (regando == (int8_t)i) return;
  if (regando >= 0) cultivoCerrar("pasa a otro cultivo", quien);   // de a uno por vez
  regando = i;
  riegoCDesde = millis();
  ultimoAgua = millis();             // desde aca cuentan los segundos sin caudal
  riegoCInicio = ahoraEpoch();
  riegoQuien = quien;
  litrosRiego = 0;
  escribirRele(PIN_VALVULA[i], true);
  bombaSet(true);
  registrar(quien, cc[i].nombre + ": empieza a regar (" + motivo + ")");
  cultivosCambio = true;
}

// ---------- sensores ----------
void cultivosLeerSuelo() {
  for (uint8_t i = 0; i < N_CULTIVOS; i++) {
#if DEMO_CULTIVOS
    // Simulacion: el suelo se seca solo (~3 % por minuto) y sube al regar
    if (isnan(ec[i].suelo)) ec[i].suelo = 40 + (i * 7) % 25;
    ec[i].suelo += (regando == (int8_t)i) ? 1.5f : -0.25f;
    ec[i].suelo = constrain(ec[i].suelo, 5.0f, 95.0f);
    ec[i].sensorOk = true;
#else
    uint32_t raw = 0;
    for (uint8_t k = 0; k < 8; k++) raw += analogRead(PIN_SUELO[i]);
    raw /= 8;
    bool ok = raw > 200 && raw < 4000;          // 0 o 4095 = sensor suelto o en corto
    if (ok != ec[i].sensorOk && !ok) alarma(cc[i].nombre + ": sensor de suelo sin lectura");
    ec[i].sensorOk = ok;
    ec[i].suelo = ok ? constrain(map((long)raw, SUELO_SECO_RAW, SUELO_MOJADO_RAW, 0, 1000) / 10.0f, 0.0f, 100.0f) : NAN;
#endif
    if (ec[i].sensorOk) { ec[i].sumS += ec[i].suelo; ec[i].nS++; }
  }
}

// Promedio de los ultimos 10 min para el historial (-128 = sin dato)
int8_t cultivoSueloProm(uint8_t i) {
  int8_t r = ec[i].nS ? (int8_t)lroundf(ec[i].sumS / ec[i].nS) : (int8_t)-128;
  ec[i].sumS = 0; ec[i].nS = 0;
  return r;
}

// Fecha local (UTC-3) como aaaammdd
uint32_t diaDe(uint32_t epoch) {
  time_t lt = epoch - 3 * 3600; struct tm tm; gmtime_r(&lt, &tm);
  return (tm.tm_year + 1900) * 10000UL + (tm.tm_mon + 1) * 100UL + tm.tm_mday;
}

// ---------- logica (se llama seguido desde loop) ----------
void cultivosTick() {
  static unsigned long ultSuelo = 0, ultSeg = 0, ultPub = 0;
  unsigned long ahora = millis();

  // Litros: cada 1 s se pasan los pulsos a litros
  if (ahora - ultSeg >= 1000) {
    float dt = (ahora - ultSeg) / 1000.0f;
    ultSeg = ahora;
#if DEMO_CULTIVOS
    if (regando >= 0) pulsos += (uint32_t)(12.0f / 60.0f * PULSOS_POR_LITRO * dt);   // simula 12 L/min
#endif
    noInterrupts(); uint32_t p = pulsos; interrupts();
    float litros = (p - pulsosLeidos) / PULSOS_POR_LITRO;
    pulsosLeidos = p;
    caudalLmin = litros / dt * 60.0f;
    if (litros > 0.02f) {
      ultimoAgua = ahora;
      if (regando >= 0) {
        litrosRiego += litros;
        ec[regando].lHoy += litros; ec[regando].lMes += litros; ec[regando].lTotal += litros;
      } else {
        litrosSinValvula += litros;    // pasa agua sin ninguna valvula abierta: posible perdida
        static bool avisado = false;
        if (litrosSinValvula > 2 && !avisado) { avisado = true; alarma("caudal sin ninguna valvula abierta: posible perdida"); registrar("automatico", "detecta caudal sin valvula abierta (posible perdida)"); }
      }
    }

    // Cambio de dia / mes (con hora de internet)
    uint32_t e = ahoraEpoch();
    if (e) {
      uint32_t d = diaDe(e);
      if (diaActual && d != diaActual) {
        for (uint8_t i = 0; i < N_CULTIVOS; i++) { ec[i].lHoy = 0; if (d / 100 != diaActual / 100) ec[i].lMes = 0; }
        litrosSinValvula = 0;
        diaActual = d; cultivosGuardarLitros(); cultivosCambio = true;
      } else if (!diaActual) diaActual = d;
    }

    // Cortes del riego en curso
    if (regando >= 0) {
      uint8_t i = regando;
      CfgCultivo& c = cc[i];
      bool automatico = riegoQuien == "automatico";
      // En un riego a mano no se corta por humedad (el operario lo pidio igual): corta por litros,
      // tiempo maximo, falta de agua o "Cortar".
      bool porHum = automatico && c.modo != CORTE_LITROS, porLit = c.modo != CORTE_HUMEDAD;
      if (ahora - ultimoAgua > SIN_CAUDAL_S * 1000UL) {
        alarma(c.nombre + ": la bomba anda pero no pasa agua. Se corto (revisar tanque, valvula o caño)");
        cultivoCerrar("sin caudal " + String(SIN_CAUDAL_S) + " s", "automatico");
      }
      else if (ahora - riegoCDesde > c.maxMin * 60000UL) {
        alarma(c.nombre + ": riego cortado por tiempo maximo");
        cultivoCerrar("tiempo maximo", "automatico");
      }
      else if (porLit && litrosRiego >= c.litros) cultivoCerrar("llego a " + String(c.litros) + " L", "automatico");
      else if (porHum && ec[i].sensorOk && ec[i].suelo >= c.humOff) cultivoCerrar("suelo en " + String(ec[i].suelo, 0) + " %", "automatico");
      else if (porHum && c.modo == CORTE_HUMEDAD && !ec[i].sensorOk) cultivoCerrar("sensor de suelo caido", "automatico");
    }
  }

  // Suelo cada 5 s y, en automatico, decidir a quien regar (el mas seco primero)
  if (ahora - ultSuelo >= 5000) {
    ultSuelo = ahora;
    cultivosLeerSuelo();
    if (modoAuto && regando < 0) {
      int8_t elegido = -1;
      for (uint8_t i = 0; i < N_CULTIVOS; i++) {
        if (!cc[i].activo || !ec[i].sensorOk || ec[i].suelo > cc[i].humOn) continue;
        if (ec[i].finUltimo && ahora - ec[i].finUltimo < PAUSA_CULTIVO_MIN * 60000UL) continue;
        if (elegido < 0 || ec[i].suelo < ec[elegido].suelo) elegido = i;
      }
      if (elegido >= 0) cultivoAbrir(elegido, "automatico", "suelo en " + String(ec[elegido].suelo, 0) + " %, corta por " + nombreCorte(cc[elegido].modo));
    }
    cultivosCambio = true;
  }

  // Publicar: al cambiar algo importante o cada 30 s
  if ((cultivosCambio && ahora - ultPub > 2000) || ahora - ultPub > 30000) { ultPub = ahora; publicarCultivos(); }
}

// ---------- ordenes por MQTT ----------
// cmd/cultivo/<n>/regar   "on|off;quien"   (on solo en manual; off siempre)
// cmd/cultivo/<n>/config  "n=Lechuga,m=0,on=35,off=60,l=50,t=15,a=1;quien"  (administrador)
void ordenCultivo(const String& resto, const String& crudo) {
  int barra = resto.indexOf('/');
  if (barra < 1) return;
  int i = resto.substring(0, barra).toInt() - 1;              // en la app se numeran desde 1
  String que = resto.substring(barra + 1);
  if (i < 0 || i >= N_CULTIVOS) return;
  int sep = crudo.lastIndexOf(';');
  String valor = sep >= 0 ? crudo.substring(0, sep) : crudo, quien = sep >= 0 ? crudo.substring(sep + 1) : "";
  valor.trim(); quien.trim();

  if (que == "regar") {
    valor.toLowerCase();
    if (valor == "off") { if (regando == i) cultivoCerrar("cortado a mano", quien); }
    else if (valor == "on") {
      if (modoAuto) { alarma(cc[i].nombre + ": orden ignorada, esta en automatico"); registrar(quien, "pide regar " + cc[i].nombre + " (ignorado: automatico)"); }
      else cultivoAbrir(i, quien, "a mano");
    }
  }
  else if (que == "config") {
    if (!esAdmin(quien)) { registrar(quien, "rechazado: ajustes de " + cc[i].nombre + " (no es administrador)"); alarma("cultivos: solo un administrador cambia los ajustes"); return; }
    CfgCultivo n = cc[i];
    int desde = 0;
    while (desde < (int)valor.length()) {
      int coma = valor.indexOf(',', desde); if (coma < 0) coma = valor.length();
      String par = valor.substring(desde, coma); desde = coma + 1;
      int ig = par.indexOf('='); if (ig < 1) continue;
      String k = par.substring(0, ig), v = par.substring(ig + 1);
      if      (k == "n")   n.nombre = limpiar(v, 16);
      else if (k == "m")   n.modo = v.toInt();
      else if (k == "on")  n.humOn = v.toInt();
      else if (k == "off") n.humOff = v.toInt();
      else if (k == "l")   n.litros = v.toInt();
      else if (k == "t")   n.maxMin = v.toInt();
      else if (k == "a")   n.activo = v == "1";
    }
    bool ok = n.nombre.length() && n.modo <= 2 && n.humOn < n.humOff && n.humOff <= 100 &&
              n.litros >= 1 && n.litros <= 5000 && n.maxMin >= 1 && n.maxMin <= 240;
    if (!ok) { alarma("cultivos: valores rechazados para " + cc[i].nombre); registrar(quien, "rechazado: ajustes de " + cc[i].nombre); return; }
    cc[i] = n;
    cultivosGuardarCfg();
    publicarCultivosCfg();
    registrar(quien, "ajustes de " + n.nombre + ": corta por " + nombreCorte(n.modo) + ", riega <" + n.humOn + " % hasta " + n.humOff +
                     " % / " + n.litros + " L, max " + n.maxMin + " min" + (n.activo ? "" : " (desactivado)"));
  }
}

void cultivosIniciar() {
  for (uint8_t i = 0; i < N_CULTIVOS; i++) { pinMode(PIN_VALVULA[i], OUTPUT); escribirRele(PIN_VALVULA[i], false); }
  analogReadResolution(12);
  pinMode(PIN_CAUDAL, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_CAUDAL), alPulso, FALLING);
  cultivosCargar();
  if (fsOk) riegosLineas = contarLineas(RIEGOS_ACTUAL);
  Serial.printf("[CULTIVOS] %u cultivos%s\n", N_CULTIVOS, DEMO_CULTIVOS ? " (MODO DEMO: suelo y caudal simulados)" : "");
}

#endif
