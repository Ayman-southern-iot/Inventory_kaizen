#!/usr/bin/env node
// Live product cards: a tiny local server that keeps the IMS API key on the server side.
// It polls the IMS catalogue and pushes every change to open browser pages (server-sent events),
// so the cards update without a refresh. Needs Node 18+. No dependencies.
//
//   node server.mjs                 then open http://localhost:5177
//   POLL_SECONDS=10 PORT=8080 node server.mjs
//
// The key (IMS_KEY_INVENTORY_READ, read from the repo-root .env) is never sent to the browser.
import http from 'node:http';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
loadEnv(resolve(here, '../../../.env'));

const KEY = (process.env.IMS_KEY_INVENTORY_READ || '').trim();
const BASE = (process.env.IMS_BASE_URL || 'https://ims.siot.solutions').replace(/\/+$/, '');
const PORT = Number(process.env.PORT) || 5177;
const POLL_SECONDS = Math.max(2, Number(process.env.POLL_SECONDS) || 5); // API limit: 120 requests per minute
if (!KEY) {
  console.error('IMS_KEY_INVENTORY_READ is empty or missing in the repo-root .env. Paste the Inventory (read) key there.');
  process.exit(3);
}

function loadEnv(path) {
  if (!existsSync(path)) return;
  if (typeof process.loadEnvFile === 'function') return process.loadEnvFile(path);
  for (const line of readFileSync(path, 'utf8').split(/\r?\n/)) {
    const m = line.match(/^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$/);
    if (m && !(m[1] in process.env)) process.env[m[1]] = m[2].trim().replace(/^["']|["']$/g, '');
  }
}

// ---------- state ----------
const redact = (s) => String(s).split(KEY).join('[REDACTED]');
const state = { version: 0, hash: '', snapshot: null, status: { ok: false, error: 'starting', lastOkAt: null, pollSeconds: POLL_SECONDS } };
const clients = new Set();
const send = (res, event, data) => res.write(`event: ${event}\ndata: ${JSON.stringify(data)}\n\n`);
const broadcast = (event, data) => clients.forEach((res) => send(res, event, data));
const stamp = () => new Date().toLocaleTimeString();

function setStatus(ok, error = null) {
  const changed = state.status.ok !== ok || state.status.error !== error;
  state.status = { ok, error, lastOkAt: ok ? new Date().toISOString() : state.status.lastOkAt, pollSeconds: POLL_SECONDS };
  if (changed) {
    broadcast('status', state.status);
    console.log(`[${stamp()}] ${ok ? 'connected to IMS' : `IMS problem: ${error}`}`);
  }
}

// ---------- polling ----------
let failures = 0;
async function poll() {
  let delay = POLL_SECONDS * 1000;
  try {
    const res = await fetch(`${BASE}/api/v1/catalogue`, {
      headers: { Authorization: `Bearer ${KEY}`, Accept: 'application/json' },
      signal: AbortSignal.timeout(15000),
    });
    if (res.status === 429) {
      delay = Math.min(Number(res.headers.get('retry-after')) || 15, 60) * 1000;
      throw new Error('rate limited by IMS (HTTP 429), slowing down');
    }
    if (res.status === 401 || res.status === 403) {
      delay = 30000;
      throw new Error(`key rejected by IMS (HTTP ${res.status}). Check IMS_KEY_INVENTORY_READ`);
    }
    if (!res.ok) throw new Error(`IMS returned HTTP ${res.status}`);
    const j = await res.json();
    if (!Array.isArray(j.products)) throw new Error('unexpected response: no products list');
    failures = 0;
    // generatedAt changes on every call, so it is left out of the change check.
    const hash = createHash('sha1').update(JSON.stringify([j.products, j.categories, j.locations])).digest('hex');
    const fetchedAt = new Date().toISOString();
    if (hash !== state.hash) {
      state.hash = hash;
      state.version += 1;
      state.snapshot = { version: state.version, generatedAt: j.generatedAt, fetchedAt, counts: j.counts, products: j.products };
      console.log(`[${stamp()}] inventory ${state.version === 1 ? 'loaded' : 'changed'}: version ${state.version}, ${j.products.length} products, ${clients.size} page(s) updated`);
      broadcast('snapshot', state.snapshot);
    }
    setStatus(true);
  } catch (e) {
    failures += 1;
    delay = Math.max(delay, Math.min(POLL_SECONDS * 1000 * 2 ** Math.min(failures, 4), 60000));
    setStatus(false, redact(e.name === 'TimeoutError' ? 'IMS did not answer in time' : e.cause?.code ? `network error (${e.cause.code})` : e.message));
  }
  setTimeout(poll, delay);
}

// ---------- http ----------
const ALLOWED_HOSTS = new Set([`localhost:${PORT}`, `127.0.0.1:${PORT}`]);
const SECURITY_HEADERS = {
  'X-Content-Type-Options': 'nosniff',
  'Referrer-Policy': 'no-referrer',
  'Content-Security-Policy': "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'; base-uri 'none'; form-action 'none'",
};

const server = http.createServer((req, res) => {
  // Refuse requests whose Host header is not ours (blocks DNS-rebinding from other websites).
  if (!ALLOWED_HOSTS.has(req.headers.host || '')) {
    res.writeHead(403, SECURITY_HEADERS).end('Forbidden');
    return;
  }
  const path = new URL(req.url, 'http://localhost').pathname;
  if (req.method !== 'GET') {
    res.writeHead(405, SECURITY_HEADERS).end('Method not allowed');
  } else if (path === '/') {
    res.writeHead(200, { ...SECURITY_HEADERS, 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
    res.end(readFileSync(join(here, 'index.html')));
  } else if (path === '/favicon.ico') {
    res.writeHead(204, SECURITY_HEADERS).end();
  } else if (path === '/events') {
    res.writeHead(200, { ...SECURITY_HEADERS, 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-cache', Connection: 'keep-alive' });
    res.write('retry: 3000\n\n');
    send(res, 'status', state.status);
    if (state.snapshot) send(res, 'snapshot', state.snapshot);
    clients.add(res);
    const beat = setInterval(() => res.write(': keep-alive\n\n'), 25000);
    req.on('close', () => { clearInterval(beat); clients.delete(res); });
  } else if (path === '/api/products') {
    res.writeHead(200, { ...SECURITY_HEADERS, 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
    res.end(JSON.stringify({ status: state.status, snapshot: state.snapshot }));
  } else {
    res.writeHead(404, SECURITY_HEADERS).end('Not found');
  }
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`Live product cards: http://localhost:${PORT}  (polling ${BASE} every ${POLL_SECONDS}s; press Ctrl+C to stop)`);
  poll();
});
process.on('SIGINT', () => { server.close(); process.exit(0); });
