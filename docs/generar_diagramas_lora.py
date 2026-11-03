# Genera los diagramas de conexion del hibernaculo y de la oficina (SVG).
# Uso: python3 generar_diagramas_lora.py   (despues se pasan a PNG)
import html

COL = {'alim': '#d32f2f', 'gnd': '#111111', 'sensor': '#2e7d32', 'lcd': '#6a1b9a', 'lora': '#ef6c00',
       'rele': '#1565c0', 'mando': '#b8860b', 'led': '#00838f'}

def diagrama(archivo, titulo, subtitulo, placa, sub_placa, filas, notas, avisos):
    # filas: (pin, color, componente, terminal)
    alto_fila = 34
    y0 = 170
    h = y0 + len(filas) * alto_fila + 60 + 150
    W = 1600
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{h}" viewBox="0 0 {W} {h}" font-family="Arial, Helvetica, sans-serif">',
         f'<rect width="{W}" height="{h}" fill="#fff"/>',
         f'<text x="60" y="62" font-size="32" font-weight="bold" fill="#1b2a20">{html.escape(titulo)}</text>',
         f'<text x="60" y="96" font-size="18" fill="#55645a">{html.escape(subtitulo)}</text>']
    alto_placa = len(filas) * alto_fila + 40
    s.append(f'<rect x="60" y="{y0-30}" width="260" height="{alto_placa}" rx="14" fill="#20303a"/>')
    s.append(f'<text x="190" y="{y0+10}" font-size="24" font-weight="bold" fill="#fff" text-anchor="middle">{html.escape(placa)}</text>')
    s.append(f'<text x="190" y="{y0+36}" font-size="15" fill="#cfd8dc" text-anchor="middle">{html.escape(sub_placa)}</text>')
    grupo_ant = None
    for i, (pin, col, comp, term) in enumerate(filas):
        y = y0 + i * alto_fila + 10
        c = COL[col]
        s.append(f'<text x="305" y="{y+6}" font-size="17" font-weight="bold" fill="#fff" text-anchor="end">{html.escape(pin)}</text>')
        s.append(f'<circle cx="320" cy="{y}" r="7" fill="#d9d9d9" stroke="#555" stroke-width="2"/>')
        s.append(f'<path d="M327 {y} H760" stroke="{c}" stroke-width="5" stroke-linecap="round"/>')
        s.append(f'<circle cx="760" cy="{y}" r="6" fill="{c}"/>')
        s.append(f'<text x="780" y="{y+6}" font-size="17" fill="#1b2a20"><tspan font-weight="bold" fill="{c}">{html.escape(comp)}</tspan>  {html.escape(term)}</text>')
    # notas
    yn = y0 + len(filas) * alto_fila + 40
    s.append(f'<rect x="60" y="{yn}" width="720" height="{30 + 26*len(notas)}" rx="10" fill="#f4f7f4" stroke="#cfd8d1" stroke-width="2"/>')
    for j, n in enumerate(notas):
        s.append(f'<text x="80" y="{yn+30+26*j}" font-size="16" fill="#1b2a20">{html.escape(n)}</text>')
    s.append(f'<rect x="800" y="{yn}" width="740" height="{30 + 26*len(avisos)}" rx="10" fill="#fff6e5" stroke="#f0d59a" stroke-width="2"/>')
    for j, n in enumerate(avisos):
        s.append(f'<text x="820" y="{yn+30+26*j}" font-size="16" font-weight="{"bold" if j == 0 else "normal"}" fill="#7a5200">{html.escape(n)}</text>')
    s.append('</svg>')
    open(archivo, 'w', encoding='utf-8').write('\n'.join(s))

diagrama('conexiones-hibernaculo.svg',
  'Vivero Sol Puntano — Hibernáculo (ESP32 + LoRa)',
  'ESP32 + DHT22 + LCD 20x4 I2C + LoRa Ra-02 433 MHz + 3 relés + palanca AUTO/MANUAL + 2 pulsadores + 4 LEDs',
  'ESP32', 'hibernáculo', [
    ('3V3', 'alim', 'DHT22', '+   y   LoRa Ra-02  3.3V (¡solo 3,3 V!)'),
    ('5V',  'alim', 'Módulo de relés', 'VCC   y   LCD  VCC'),
    ('GND', 'gnd',  'GND común', 'DHT −, relés GND, LCD GND, Ra-02 GND, palanca, pulsadores, LEDs'),
    ('GPIO 4',  'sensor', 'DHT22', 'out (datos)'),
    ('GPIO 21', 'lcd', 'LCD 20x4 I2C', 'SDA'),
    ('GPIO 22', 'lcd', 'LCD 20x4 I2C', 'SCL'),
    ('GPIO 18', 'lora', 'LoRa Ra-02', 'SCK'),
    ('GPIO 19', 'lora', 'LoRa Ra-02', 'MISO'),
    ('GPIO 23', 'lora', 'LoRa Ra-02', 'MOSI'),
    ('GPIO 5',  'lora', 'LoRa Ra-02', 'NSS'),
    ('GPIO 35', 'lora', 'LoRa Ra-02', 'DIO0 (cableado, el código no lo usa)'),
    ('GPIO 25', 'rele', 'Relé 1', 'IN1 → ABRIR venteo'),
    ('GPIO 26', 'rele', 'Relé 2', 'IN2 → CERRAR venteo'),
    ('GPIO 27', 'rele', 'Relé 3', 'IN3 → RIEGO'),
    ('GPIO 16', 'mando', 'Palanca sin retención', 'lado MANUAL (común a GND)'),
    ('GPIO 15', 'mando', 'Palanca sin retención', 'lado AUTOMÁTICO'),
    ('GPIO 17', 'mando', 'Pulsador RIEGO', '↔ GND'),
    ('GPIO 32', 'mando', 'Pulsador VENTEO', '↔ GND'),
    ('GPIO 33', 'led', 'LED AUTO', '+ resistencia 330 Ω → GND'),
    ('GPIO 13', 'led', 'LED MANUAL', '+ resistencia 330 Ω → GND'),
    ('GPIO 2',  'led', 'LED RIEGO', '+ resistencia 330 Ω → GND (también prende el LED de la placa)'),
    ('GPIO 14', 'led', 'LED VENTEO', '+ resistencia 330 Ω → GND (fijo = abierto, titila = moviéndose)'),
  ],
  ['Arduino IDE: placa ESP32 Dev Module. Abrir firmware/hibernaculo/hibernaculo.ino',
   'config.h: NODO_NUM (2 = primer hibernáculo) y NODO_NOMBRE',
   'Librerías: LoRa (Sandeep Mistry), LiquidCrystal I2C (Frank de Brabander), DHT sensor library',
   'Pulsadores y palanca sin resistencias: el ESP32 usa su pull-up interna'],
  ['Antes de enchufar:',
   'Ra-02 a 3,3 V. A 5 V se quema.',
   'Nunca transmitir sin la antena puesta: se daña el módulo.',
   'LCD a 5 V: su módulo I2C sube SDA/SCL a 5 V. Poner un conversor',
   'de nivel I2C, o sacarle las 2 resistencias pull-up al módulo.'])

diagrama('conexiones-oficina.svg',
  'Vivero Sol Puntano — Oficina (ESP8266 NodeMCU + LoRa)',
  'NodeMCU ESP8266 + LoRa Ra-02 433 MHz + LCD 16x2 I2C + LED de error. Sube los hibernáculos al broker por WiFi.',
  'NodeMCU', 'ESP8266 - oficina', [
    ('3V3', 'alim', 'LoRa Ra-02', '3.3V (¡solo 3,3 V!)'),
    ('VIN (5 V)', 'alim', 'LCD 16x2', 'VCC'),
    ('GND', 'gnd', 'GND común', 'Ra-02 GND, LCD GND, LED de error'),
    ('D5', 'lora', 'LoRa Ra-02', 'SCK'),
    ('D6', 'lora', 'LoRa Ra-02', 'MISO'),
    ('D7', 'lora', 'LoRa Ra-02', 'MOSI'),
    ('D8', 'lora', 'LoRa Ra-02', 'NSS'),
    ('D2', 'lcd', 'LCD 16x2 I2C', 'SDA'),
    ('D1', 'lcd', 'LCD 16x2 I2C', 'SCL'),
    ('D0', 'led', 'LED de ERROR', '+ resistencia 330 Ω → GND'),
    ('FLASH', 'mando', 'Botón de la placa', 'toque = siguiente estación · 5 s = portal WiFi'),
  ],
  ['Arduino IDE: placa NodeMCU 1.0 (ESP-12E Module). Abrir firmware/oficina/oficina.ino',
   'secrets.h: clave del broker. ca_cert.h: el mismo certificado del vivero',
   'Librerías: LoRa, LiquidCrystal I2C, PubSubClient, WiFiManager',
   'WiFi: red "Vivero-oficina" desde el celular (igual que el vivero)'],
  ['Antes de enchufar:',
   'Ra-02 a 3,3 V. A 5 V se quema. Siempre con antena.',
   'LCD a 5 V: conversor de nivel I2C o sacar sus pull-up.',
   'Si con el Ra-02 en D8 la placa no arranca, avisar:',
   'se cambia el NSS de pin.'])
print('ok')
