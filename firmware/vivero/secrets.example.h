// =============================================================
//  secrets.example.h  -  PLANTILLA
//  1) Copiar este archivo como  secrets.h
//  2) Completar con los datos reales
//  secrets.h esta en .gitignore: nunca se sube a GitHub.
// =============================================================
#pragma once

// WiFi fijo: SOLO se usa en Wokwi (MODO_WOKWI 1). Ahi la red es "Wokwi-GUEST" sin clave.
// En el equipo real el WiFi se carga desde el portal con el celular.
#define WIFI_SSID  "Wokwi-GUEST"
#define WIFI_PASS  ""

// Usuario creado en EMQX -> Access Control -> Authentication.
// En el equipo real son los valores de arranque: se pueden cambiar desde el portal.
#define MQTT_USER  "vivero-esp"
#define MQTT_PASS  "PONER_LA_CLAVE_ACA"

// Clave de la red WiFi del portal.
//   ""          -> red ABIERTA: cualquiera con un celular la puede configurar (como un enchufe inteligente)
//   8 o mas car.-> red con contrasena
#define PORTAL_CLAVE  ""
