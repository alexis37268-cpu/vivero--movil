// =============================================================
//  Robot del clima (GitHub Actions, cada 15 min)
//
//  Corre el MISMO revisor que usa la app del celular (app/runner-clima.js):
//  lee la REM (estacion San Luis Rural + pronostico de La Punta) y las
//  alertas del SMN, y deja el resultado en datos/clima.json.
//  El workflow sube ese archivo a la rama "datos" del repo; la pagina
//  para PC lo lee de ahi (GitHub lo permite, la REM no).
//
//  Uso: node robot/clima-robot.js <carpeta de datos>
// =============================================================
const fs = require('fs'), path = require('path'), vm = require('vm');

const DIR = process.argv[2] || 'datos';
const KV_ARCHIVO = path.join(DIR, 'kv.json');          // memoria del revisor (cache del SMN)
fs.mkdirSync(DIR, { recursive: true });
let kv = {};
try { kv = JSON.parse(fs.readFileSync(KV_ARCHIVO, 'utf8')); } catch (e) {}

const handlers = {};
const ctx = {
  console, Date, Math, JSON, Promise, String, Number, Object, Array, parseFloat, parseInt, RegExp, Error, setTimeout,
  fetch: (url, op) => fetch(url, op),                    // fetch de Node 20
  CapacitorKV: { get: (k) => ({ value: kv[k] }), set: (k, v) => { kv[k] = v; }, remove: (k) => { delete kv[k]; } },
  CapacitorNotifications: { schedule: () => {} },        // el robot no notifica (eso lo hace el celular)
  addEventListener: (ev, fn) => { handlers[ev] = fn; },
};
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(__dirname, '..', 'app', 'runner-clima.js'), 'utf8'), ctx);
const disparar = (ev, args) => new Promise((res, rej) => handlers[ev](res, rej, args));

(async () => {
  const lat = parseFloat(process.env.VIVERO_LAT), lon = parseFloat(process.env.VIVERO_LON);
  const cfg = { avisos: false };
  if (!isNaN(lat) && !isNaN(lon)) Object.assign(cfg, { lat, lon });
  await disparar('guardarConfig', cfg);
  const r = await disparar('revisarClima', {});
  r.fuente = 'robot';
  fs.writeFileSync(path.join(DIR, 'clima.json'), JSON.stringify(r));
  fs.writeFileSync(KV_ARCHIVO, JSON.stringify(kv));
  const e = r.estacion;
  console.log('Estacion:', e ? `${e.nombre} ${e.temp} °C ${e.hum} % (${e.hora})` : 'sin datos');
  console.log('Pronostico:', r.pronostico ? r.pronostico.dias.map(d => `${d.dia} ${d.min}/${d.max}`).join(' | ') : 'sin datos');
  console.log('Alertas:', r.alertas.length ? r.alertas.map(a => `${a.nivel} ${a.evento}`).join(' | ') : 'ninguna');
  if (r.errores.length) console.log('Errores:', r.errores.join(' | '));
  // Si no se pudo leer nada, que el workflow falle (se ve en rojo en Actions)
  if (!e && !r.pronostico) process.exit(1);
})().catch(e => { console.error(e); process.exit(1); });
