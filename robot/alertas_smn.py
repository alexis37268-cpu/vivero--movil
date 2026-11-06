#!/usr/bin/env python3
# =============================================================
#  Robot de alertas meteorologicas (corre en GitHub Actions cada 15 min)
#
#  1. Lee los avisos oficiales del SMN (feed CAP, datos abiertos CC BY 4.0).
#  2. Se queda solo con los que cubren la ubicacion del vivero
#     (cada aviso trae el poligono de la zona afectada).
#  3. Los publica en el broker MQTT, retenidos:
#       vivero/clima/alerta  -> JSON con las alertas vigentes (lo lee la app)
#       vivero/clima/cierre  -> hasta cuando cerrar el venteo (lo lee el ESP)
#
#  Solo usa la libreria estandar de Python, mas paho-mqtt para publicar.
#  Sin MQTT_USER/MQTT_PASS hace todo menos publicar (sirve para probar).
# =============================================================
import json, os, sys, time, ssl, urllib.request
import xml.etree.ElementTree as ET
from datetime import datetime, timezone

FEED      = "https://ssl.smn.gob.ar/CAP/AR.php"
ORIGEN    = "https://ssl.smn.gob.ar/"        # solo se siguen links de este sitio
AGENTE    = "SolPuntano-vivero/1.0 (control de vivero; github.com/alexis37268-cpu/vivero--movil)"
CAP_NS    = {"c": "urn:oasis:names:tc:emergency:cap:1.2"}
MAX_BYTES = 2_000_000                        # tope por descarga
CACHE     = os.environ.get("CACHE", "cache-alertas.json")

# Ubicacion del vivero (se cambia en GitHub: Settings > Variables)
LAT = float(os.environ.get("VIVERO_LAT") or -33.3017)   # por defecto: ciudad de San Luis
LON = float(os.environ.get("VIVERO_LON") or -66.3378)

# Avisos que hacen cerrar el venteo (la estructura sufre con viento y granizo)
CIERRAN = ("tormenta", "viento", "zonda", "granizo", "nevada", "nieve")
# El SMN no pone el color en el feed: se deduce de la severidad CAP
NIVEL = {"moderate": "amarillo", "severe": "naranja", "extreme": "rojo"}
ANTICIPO_S = 30 * 60                          # cerrar 30 min antes de que empiece


def bajar(url):
    req = urllib.request.Request(url, headers={"User-Agent": AGENTE})
    with urllib.request.urlopen(req, timeout=20, context=ssl.create_default_context()) as r:
        datos = r.read(MAX_BYTES + 1)
    if len(datos) > MAX_BYTES:
        raise ValueError("respuesta demasiado grande: " + url)
    if b"<!DOCTYPE" in datos[:500].upper():
        raise ValueError("el SMN devolvio HTML en vez de XML: " + url)
    return datos


def epoch(iso):
    return int(datetime.fromisoformat(iso).timestamp()) if iso else 0


def dentro(lat, lon, poligono):
    """Punto dentro de poligono (metodo del rayo). poligono = [(lat, lon), ...]"""
    adentro = False
    n = len(poligono)
    for i in range(n):
        la1, lo1 = poligono[i]
        la2, lo2 = poligono[(i + 1) % n]
        if (lo1 > lon) != (lo2 > lon):
            cruce = la1 + (lon - lo1) * (la2 - la1) / (lo2 - lo1)
            if lat < cruce:
                adentro = not adentro
    return adentro


def leer_cap(xml_bytes, lat, lon):
    """Devuelve la alerta si cubre el punto y sigue vigente; si no, None."""
    raiz = ET.fromstring(xml_bytes)
    if raiz.findtext("c:status", "", CAP_NS) != "Actual" or raiz.findtext("c:msgType", "", CAP_NS) == "Cancel":
        return None
    for info in raiz.findall("c:info", CAP_NS):
        poligonos = []
        for p in info.findall("c:area/c:polygon", CAP_NS):
            pts = [tuple(float(x) for x in par.split(",")) for par in (p.text or "").split()]
            if len(pts) >= 3:
                poligonos.append(pts)
        if not any(dentro(lat, lon, pol) for pol in poligonos):
            continue
        evento = info.findtext("c:event", "", CAP_NS).strip()
        sev = info.findtext("c:severity", "", CAP_NS).strip().lower()
        return {
            "id": raiz.findtext("c:identifier", "", CAP_NS),
            "evento": evento,
            "nivel": NIVEL.get(sev, "otro"),
            "desde": epoch(info.findtext("c:onset", "", CAP_NS) or raiz.findtext("c:sent", "", CAP_NS)),
            "hasta": epoch(info.findtext("c:expires", "", CAP_NS)),
            "texto": (info.findtext("c:description", "", CAP_NS) or "").strip(),
            "cierra": any(k in evento.lower() for k in CIERRAN),
        }
    return None


def buscar_alertas(lat, lon, rss_bytes, bajar_cap, cache):
    """Recorre el RSS. Cada aviso se descarga una sola vez (cache por link)."""
    canal = ET.fromstring(rss_bytes).find("channel")
    vistos, alertas, nuevos = set(), [], 0
    for item in canal.findall("item"):
        link = (item.findtext("guid") or item.findtext("link") or "").strip()
        if not link.startswith(ORIGEN) or link in vistos:
            continue
        vistos.add(link)
        if link not in cache:
            try:
                a = leer_cap(bajar_cap(link), lat, lon)
                cache[link] = a if a else False      # False = no nos afecta
                nuevos += 1
            except Exception as e:                   # un aviso roto no frena el resto
                print("  no se pudo leer", link, "->", e)
                continue
        if cache[link]:
            alertas.append(cache[link])
    for k in [k for k in cache if k not in vistos]:  # lo que ya no esta en el feed
        del cache[k]
    return alertas, nuevos


def armar_mensajes(alertas, ahora):
    vigentes = [a for a in alertas if a["hasta"] > ahora]
    vigentes.sort(key=lambda a: (a["desde"], a["evento"]))
    cierre = max([a["hasta"] for a in vigentes if a["cierra"] and a["desde"] - ANTICIPO_S <= ahora] or [0])
    alerta = {
        "actualizado": ahora, "lat": LAT, "lon": LON, "fuente": "SMN (CC BY 4.0)",
        "alertas": [{k: a[k] for k in ("id", "evento", "nivel", "desde", "hasta", "texto", "cierra")} for a in vigentes],
    }
    return alerta, cierre


def publicar(alerta, cierre):
    usuario, clave = os.environ.get("MQTT_USER"), os.environ.get("MQTT_PASS")
    if not usuario or not clave:
        print("Sin MQTT_USER / MQTT_PASS: no se publica (modo prueba).")
        return
    import paho.mqtt.client as mqtt
    host = os.environ.get("MQTT_HOST", "p64138d1.ala.us-east-1.emqxsl.com")
    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="vivero-robot-alertas")
    c.username_pw_set(usuario, clave)
    c.tls_set()                                      # TLS con los certificados del sistema
    c.connect(host, 8883, keepalive=30)
    c.loop_start()
    r1 = c.publish("vivero/clima/alerta", json.dumps(alerta, ensure_ascii=False), qos=1, retain=True)
    r2 = c.publish("vivero/clima/cierre", str(cierre), qos=1, retain=True)
    r1.wait_for_publish(10); r2.wait_for_publish(10)
    c.loop_stop(); c.disconnect()
    print("Publicado en el broker.")


def main():
    try:
        cache = json.load(open(CACHE))
    except Exception:
        cache = {}
    ahora = int(time.time())
    print(f"Vivero en {LAT}, {LON}")
    rss = bajar(FEED)
    alertas, nuevos = buscar_alertas(LAT, LON, rss, bajar, cache)
    print(f"Avisos nuevos descargados: {nuevos} (el resto estaba en cache)")
    alerta, cierre = armar_mensajes(alertas, ahora)
    for a in alerta["alertas"]:
        print(f"  ALERTA {a['nivel'].upper()}: {a['evento']} hasta {datetime.fromtimestamp(a['hasta'], timezone.utc).isoformat()}"
              + ("  (cierra venteo)" if a["cierra"] else ""))
    if not alerta["alertas"]:
        print("  Sin alertas para el vivero.")
    print("Cierre de venteo hasta:", cierre or "no")
    json.dump(cache, open(CACHE, "w"))
    publicar(alerta, cierre)


if __name__ == "__main__":
    sys.exit(main())
