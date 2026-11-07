// Service worker minimo: solo para que Chrome/Edge ofrezcan "Instalar app".
// No guarda copias: la app siempre se carga de internet (asi toma las actualizaciones).
self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (e) => e.waitUntil(self.clients.claim()));
self.addEventListener('fetch', () => {});
