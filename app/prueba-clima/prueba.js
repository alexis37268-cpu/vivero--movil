// Prueba del revisor de clima con paginas REALES guardadas (REM 46 y 20 del 6/11/2026, REM 56 del 7/11/2026, SMN 6/10/2026).
// Simula el entorno del Background Runner (fetch, CapacitorKV, CapacitorNotifications).
// Uso: node prueba-clima/prueba.js   (desde app/)
const fs = require('fs'), path = require('path'), vm = require('vm');
const D = __dirname;
let fallas = 0;
const ok = (c, msg) => { console.log((c ? 'OK  ' : 'MAL ') + msg); if (!c) fallas++; };

function entorno(ajustes) {
  const kv = {}, notif = [], handlers = {};
  const archivo = (url) => {
    if (url.includes('Estacion.aspx?Estacion=56')) return 'estacion56.html';
    if (url.includes('Estacion.aspx')) return 'estacion46.html';
    if (url.includes('Pronostico.aspx')) return 'pronostico20.html';
    if (url.includes('CAP/AR.php')) return 'rss_excerpt.xml';
    return url.split('/').pop();
  };
  const ctx = {
    console, Date, Math, JSON, Promise, String, Number, Object, parseFloat, parseInt, RegExp, Error,
    fetch: async (url) => ({ ok: true, status: 200, text: async () => fs.readFileSync(path.join(D, archivo(url)), 'utf8') }),
    CapacitorKV: { get: k => ({ value: kv[k] }), set: (k, v) => { kv[k] = v; }, remove: k => { delete kv[k]; } },
    CapacitorNotifications: { schedule: (l) => notif.push(...l) },
    addEventListener: (ev, fn) => { handlers[ev] = fn; },
  };
  vm.createContext(ctx);
  vm.runInContext(fs.readFileSync(path.join(D, '..', 'runner-clima.js'), 'utf8'), ctx);
  const disparar = (ev, args) => new Promise((res, rej) => handlers[ev](res, rej, args));
  return { kv, notif, disparar, ctx };
}

function fijarFecha(e, iso) {
  e.ctx.Date = class extends Date { constructor(...a) { super(...(a.length ? a : [iso])); } static now() { return Date.parse(iso); } };
  vm.runInContext('Date = this.Date', e.ctx);
}

(async () => {
  // 1) San Luis, viernes 6/11/2026 21:05 (hora de la captura de la REM)
  let e = entorno();
  fijarFecha(e, '2026-11-06T21:05:00-03:00');
  let r = await e.disparar('revisarClima', {});
  ok(r.estacion && r.estacion.nombre === 'Las Chacras' && r.estacion.temp === 17.9 && r.estacion.hum === 36.2, 'estación Las Chacras: 17,9 °C y 36,2 %');
  ok(r.estacion.vientoDe === 'San Luis Rural', 'viento y suelo completados con San Luis Rural');
  ok(r.estacion.viento === 20.5 && r.estacion.vientoDir === 'SE', 'viento SE 20,5 km/h');
  ok(r.estacion.rafagaHoy === 47 && r.estacion.rafagaHora === '15:41', 'ráfaga del día 47 km/h a las 15:41');
  ok(JSON.stringify(r.estacion.suelo) === '[19.5,19.9,19.4]', 'suelo 5/10/30 cm');
  ok(r.pronostico.dias.length === 3 && r.pronostico.dias[0].dia === 'Sábado 7' && r.pronostico.dias[0].min === 8 && r.pronostico.dias[0].max === 21, 'pronóstico sábado 8/21');
  ok(r.pronostico.dias[0].cielo === 'Ventoso / Parcialmente Nublado' && r.pronostico.dias[0].lluvia, 'cielo y probabilidad de lluvia');
  ok(r.alertas.length === 0 && e.notif.length === 0, 'sin alertas para San Luis ese día (las del SMN de prueba son de octubre)');

  // 2) Umbral de helada alto (10 °C): tiene que avisar helada para el sábado (mín 8)
  e = entorno(); fijarFecha(e, '2026-11-06T21:05:00-03:00');
  await e.disparar('guardarConfig', { helada: 10 });
  r = await e.disparar('revisarClima', {});
  const hel = r.alertas.filter(a => a.evento === 'Helada pronosticada');
  ok(hel.length >= 1 && /Sábado 7: mínima 8/.test(hel[0].texto), 'helada pronosticada con umbral 10 °C');
  ok(e.notif.length === r.alertas.length, 'una notificación por alerta nueva (' + e.notif.length + ')');
  const antes = e.notif.length;
  await e.disparar('revisarClima', {});
  ok(e.notif.length === antes, 'la segunda vez no repite la notificación');

  // 3) Avisos apagados: guarda alertas pero no notifica
  e = entorno(); fijarFecha(e, '2026-11-06T21:05:00-03:00');
  await e.disparar('guardarConfig', { helada: 10, avisos: false });
  r = await e.disparar('revisarClima', {});
  ok(r.alertas.length > 0 && e.notif.length === 0, 'con avisos apagados no notifica');

  // 4) Alerta del SMN real: punto en Rio Cuarto, el 7/10/2026 a las 06:00 (vigente)
  e = entorno();
  fijarFecha(e, '2026-10-07T06:00:00-03:00');
  await e.disparar('guardarConfig', { lat: -33.1235, lon: -64.3493 });
  r = await e.disparar('revisarClima', {});
  const smn = r.alertas.filter(a => a.fuente === 'SMN');
  ok(smn.length === 1 && smn[0].evento === 'Tormentas' && smn[0].nivel === 'naranja' && smn[0].cierra, 'alerta SMN naranja de tormentas en Río Cuarto');
  ok(e.kv.smnCache && Object.keys(JSON.parse(e.kv.smnCache)).length === 5, 'caché del SMN guardada');

  // 5) Ráfaga reciente sobre el umbral (47 km/h a las 15:41, revisando a las 16:30 con umbral 40)
  e = entorno();
  fijarFecha(e, '2026-11-06T16:30:00-03:00');
  await e.disparar('guardarConfig', { rafaga: 40 });
  r = await e.disparar('revisarClima', {});
  ok(r.alertas.some(a => a.evento === 'Ráfaga fuerte medida'), 'ráfaga de 47 km/h con umbral 40 avisa');

  // 6) El último resultado queda guardado para la app
  const u = await e.disparar('ultimoClima', {});
  ok(u && u.estacion && u.estacion.temp === 17.9, 'último resultado disponible para la app');

  // 7) Sin estacion de viento: Las Chacras sola deja el viento vacio (no inventa)
  e = entorno(); fijarFecha(e, '2026-11-06T21:05:00-03:00');
  await e.disparar('guardarConfig', { estacionViento: 0 });
  r = await e.disparar('revisarClima', {});
  ok(r.estacion.viento === null && !r.estacion.vientoDe && r.errores.length === 0, 'Las Chacras sola: sin viento y sin errores');

  console.log(fallas ? fallas + ' FALLAS' : 'TODO OK');
  process.exit(fallas ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
