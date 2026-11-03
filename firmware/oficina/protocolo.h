// =============================================================
//  protocolo.h  -  Como hablan por LoRa el hibernaculo y la oficina
//
//  ESTE ARCHIVO TIENE QUE SER IGUAL en firmware/hibernaculo/ y en
//  firmware/oficina/. Si se cambia uno, copiarlo al otro.
//
//  Mensajes de texto cortos, separados por "|". Cortos a proposito:
//  en LoRa cada byte tarda en salir, y mientras uno habla nadie mas puede.
//
//  Del hibernaculo a la oficina:
//    SP|S|<nodo>|<seq>|<temp>|<hum>|<sensorOk>|<venteo>|<riego>|<auto>|<rssiUlt>   estado
//    SP|C|<nodo>|<seq>|<tAbrir>|<tCerrar>|<hOn>|<hOff>|<recorrido>|<riegoMax>|<pausa> config
//    SP|A|<nodo>|<seq>|<texto>                                                    alarma
//    SP|N|<nodo>|<seq>|<nombre>                                                   nombre
//    SP|K|<nodo>|<seq>                                                            "recibi tu orden <seq>"
//
//  De la oficina al hibernaculo:
//    SP|M|<nodo>|<seq>|<orden>|<valor>      orden (igual que los topics MQTT: modo, venteo,
//                                           riego, config/temp_abrir, estado...)
//  El hibernaculo contesta SP|K|<nodo>|<seq>. Si la oficina no recibe la K,
//  reintenta. Si el hibernaculo recibe dos veces la misma <seq>, la ignora.
// =============================================================
#pragma once

#define LORA_FRECUENCIA   433175000UL   // Hz. Los dos equipos tienen que tener la MISMA
#define LORA_SF           9             // factor de dispersion: mas alto = mas alcance, mas lento
#define LORA_BW           125000UL      // ancho de banda
#define LORA_CR           5             // codificacion 4/5
#define LORA_SYNC         0x5A          // "palabra de red": ignora paquetes LoRa de otros equipos
#define LORA_POTENCIA_DBM 17            // potencia de salida
#define LORA_RED          "SP"          // prefijo de todos nuestros mensajes

// Estados del venteo, en numero para que el mensaje sea corto
enum : uint8_t { VT_DESCONOCIDO = 0, VT_CERRADO = 1, VT_ABIERTO = 2, VT_ABRIENDO = 3, VT_CERRANDO = 4, VT_ERROR = 5 };

static inline const char* nombreVenteoCodigo(int c) {
  switch (c) {
    case VT_CERRADO:  return "cerrado";
    case VT_ABIERTO:  return "abierto";
    case VT_ABRIENDO: return "abriendo";
    case VT_CERRANDO: return "cerrando";
    case VT_ERROR:    return "error";
    default:          return "desconocido";
  }
}
