// Arma la carpeta www/ que usa la app de Android:
//  - index.html = app.html con la cabecera de documento
//  - mqtt.min.js = el cliente MQTT (va adentro de la app, no se baja de internet)
const fs = require('fs');
const cuerpo = fs.readFileSync('app.html', 'utf8');
const html = `<!doctype html><html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Vivero Sol Puntano</title>
<style>:root{padding-top:env(safe-area-inset-top,0px)}[hidden]{display:none!important}</style>
</head><body>
${cuerpo}
</body></html>`;
fs.mkdirSync('www', { recursive: true });
fs.writeFileSync('www/index.html', html);
fs.copyFileSync('node_modules/mqtt/dist/mqtt.min.js', 'www/mqtt.min.js');
fs.copyFileSync('logo-completo.png', 'www/logo.png');      // logo de Sol Puntano para el encabezado
console.log('www/ listo');
