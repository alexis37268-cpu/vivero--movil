// =============================================================
//  Revisor de clima en segundo plano (Capacitor Background Runner)
//
//  Android lo despierta cada 15 min o mas, aunque la app este cerrada.
//  Lee:
//   - REM (Gobierno de San Luis): estacion Las Chacras (temperatura, humedad,
//     lluvia), San Luis Rural para el viento y la temperatura del suelo (Las
//     Chacras no los mide) y pronostico de La Punta (la mas cercana con pronostico).
//   - SMN: alertas oficiales que cubran la ubicacion del vivero.
//  Si hay algo nuevo (helada, rafaga fuerte, alerta SMN) manda una
//  notificacion al celular.
//
//  El MISMO archivo lo carga la app con la pantalla abierta para mostrar
//  los datos (ahi no hay CapacitorKV ni notificaciones: se reemplazan).
//  En segundo plano no hay navegador (ni DOM ni localStorage): solo fetch,
//  CapacitorKV (memoria chica) y CapacitorNotifications.
// =============================================================

const REM = 'https://clima.sanluis.gob.ar/';
const SMN_FEED = 'https://ssl.smn.gob.ar/CAP/AR.php';
const SMN_ORIGEN = 'https://ssl.smn.gob.ar/';
const AGENTE = 'SolPuntano-vivero/1.0 (control de vivero)';
const NIVEL = { moderate: 'amarillo', severe: 'naranja', extreme: 'rojo' };
const CIERRAN = ['tormenta', 'viento', 'zonda', 'granizo', 'nevada', 'nieve'];

const CFG_DEF = {
  estacion: 56,          // REM Las Chacras (zona de la Escuela de Policia, Juana Koslay)
  estacionViento: 46,    // REM San Luis Rural: viento, rafagas y suelo (Las Chacras no los mide)
  pronostico: 20,        // REM La Punta (la mas cercana con pronostico)
  lat: -33.2617, lon: -66.2430,   // para filtrar alertas del SMN (estacion 56)
  helada: 2,             // °C: minima pronosticada o medida que dispara el aviso
  rafaga: 60,            // km/h: rafaga medida que dispara el aviso
  avisos: true,          // notificaciones en el celular
};

// ¿Corre dentro de la app (pantalla abierta) o en segundo plano?
const EN_APP = typeof window !== 'undefined' && typeof document !== 'undefined';
const KV = EN_APP
  ? { get: (k) => localStorage.getItem('clima.' + k), set: (k, v) => localStorage.setItem('clima.' + k, v) }
  : CapacitorKV;

// ---------- utilidades ----------
function kvLeer(k, def) {
  try {
    const r = KV.get(k);
    const v = typeof r === 'string' ? r : (r && r.value);   // segun la version devuelve texto u {value}
    return v ? JSON.parse(v) : def;
  } catch (e) { return def; }
}
function kvGuardar(k, v) { try { KV.set(k, JSON.stringify(v)); } catch (e) {} }

async function bajar(url) {
  const r = await fetch(url, { headers: { 'User-Agent': AGENTE } });
  if (r.ok === false || r.status >= 400) throw new Error(url + ' -> ' + r.status);
  return await r.text();
}
const ent = (s) => s.replace(/&#(\d+);/g, (_, n) => String.fromCharCode(+n)).replace(/&#x([0-9a-f]+);/gi, (_, n) => String.fromCharCode(parseInt(n, 16)))
                    .replace(/&quot;/g, '"').replace(/&amp;/g, '&').replace(/&lt;/g, '<').replace(/&gt;/g, '>');
const num = (s) => { const m = /-?\d+(?:[.,]\d+)?/.exec(s || ''); return m ? parseFloat(m[0].replace(',', '.')) : null; };
// Valores de las paginas de la REM: <span id="ContentPlaceHolder1_lblTemperatura">11,8</span>
function campos(html) {
  const o = {};
  const re = /id="ContentPlaceHolder1_(\w+)"[^>]*>([^<]*)</g;
  let m;
  while ((m = re.exec(html))) o[m[1]] = ent(m[2]).trim();
  return o;
}

// ---------- REM: estacion (datos medidos) ----------
function leerEstacion(html) {
  const c = campos(html);
  const vmax = c.lblVvMaxHoy || '';
  return {
    nombre: (c.Titulo || '').replace(/^.*:\s*/, ''),
    hora: c.lblHora || '',
    temp: num(c.lblTemperatura),
    hum: num(c.lblHumedad),
    vientoDir: (c.lblDireccionViento || '').split('/')[0],
    viento: num(c.lblVelocidadViento),
    lluvia1h: num(c.lblPrecipitacion),
    lluvia24h: num(c.lblPrecipitacion24h),
    lluvia30d: num(c.lblPrecipitacion30d),
    suelo: [num(c.lblTemperaturaSuelo1), num(c.lblTemperaturaSuelo2), num(c.lblTemperaturaSuelo3)],
    maxHoy: num(c.lblTempMaxHoy), minHoy: num(c.lblTempMinHoy),
    rafagaHoy: num(vmax), rafagaHora: (/a las (\d{1,2}:\d{2})/.exec(vmax) || [])[1] || '',
  };
}

// ---------- REM: pronostico de 3 dias ----------
function leerPronostico(html) {
  const c = campos(html);
  const dias = [];
  [['24hs', '24hsT'], ['48hs', null], ['72hs', null]].forEach(([k, kt]) => {
    if (!c['Titulo' + k]) return;
    dias.push({
      dia: c['Titulo' + k],
      min: num(c['TempMin' + k]), max: num(c['TempMax' + k]),
      cielo: [c['NombreCondicion' + k], kt && c['NombreCondicion' + kt]].filter((v, i, l) => v && l.indexOf(v) === i).join(' / '),   // sin repetir si mañana = tarde
      lluvia: /Probabilidad/i.test(c['Probabilidades' + k] || '') || /Probabilidad/i.test((kt && c['Probabilidades' + kt]) || ''),
    });
  });
  return { estacion: (c.Titulo || '').replace(/^.*estación\s*/i, ''), hoyMin: num(c.TempMinHoy), hoyMax: num(c.TempMaxHoy), dias };
}

// Horas de la REM = hora argentina (UTC-3, sin horario de verano). No se usa la
// zona horaria del aparato, asi da igual como este configurado el celular.
const AR = -3 * 3600e3;
const enAR = (ms) => new Date(ms + AR);                        // leer con getUTC* = hora argentina
const deAR = (y, m, d, h, mi) => new Date(Date.UTC(y, m, d, h, mi) - AR);

// "Sábado 7" -> mediodia de ese dia, hora argentina (dentro de los proximos 4 dias)
function fechaDe(titulo, ahora) {
  const n = num(titulo);
  for (let i = 0; i < 5; i++) {
    const d = enAR(ahora.getTime() + i * 86400e3);
    if (d.getUTCDate() === n) return deAR(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate(), 12, 0);
  }
  return null;
}

// ---------- SMN: alertas oficiales ----------
function dentro(lat, lon, pol) {
  let a = false;
  for (let i = 0, j = pol.length - 1; i < pol.length; j = i++) {
    const [la1, lo1] = pol[i], [la2, lo2] = pol[j];
    if ((lo1 > lon) !== (lo2 > lon) && lat < la1 + (lon - lo1) * (la2 - la1) / (lo2 - lo1)) a = !a;
  }
  return a;
}
const tag = (xml, t) => { const m = new RegExp('<' + t + '>([\\s\\S]*?)</' + t + '>').exec(xml); return m ? ent(m[1]).trim() : ''; };
function leerCap(xml, lat, lon) {
  if (tag(xml, 'status') !== 'Actual' || tag(xml, 'msgType') === 'Cancel') return null;
  const infos = xml.split('<info>').slice(1);
  for (const info of infos) {
    const pols = [];
    const re = /<polygon>([\s\S]*?)<\/polygon>/g;
    let m;
    while ((m = re.exec(info))) {
      const pts = m[1].trim().split(/\s+/).map(p => p.split(',').map(Number)).filter(p => p.length === 2);
      if (pts.length >= 3) pols.push(pts);
    }
    if (!pols.some(p => dentro(lat, lon, p))) continue;
    const evento = tag(info, 'event');
    return {
      id: tag(xml, 'identifier'), evento,
      nivel: NIVEL[tag(info, 'severity').toLowerCase()] || 'otro',
      desde: Math.floor(Date.parse(tag(info, 'onset') || tag(xml, 'sent')) / 1000),
      hasta: Math.floor(Date.parse(tag(info, 'expires')) / 1000),
      texto: tag(info, 'description'),
      cierra: CIERRAN.some(k => evento.toLowerCase().includes(k)),
      fuente: 'SMN',
    };
  }
  return null;
}
async function alertasSmn(cfg, ahoraS) {
  const rss = await bajar(SMN_FEED);
  const links = [];
  const re = /<guid>([\s\S]*?)<\/guid>/g;
  let m;
  while ((m = re.exec(rss))) { const l = m[1].trim(); if (l.startsWith(SMN_ORIGEN) && !links.includes(l)) links.push(l); }
  const cache = kvLeer('smnCache', {});
  const nuevo = {};
  const faltan = links.filter(l => !(l in cache));
  for (let i = 0; i < faltan.length; i += 8) {           // de a 8 en paralelo
    await Promise.all(faltan.slice(i, i + 8).map(async (l) => {
      try { nuevo[l] = leerCap(await bajar(l), cfg.lat, cfg.lon) || false; } catch (e) { /* se reintenta la proxima */ }
    }));
  }
  const final = {};
  links.forEach(l => { if (l in cache) final[l] = cache[l]; else if (l in nuevo) final[l] = nuevo[l]; });
  kvGuardar('smnCache', final);                          // lo que ya no esta en el feed se olvida
  return Object.values(final).filter(a => a && a.hasta > ahoraS);
}

// ---------- alertas propias (helada y rafaga) ----------
function alertasRem(est, pron, cfg, ahora) {
  const out = [], ahoraS = Math.floor(ahora.getTime() / 1000);
  if (pron) pron.dias.slice(0, 2).forEach(d => {
    if (d.min == null || d.min > cfg.helada) return;
    const f = fechaDe(d.dia, ahora); if (!f) return;
    const fin = new Date(f.getTime() - 2 * 3600e3);          // 10:00 de ese dia: ya paso la helada
    out.push({ id: 'helada-' + f.toISOString().slice(0, 10), evento: 'Helada pronosticada', nivel: d.min <= 0 ? 'naranja' : 'amarillo',
               desde: ahoraS, hasta: Math.floor(fin.getTime() / 1000), cierra: false, fuente: 'REM',
               texto: 'Pronóstico REM (' + pron.estacion + ') para el ' + d.dia + ': mínima ' + d.min + ' °C. Revisá cerrar el venteo y proteger los plantines.' });
  });
  if (est && est.temp != null && est.temp <= cfg.helada) {
    out.push({ id: 'heladaMedida-' + ahora.toISOString().slice(0, 13), evento: 'Temperatura baja medida', nivel: est.temp <= 0 ? 'rojo' : 'naranja',
               desde: ahoraS, hasta: ahoraS + 3600, cierra: false, fuente: 'REM',
               texto: 'La estación ' + est.nombre + ' marca ' + est.temp + ' °C (' + est.hora + ').' });
  }
  if (est && est.rafagaHoy != null && est.rafagaHoy >= cfg.rafaga && est.rafagaHora) {
    const [h, mi] = est.rafagaHora.split(':').map(Number);
    const hoy = enAR(ahora.getTime());
    const cuando = deAR(hoy.getUTCFullYear(), hoy.getUTCMonth(), hoy.getUTCDate(), h, mi);
    if (ahora - cuando >= 0 && ahora - cuando <= 90 * 60e3) {          // solo si fue hace poco
      out.push({ id: 'rafaga-' + cuando.toISOString().slice(0, 16), evento: 'Ráfaga fuerte medida', nivel: est.rafagaHoy >= cfg.rafaga + 20 ? 'naranja' : 'amarillo',
                 desde: Math.floor(cuando.getTime() / 1000), hasta: Math.floor(cuando.getTime() / 1000) + 3 * 3600, cierra: true, fuente: 'REM',
                 texto: 'Ráfaga de ' + est.rafagaHoy + ' km/h a las ' + est.rafagaHora + ' en la estación ' + (est.vientoDe || est.nombre) + '.' });
    }
  }
  return out;
}

// ---------- revision completa ----------
async function revisar(ahora, extra) {
  const cfg = Object.assign({}, CFG_DEF, kvLeer('cfg', {}), extra || {});
  const ahoraS = Math.floor(ahora.getTime() / 1000);
  const errores = [];
  let est = null, estV = null, pron = null, smn = [];
  const otra = cfg.estacionViento && cfg.estacionViento !== cfg.estacion;
  await Promise.all([
    bajar(REM + 'Estacion.aspx?Estacion=' + cfg.estacion).then(h => { est = leerEstacion(h); }).catch(e => errores.push('REM estación: ' + e.message)),
    otra ? bajar(REM + 'Estacion.aspx?Estacion=' + cfg.estacionViento).then(h => { estV = leerEstacion(h); }).catch(e => errores.push('REM viento: ' + e.message)) : null,
    bajar(REM + 'Pronostico.aspx?Estacion=' + cfg.pronostico).then(h => { pron = leerPronostico(h); }).catch(e => errores.push('REM pronóstico: ' + e.message)),
    alertasSmn(cfg, ahoraS).then(a => { smn = a; }).catch(e => errores.push('SMN: ' + e.message)),
  ]);
  // Lo que la estacion principal no mide (viento, rafaga, suelo) se completa con la otra
  if (est && estV) {
    const v = {};
    if (est.viento == null && estV.viento != null) Object.assign(v, { viento: estV.viento, vientoDir: estV.vientoDir });
    if (est.rafagaHoy == null && estV.rafagaHoy != null) Object.assign(v, { rafagaHoy: estV.rafagaHoy, rafagaHora: estV.rafagaHora });
    if (est.suelo.every(x => x == null) && estV.suelo.some(x => x != null)) v.suelo = estV.suelo;
    if (Object.keys(v).length) Object.assign(est, v, { vientoDe: estV.nombre });
  }
  const alertas = smn.concat(alertasRem(est, pron, cfg, ahora));

  // Notificar solo lo nuevo (cada alerta una sola vez). Con la app abierta no se
  // notifica: lo ve en pantalla (y el aviso queda para el revisor de segundo plano).
  if (EN_APP) { const res = { revisado: ahoraS, estacion: est, pronostico: pron, alertas, errores }; kvGuardar('ultimo', res); return res; }
  const avisadas = kvLeer('avisadas', {});
  const nuevas = alertas.filter(a => !avisadas[a.id]);
  if (cfg.avisos && nuevas.length) {
    const N = { amarillo: 'Alerta amarilla', naranja: 'Alerta naranja', rojo: 'Alerta roja', otro: 'Aviso' };
    CapacitorNotifications.schedule(nuevas.slice(0, 4).map((a, i) => ({
      id: (ahoraS % 100000) * 10 + i,
      title: '🌩 ' + N[a.nivel] + ': ' + a.evento,
      body: a.texto.slice(0, 180),
    })));
  }
  nuevas.forEach(a => { avisadas[a.id] = a.hasta; });
  Object.keys(avisadas).forEach(k => { if (avisadas[k] < ahoraS - 86400) delete avisadas[k]; });
  kvGuardar('avisadas', avisadas);

  const res = { revisado: ahoraS, estacion: est, pronostico: pron, alertas, errores };
  kvGuardar('ultimo', res);
  return res;
}

// ---------- eventos ----------
if (EN_APP) {
  window.ClimaRevisor = { revisar, leerEstacion, leerPronostico };
} else {
// Android lo llama solo cada 15 min (configurado en capacitor.config.json)
addEventListener('revisarClima', async (resolve, reject, args) => {
  try { resolve(await revisar(new Date())); }
  catch (e) { console.error('[clima] ' + e); reject(String(e)); }
});
// La app pide el ultimo resultado guardado (al abrir, es instantaneo)
addEventListener('ultimoClima', (resolve) => { resolve(kvLeer('ultimo', null)); });
// La app guarda los ajustes (umbrales, avisos si/no)
addEventListener('guardarConfig', (resolve, reject, args) => {
  const c = Object.assign({}, kvLeer('cfg', {}));
  ['helada', 'rafaga', 'avisos', 'estacion', 'estacionViento', 'pronostico', 'lat', 'lon'].forEach(k => { if (args && args[k] !== undefined) c[k] = args[k]; });
  kvGuardar('cfg', c);
  resolve(Object.assign({}, CFG_DEF, c));
});
}
