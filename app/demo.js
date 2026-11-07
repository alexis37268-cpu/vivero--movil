// =============================================================
//  MODO DEMOSTRACION (para mostrar la app sin placa ni internet)
//
//  Simula, dentro de la misma app, un broker MQTT con:
//   - Vivero (nodo1): temperatura, humedad, venteo, 5 cultivos con
//     litros y riegos, usuarios de prueba, registro e historial de 48 h.
//   - 4 hibernaculos (nodo2 a nodo5) por LoRa, con lo basico.
//   - Clima de afuera y una alerta de ejemplo.
//  Usa los mismos mensajes que el equipo real, asi que la app no cambia.
//
//  Lo que se cambia (usuarios, PINs, ajustes, registro, riegos) queda
//  guardado en este navegador/celular ("demo.sim"); "Reiniciar demo"
//  lo vuelve a fabrica.
//
//  PARA SACARLO cuando se venda: borrar este archivo y la linea
//  <script src="demo.js"> de app.html. Nada mas.
// =============================================================
(function () {
  'use strict';
  const PASO_MS = 2000;                       // la simulacion avanza cada 2 s
  const ahoraS = () => Math.floor(Date.now() / 1000);
  // Usuarios de prueba (hash = SHA-256 de "nodo1:nombre:PIN", 16 caracteres)
  const GUARDA = 'demo.sim';
  const USUARIOS = [{ n: 'Admin', admin: true, h: '299317cbb4b36f51' }, { n: 'Operario', admin: false, h: '1859409e4440c15d' }];

  function coincide(patron, topic) {           // comodines MQTT: + y #
    const p = patron.split('/'), t = topic.split('/');
    for (let i = 0; i < p.length; i++) {
      if (p[i] === '#') return true;
      if (i >= t.length || (p[i] !== '+' && p[i] !== t[i])) return false;
    }
    return p.length === t.length;
  }

  function crear() {
    const manejadores = {}, subs = [], retenidos = {};
    let vivo = true;
    const entregar = (t, m) => (manejadores.message || []).forEach(fn => fn(t, { toString: () => m }));
    const emitir = (t, m, retener) => {
      if (retener) retenidos[t] = m;
      if (subs.some(s => coincide(s, t))) setTimeout(() => vivo && entregar(t, m), 15);
    };
    const B1 = 'vivero/nodo1/';
    let nombre1 = 'Vivero';

    // Aire: ciclo del dia (minimo a la madrugada, maximo a la tarde)
    const ciclo = (ms) => { const d = new Date(ms); return Math.sin((d.getHours() + d.getMinutes() / 60 - 9) / 24 * 2 * Math.PI); };
    const aireT = (ms) => 21 + 7 * ciclo(ms), aireH = (ms) => 62 - 16 * ciclo(ms);

    // ---------------- Vivero ----------------
    const st = { temp: +aireT(Date.now()).toFixed(1), hum: +aireH(Date.now()).toFixed(1), sensor_ok: true, venteo: 'cerrado', riego: 'off', modo: 'auto', rssi: -58, uptime_s: 3600, clima_cierre: false };
    const cfg = { temp_abrir: 28, temp_cerrar: 24, hum_riego_on: 40, hum_riego_off: 60, motor_max_s: 40, riego_max_min: 10, riego_pausa_min: 30,
                  recorrido: true, hist: true, clima_cierra: 1, usuarios: true, renombrar: true, cultivos: 5 };
    const cc = [['Lechuga', 0, 35, 60, 40, 15], ['Tomate', 2, 30, 55, 60, 20], ['Acelga', 1, 35, 60, 25, 15], ['Pimiento', 0, 30, 58, 50, 15], ['Plantines', 2, 40, 65, 15, 10]]
      .map(([n, m, on, off, l, t]) => ({ n, m, on, off, l, t, a: 1 }));
    const ec = cc.map((x, i) => ({ s: [37, 52, 45, 61, 43][i], lh: [12.4, 30.1, 8.5, 18.2, 6.4][i], lm: [212, 486, 96, 163, 61][i], lt: [1214, 2486, 603, 912, 318][i] }));
    let usuarios = USUARIOS.map(u => Object.assign({}, u));
    let reg = [], riegosL = [], regando = -1, lr = 0, rDesde = 0, rIni = 0, rQuien = '', venteoHasta = 0;
    const quienTxt = q => q || 'sin usuario';
    const esAdmin = q => usuarios.some(u => u.n.toLowerCase() === (q || '').toLowerCase() && u.admin);
    const registrar = (q, que) => { const l = ahoraS() + '|' + quienTxt(q) + '|' + que; reg.push(l); reg = reg.slice(-300); emitir(B1 + 'registro/nuevo', l); guardarSim(); };
    const pubE = () => { st.riego = regando >= 0 ? 'on' : 'off'; emitir(B1 + 'estado', JSON.stringify(st), true); };
    const pubC = () => emitir(B1 + 'config', JSON.stringify(cfg), true);
    const pubU = () => emitir(B1 + 'usuarios', JSON.stringify(usuarios.map(u => ({ n: u.n, r: u.admin ? 'admin' : 'operario', h: u.h }))), true);
    const pubCuCfg = () => emitir(B1 + 'cultivos/config', JSON.stringify(cc), true);
    const pubCu = () => emitir(B1 + 'cultivos', JSON.stringify({
      r: regando, q: regando >= 0 ? 12 : 0, lr: +lr.toFixed(1), rs: regando >= 0 ? Math.floor((Date.now() - rDesde) / 1000) : 0, lsv: 0,
      c: ec.map(e => ({ s: +e.s.toFixed(1), ok: 1, lh: +e.lh.toFixed(1), lm: +e.lm.toFixed(1), lt: +e.lt.toFixed(1) })) }), true);
    function cerrar(mot, q) {
      if (regando < 0) return;
      const i = regando, seg = Math.floor((Date.now() - rDesde) / 1000);
      const l = rIni + '|' + ahoraS() + '|' + cc[i].n + '|' + lr.toFixed(1) + '|' + seg + '|' + quienTxt(rQuien) + '|' + mot;
      riegosL.push(l); emitir(B1 + 'riegos/nuevo', l);
      registrar(q || 'automatico', cc[i].n + ': termina de regar, ' + lr.toFixed(1) + ' L (' + mot + ')');
      regando = -1; lr = 0; pubE(); pubCu();
    }
    function abrir(i, q, mot) {
      if (regando >= 0) cerrar('pasa a otro cultivo', q);
      regando = i; rDesde = Date.now(); rIni = ahoraS(); rQuien = q; lr = 0;
      registrar(q, cc[i].n + ': empieza a regar (' + mot + ')'); pubE(); pubCu();
    }
    function moverVenteo(a, q) {
      st.venteo = a === 'abrir' ? 'abriendo' : 'cerrando'; venteoHasta = Date.now() + 8000;
      registrar(q, (a === 'abrir' ? 'abre' : 'cierra') + ' el venteo' + (q === 'automatico' ? ' (' + st.temp.toFixed(1) + ' C)' : '')); pubE();
    }

    // Historial de 48 h (temperatura, humedad y suelo de los 5 cultivos)
    const hist = [];
    for (let k = 0; k < 288; k++) {
      const ms = Date.now() - (287 - k) * 600e3;
      hist.push([Math.round((aireT(ms) + (Math.random() - 0.5) * 0.6) * 10), Math.round((aireH(ms) + (Math.random() - 0.5) * 2) * 10)]
        .concat(cc.map((c, i) => Math.round(Math.max(10, Math.min(90, 48 + 14 * Math.sin((k / 6 + i * 3) / 7 * Math.PI) + Math.random() * 2))))));
    }
    ec.forEach((e, i) => {
      const dif = e.s - hist[287][2 + i];
      for (let k = 276; k < 288; k++) hist[k][2 + i] = Math.round(Math.max(5, Math.min(95, hist[k][2 + i] + dif * (k - 275) / 12)));
    });
    // Riegos y cambios de los ultimos dos dias
    for (let k = 14; k >= 1; k--) {
      const i = k % 5, ini = ahoraS() - k * 3 * 3600, seg = 300 + (k % 4) * 90, litros = (seg / 60 * 12).toFixed(1);
      riegosL.push(ini + '|' + (ini + seg) + '|' + cc[i].n + '|' + litros + '|' + seg + '|' + (k % 4 ? 'automatico' : 'Operario') + '|' +
                   (cc[i].m === 1 ? 'llego a ' + cc[i].l + ' L' : 'suelo en ' + cc[i].off + ' %'));
      reg.push(ini + '|' + (k % 4 ? 'automatico' : 'Operario') + '|' + cc[i].n + ': empieza a regar (suelo en ' + (cc[i].on - 2) + ' %)');
      if (k % 3 === 0) reg.push((ini + 3600) + '|automatico|' + (k % 2 ? 'abre' : 'cierra') + ' el venteo (' + (k % 2 ? 28.4 : 23.8) + ' C)');
      if (k === 9) reg.push((ini + 600) + '|Admin|ajuste temp_abrir = 28.0');
      if (k === 5) reg.push((ini + 900) + '|panel|pasa a manual', (ini + 1500) + '|panel|pasa a automatico');
    }
    reg.unshift((ahoraS() - 50 * 3600) + '|Admin|alta de usuario Operario (operario)', (ahoraS() - 50 * 3600 - 60) + '|Admin|alta de usuario Admin (administrador)');
    reg.sort((a, b) => (+a.split('|')[0]) - (+b.split('|')[0]));

    // ---------------- Hibernaculo (LoRa) ----------------
    // Hibernaculos: cada uno un poco distinto (dT/dH respecto del vivero)
    const HIB = [['nodo2', 'Hibernáculo 1', -4, 10, -96], ['nodo3', 'Hibernáculo IMProFoP', -2, 6, -88],
                 ['nodo4', 'Hibernáculo 3', -5, 14, -101], ['nodo5', 'Hibernáculo 4', -3, 8, -93]]
      .map(([id, nombre, dT, dH, rssi]) => ({ id, nombre, dT, dH,
        st: { via: 'lora', temp: 19.5, hum: 71, sensor_ok: true, venteo: 'cerrado', riego: 'off', modo: 'auto', rssi, uptime_s: 7200 },
        cfg: { temp_abrir: 26, temp_cerrar: 22, hum_riego_on: 45, hum_riego_off: 70, motor_max_s: 40, riego_max_min: 10, riego_pausa_min: 30, recorrido: true } }));
    const pubH = (h) => emitir('vivero/' + h.id + '/estado', JSON.stringify(h.st), true);

    // ---------------- Lo guardado de antes (si hay) ----------------
    function guardarSim() {
      try {
        localStorage.setItem(GUARDA, JSON.stringify({ v: 1, cfg, hib: HIB.map(h => ({ cfg: h.cfg, modo: h.st.modo })), nombre1, cc, usuarios, modo: st.modo, reg: reg.slice(-300), riegos: riegosL.slice(-100),
          ec: ec.map(e => ({ lh: e.lh, lm: e.lm, lt: e.lt })) }));
      } catch (e) {}
    }
    let previo = null;
    try { previo = JSON.parse(localStorage.getItem(GUARDA) || 'null'); } catch (e) {}
    if (previo && previo.v === 1) {
      Object.assign(cfg, previo.cfg);
      (previo.hib || []).forEach((x, i) => { if (HIB[i]) { Object.assign(HIB[i].cfg, x.cfg); if (x.modo) HIB[i].st.modo = x.modo; } });
      if (previo.nombre1) nombre1 = previo.nombre1;
      (previo.cc || []).forEach((c, i) => cc[i] && Object.assign(cc[i], c));
      (previo.ec || []).forEach((e, i) => ec[i] && Object.assign(ec[i], e));
      if (Array.isArray(previo.usuarios) && previo.usuarios.length) usuarios = previo.usuarios;
      if (Array.isArray(previo.reg)) reg = previo.reg;
      if (Array.isArray(previo.riegos)) riegosL = previo.riegos;
      if (previo.modo === 'auto' || previo.modo === 'manual') st.modo = previo.modo;
    }

    function publicarTodo() {
      emitir(B1 + 'online', 'online', true); emitir(B1 + 'nombre', nombre1, true);
      HIB.forEach(h => { emitir('vivero/' + h.id + '/online', 'online', true); emitir('vivero/' + h.id + '/nombre', h.nombre, true); });
      pubC(); pubE(); pubU(); pubCuCfg(); pubCu();
      HIB.forEach(h => { emitir('vivero/' + h.id + '/config', JSON.stringify(h.cfg), true); pubH(h); });
    }

    // ---------------- Ordenes de la app ----------------
    function recibir(t, crudo) {
      const m = /^vivero\/(nodo\d)\/cmd\/(.+)$/.exec(t); if (!m) return;
      const nodo = m[1], cmd = m[2], li = crudo.lastIndexOf(';');
      const valor = (li >= 0 ? crudo.slice(0, li) : crudo).trim(), quien = li >= 0 ? crudo.slice(li + 1).trim() : '', msg = valor.toLowerCase();
      const h = HIB.find(x => x.id === nodo);
      if (h) {                                 // hibernaculo: lo basico
        const B = 'vivero/' + h.id + '/', s2 = h.st;
        if (cmd === 'modo') { s2.modo = msg; guardarSim(); }
        else if (cmd === 'venteo' && (s2.modo === 'manual' || msg === 'parar')) s2.venteo = msg === 'abrir' ? 'abierto' : msg === 'cerrar' ? 'cerrado' : s2.venteo;
        else if (cmd === 'riego' && (s2.modo === 'manual' || msg === 'off')) s2.riego = msg;
        else if (cmd.startsWith('config/')) { h.cfg[cmd.slice(7)] = parseFloat(msg); emitir(B + 'config', JSON.stringify(h.cfg), true); guardarSim(); }
        else if (cmd === 'historial') emitir(B + 'historial', JSON.stringify({ p: 0, de: 1, n: 0, dt: 600, edad: 0, desde: 0, v: [] }));
        pubH(h);
        return;
      }
      if (nodo !== 'nodo1') return;
      if (cmd === 'nombre') {                  // como el equipo real: solo administrador
        const n = valor.replace(/[|;"\\]/g, ' ').trim().slice(0, 24);
        if (!esAdmin(quien)) { registrar(quien, 'rechazado: cambio de nombre (no es administrador)'); emitir(B1 + 'alarma', 'nombre: solo un administrador lo cambia'); return; }
        if (n.length < 2 || n === nombre1) return;
        registrar(quien, 'cambia el nombre: ' + nombre1 + ' -> ' + n); nombre1 = n; guardarSim();
        emitir(B1 + 'nombre', nombre1, true);
        return;
      }
      if (cmd === 'modo') { if (msg === 'auto' || msg === 'manual') { st.modo = msg; registrar(quien, msg === 'auto' ? 'pasa a automatico' : 'pasa a manual'); } }
      else if (cmd === 'venteo') {
        if (msg === 'parar') { st.venteo = 'desconocido'; venteoHasta = 0; registrar(quien, 'para el venteo'); }
        else if (st.modo === 'auto') { emitir(B1 + 'alarma', 'venteo: orden ignorada, esta en automatico'); registrar(quien, 'pide ' + msg + ' venteo (ignorado: automatico)'); }
        else moverVenteo(msg, quien);
      }
      else if (cmd === 'riego') { if (msg === 'off' && regando >= 0) cerrar('cortado a mano', quien); else if (msg === 'on') emitir(B1 + 'alarma', 'riego: elegir el cultivo en la pestana Cultivos'); }
      else if (cmd.startsWith('cultivo/')) {
        const [n, que] = cmd.slice(8).split('/'), i = +n - 1; if (!cc[i]) return;
        if (que === 'regar') {
          if (msg === 'off') { if (regando === i) cerrar('cortado a mano', quien); }
          else if (st.modo === 'auto') { emitir(B1 + 'alarma', cc[i].n + ': orden ignorada, esta en automatico'); registrar(quien, 'pide regar ' + cc[i].n + ' (ignorado: automatico)'); }
          else abrir(i, quien, 'a mano');
        } else if (que === 'config') {
          if (!esAdmin(quien)) { registrar(quien, 'rechazado: ajustes de ' + cc[i].n + ' (no es administrador)'); emitir(B1 + 'alarma', 'cultivos: solo un administrador cambia los ajustes'); return; }
          valor.split(',').forEach(p => { const [k, v] = p.split('='); if (k === 'n') cc[i].n = v; else if (k in cc[i]) cc[i][k] = +v; });
          pubCuCfg(); registrar(quien, 'ajustes de ' + cc[i].n);
        }
      }
      else if (cmd.startsWith('config/')) {
        const k = cmd.slice(7);
        if (!esAdmin(quien)) { registrar(quien, 'rechazado: ajuste ' + k + ' (no es administrador)'); emitir(B1 + 'alarma', 'config: ' + quienTxt(quien) + ' no es administrador'); return; }
        cfg[k] = parseFloat(msg); pubC(); registrar(quien, 'ajuste ' + k + ' = ' + parseFloat(msg).toFixed(1));
      }
      else if (cmd.startsWith('usuario/')) {
        const a = cmd.slice(8), f = crudo.split(';').map(x => x.trim()), q = f[f.length - 1];
        const k = usuarios.findIndex(u => u.n.toLowerCase() === (f[0] || '').toLowerCase());
        if (a === 'alta' && f.length === 4 && esAdmin(q)) {
          if (k < 0) usuarios.push({ n: f[0], admin: f[1] === 'admin', h: f[2] }); else Object.assign(usuarios[k], { admin: f[1] === 'admin', h: f[2] });
          registrar(q, (k < 0 ? 'alta' : 'modifica') + ' de usuario ' + f[0]);
        } else if (a === 'baja' && f.length === 2 && esAdmin(q) && k >= 0 && usuarios.filter(u => u.admin).length > (usuarios[k].admin ? 1 : 0)) {
          usuarios.splice(k, 1); registrar(q, 'baja de usuario ' + f[0]);
        } else if (a === 'pin' && f.length === 3 && k >= 0 && (esAdmin(q) || usuarios[k].n.toLowerCase() === q.toLowerCase())) {
          usuarios[k].h = f[1]; registrar(q, 'cambia el PIN de ' + usuarios[k].n);
        }
        pubU();
      }
      else if (cmd === 'registro' || cmd === 'riegos') {
        const l = cmd === 'registro' ? reg : riegosL, P = 12, de = Math.max(1, Math.ceil(l.length / P));
        for (let p = 0; p < de; p++) emitir(B1 + cmd, JSON.stringify({ p, de, l: l.slice(p * P, (p + 1) * P) }));
        return;
      }
      else if (cmd === 'historial') {
        const P = 32, de = Math.ceil(hist.length / P), edad = Math.floor((Date.now() % 600e3) / 1000);
        for (let p = 0; p < de; p++) emitir(B1 + 'historial', JSON.stringify({ p, de, n: hist.length, dt: 600, edad, desde: p * P, v: hist.slice(p * P, (p + 1) * P) }));
        return;
      }
      else if (cmd === 'estado') { pubC(); }
      pubE();
    }

    // ---------------- La simulacion avanza sola ----------------
    let ultHist = Date.now();
    const reloj = setInterval(() => {
      // aire: ciclo del dia + ruido
      st.temp = +(aireT(Date.now()) + (st.venteo === 'abierto' ? -1.5 : 0) + (Math.random() - 0.5) * 0.4).toFixed(1);
      st.hum = +(aireH(Date.now()) + (Math.random() - 0.5) * 2).toFixed(1);
      HIB.forEach(h => { h.st.temp = +(st.temp + h.dT + (Math.random() - 0.5) * 0.3).toFixed(1); h.st.hum = Math.min(95, +(st.hum + h.dH + (Math.random() - 0.5)).toFixed(1)); h.st.uptime_s += PASO_MS / 1000; });
      if (venteoHasta && Date.now() > venteoHasta) { st.venteo = st.venteo === 'abriendo' ? 'abierto' : 'cerrado'; venteoHasta = 0; }
      if (st.modo === 'auto' && !venteoHasta) {
        if (st.temp >= cfg.temp_abrir && st.venteo !== 'abierto') moverVenteo('abrir', 'automatico');
        else if (st.temp <= cfg.temp_cerrar && st.venteo !== 'cerrado') moverVenteo('cerrar', 'automatico');
      }
      // cultivos: se secan solos y suben al regar (12 L/min)
      ec.forEach((e, i) => { e.s = Math.max(5, Math.min(95, e.s + (regando === i ? 1.6 : -0.12))); });
      if (regando >= 0) {
        const i = regando, l = 12 / 60 * PASO_MS / 1000;
        lr += l; ec[i].lh += l; ec[i].lm += l; ec[i].lt += l;
        const k = cc[i], auto = rQuien === 'automatico';
        if (Date.now() - rDesde > k.t * 60e3) cerrar('tiempo maximo');
        else if (k.m !== 0 && lr >= k.l) cerrar('llego a ' + k.l + ' L');
        else if (auto && k.m !== 1 && ec[i].s >= k.off) cerrar('suelo en ' + Math.round(ec[i].s) + ' %');
      } else if (st.modo === 'auto') {
        let el = -1;
        ec.forEach((e, i) => { if (cc[i].a && e.s <= cc[i].on && (el < 0 || e.s < ec[el].s)) el = i; });
        if (el >= 0) abrir(el, 'automatico', 'suelo en ' + Math.round(ec[el].s) + ' %, corta por ' + ['humedad', 'litros', 'humedad o litros'][cc[el].m]);
      }
      // un punto nuevo de historial cada 10 min (como el equipo real)
      if (Date.now() - ultHist >= 600e3) { ultHist = Date.now(); hist.shift(); hist.push([Math.round(st.temp * 10), Math.round(st.hum * 10)].concat(ec.map(e => Math.round(e.s)))); }
      st.uptime_s += PASO_MS / 1000;
      pubE(); pubCu(); HIB.forEach(pubH);
    }, PASO_MS);

    const cli = {
      connected: true,
      on(ev, fn) { (manejadores[ev] = manejadores[ev] || []).push(fn); return cli; },
      subscribe(t) {
        [].concat(t).forEach(x => {
          if (!subs.includes(x)) subs.push(x);
          Object.keys(retenidos).forEach(k => { if (coincide(x, k)) { const m = retenidos[k]; setTimeout(() => vivo && entregar(k, m), 20); } });
        });
        return cli;
      },
      unsubscribe(t) { [].concat(t).forEach(x => { const i = subs.indexOf(x); if (i >= 0) subs.splice(i, 1); }); return cli; },
      publish(t, m) { setTimeout(() => vivo && recibir(t, String(m)), 60); return cli; },
      end() { vivo = false; cli.connected = false; clearInterval(reloj); return cli; },
    };
    publicarTodo();
    setTimeout(() => vivo && (manejadores.connect || []).forEach(fn => fn()), 250);
    return cli;
  }

  // Clima de afuera de ejemplo (para la pestana Clima, sin internet)
  function climaEjemplo() {
    const dias = ['Domingo', 'Lunes', 'Martes', 'Miércoles', 'Jueves', 'Viernes', 'Sábado'];
    const d = (n) => { const f = new Date(Date.now() + n * 86400e3); return dias[f.getDay()] + ' ' + f.getDate(); };
    const ahora = ahoraS(), hhmm = new Date(Date.now() - 300e3).toLocaleTimeString('es-AR', { hour: '2-digit', minute: '2-digit', hour12: false });
    return {
      revisado: ahora, fuente: 'demo', errores: [],
      estacion: { nombre: 'Las Chacras', vientoDe: 'San Luis Rural', hora: hhmm, temp: 21.4, hum: 48, vientoDir: 'NE', viento: 14, lluvia1h: 0, lluvia24h: 0, lluvia30d: 82.4,
                  suelo: [19.2, 18.8, 18.1], maxHoy: 26, minHoy: 9, rafagaHoy: 38, rafagaHora: '15:10' },
      pronostico: { estacion: 'La Punta', hoyMin: 9, hoyMax: 26, dias: [
        { dia: d(1), min: 7, max: 26, cielo: 'Despejado', lluvia: false },
        { dia: d(2), min: 1, max: 19, cielo: 'Ventoso / Parcialmente Nublado', lluvia: false },
        { dia: d(3), min: 9, max: 24, cielo: 'Nublado', lluvia: true }] },
      alertas: [{ id: 'demo-viento', evento: 'Viento', nivel: 'amarillo', desde: ahora - 1800, hasta: ahora + 6 * 3600, cierra: true, fuente: 'SMN',
                  texto: 'EJEMPLO DE DEMOSTRACIÓN. El área será afectada por vientos del sector sur con velocidades entre 35 y 50 km/h y ráfagas que pueden superar los 70 km/h.' }],
    };
  }

  // Vuelve todo a fabrica: borra lo guardado de la demo (no toca nada del equipo real)
  function reiniciar() {
    try { Object.keys(localStorage).filter(k => k === GUARDA || k.startsWith('demo.')).forEach(k => localStorage.removeItem(k)); } catch (e) {}
  }

  window.DemoVivero = { crear, climaEjemplo, reiniciar, usuarios: 'Admin (PIN 1234) · Operario (PIN 0000)' };
})();
