// =============================================================
//  prefs8266.h  -  "Preferences" para el ESP8266
//
//  El ESP32 trae la libreria Preferences para guardar datos que
//  sobreviven a un corte de luz. El ESP8266 no la tiene, asi que
//  esta clase hace lo mismo (con las mismas funciones) guardando
//  un archivito de texto por "espacio" en la flash (LittleFS).
//  Asi el resto del programa no cambia entre los dos chips.
// =============================================================
#pragma once
#if defined(ESP8266)
#include <LittleFS.h>
#include <map>

class Prefs8266 {
  std::map<String, String> datos;
  String archivo;
  bool   sucio = false, abierto = false;

  String leer(const char* k, const String& def) {
    auto it = datos.find(k);
    return it == datos.end() ? def : it->second;
  }
  void escribir(const char* k, const String& v) {
    if (leer(k, "\x01") != v) { datos[k] = v; sucio = true; }
  }

public:
  // Montar la flash una sola vez al arrancar (la formatea si esta vacia)
  static bool iniciar() { return LittleFS.begin(); }

  bool begin(const char* espacio, bool /*soloLectura*/ = false) {
    datos.clear(); sucio = false; abierto = true;
    archivo = String("/") + espacio + ".txt";
    File f = LittleFS.open(archivo, "r");
    if (!f) return true;                      // primera vez: sin datos
    while (f.available()) {
      String linea = f.readStringUntil('\n');
      int i = linea.indexOf('=');
      if (i > 0) datos[linea.substring(0, i)] = linea.substring(i + 1);
    }
    f.close();
    return true;
  }

  void end() {
    if (abierto && sucio) {
      File f = LittleFS.open(archivo, "w");
      if (f) { for (auto& kv : datos) f.print(kv.first + "=" + kv.second + "\n"); f.close(); }
    }
    abierto = false; sucio = false;
  }

  float    getFloat (const char* k, float def)    { String v = leer(k, ""); return v.length() ? v.toFloat() : def; }
  uint16_t getUShort(const char* k, uint16_t def) { String v = leer(k, ""); return v.length() ? (uint16_t)v.toInt() : def; }
  bool     getBool  (const char* k, bool def)     { String v = leer(k, ""); return v.length() ? v == "1" : def; }
  String   getString(const char* k, const String& def) { return leer(k, def); }

  void putFloat (const char* k, float v)    { escribir(k, String(v, 2)); }
  void putUShort(const char* k, uint16_t v) { escribir(k, String(v)); }
  void putBool  (const char* k, bool v)     { escribir(k, v ? "1" : "0"); }
  void putString(const char* k, const char* v) { escribir(k, String(v)); }
};
#endif
