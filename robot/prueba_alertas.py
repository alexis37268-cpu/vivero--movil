#!/usr/bin/env python3
# Prueba del robot con avisos REALES del SMN guardados en prueba/ (6/10/2026, CC BY 4.0 SMN).
# Uso: python3 robot/prueba_alertas.py
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
import alertas_smn as r

D = os.path.join(os.path.dirname(__file__), "prueba")
rss = open(os.path.join(D, "rss_excerpt.xml"), "rb").read()
local = lambda url: open(os.path.join(D, url.rsplit("/", 1)[1]), "rb").read()
AHORA = 1_791_300_000          # 7/10/2026 ~09:00 (Argentina): las alertas de ejemplo siguen vigentes

casos = [  # lugar, lat, lon, eventos esperados
    ("Rio Cuarto", -33.1235, -64.3493, ["Tormentas"]),
    ("Cordoba",    -31.4135, -64.1810, ["Tormentas"]),
    ("San Luis",   -33.3017, -66.3378, []),
]
fallas = 0
for lugar, lat, lon, esperado in casos:
    cache = {}
    alertas, nuevos = r.buscar_alertas(lat, lon, rss, local, cache)
    msg, cierre = r.armar_mensajes(alertas, AHORA)
    eventos = [a["evento"] for a in msg["alertas"]]
    ok = eventos == esperado
    fallas += not ok
    print(f"{'OK ' if ok else 'MAL'} {lugar:10} -> {eventos} niveles={[a['nivel'] for a in msg['alertas']]} cierre={cierre} (descargados {nuevos})")
    # segunda pasada: todo sale de la cache, no se descarga nada
    _, nuevos2 = r.buscar_alertas(lat, lon, rss, local, cache)
    if nuevos2: print("MAL: la cache no funciono"); fallas += 1

# punto en poligono: cuadrado simple
cuad = [(0, 0), (0, 1), (1, 1), (1, 0)]
assert r.dentro(0.5, 0.5, cuad) and not r.dentro(1.5, 0.5, cuad)
# vencida: con fecha posterior al "hasta" no queda nada
alertas, _ = r.buscar_alertas(-33.1235, -64.3493, rss, local, {})
msg, cierre = r.armar_mensajes(alertas, 1_900_000_000)
assert msg["alertas"] == [] and cierre == 0, "las vencidas tienen que desaparecer"
print("Vencidas: OK")
# durante la tormenta (7/10 06:00) el venteo tiene que cerrarse hasta el fin del aviso (08:59:59)
msg, cierre = r.armar_mensajes(alertas, 1_791_363_600)
assert cierre == 1_791_374_399, cierre
# 20 min antes de que empiece ya cierra (anticipo de 30 min); 1 h antes todavia no
assert r.armar_mensajes(alertas, 1_791_352_800 - 20 * 60)[1] == 1_791_374_399
assert r.armar_mensajes(alertas, 1_791_352_800 - 60 * 60)[1] == 0
print("Cierre del venteo: OK")
print("TODO OK" if not fallas else f"{fallas} FALLAS")
sys.exit(1 if fallas else 0)
