#!/usr/bin/env node
// Probes the IMS API with ONE API key and records what that key can and cannot do.
// Needs Node 18+. No dependencies. Never prints or stores the key, and stores no inventory records
// (only status codes, timings, counts and field names).
//
//   node probe.mjs --key inventory-read                  safe read-only checks (GET only)
//   node probe.mjs --key inventory-read --all-pages      also page through every product to count them
//   node probe.mjs --key catalog-write --probe-writes    also send EMPTY-BODY write requests (see below)
//
// Key types: inventory-read, catalog-write, locations-write, stock-receive, stock-take
// Keys live in the repo-root .env (gitignored). See .env.example for the variable names.
//
// --probe-writes sends write requests with deliberately invalid (empty) bodies or non-existent ids,
// so they cannot create or change data. The status tells us whether the key's scope was accepted:
//   401 = key rejected, 403 = scope missing, 400/404/409/422 = scope accepted (stopped at validation).
// If any such request returns 2xx the script flags it loudly, because something may have changed.
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { randomUUID } from 'node:crypto';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const KEYS = {
  'inventory-read': { env: 'IMS_KEY_INVENTORY_READ', scope: 'inventory:read' },
  'catalog-write': { env: 'IMS_KEY_CATALOG_WRITE', scope: 'catalog:write' },
  'locations-write': { env: 'IMS_KEY_LOCATIONS_WRITE', scope: 'locations:write' },
  'stock-receive': { env: 'IMS_KEY_STOCK_RECEIVE', scope: 'stock:receive' },
  'stock-take': { env: 'IMS_KEY_STOCK_TAKE', scope: 'stock:take' },
};
const READS = [
  { name: 'catalogue', path: '/api/v1/catalogue', scope: 'inventory:read' },
  { name: 'categories', path: '/api/v1/categories', scope: 'inventory:read' },
  { name: 'locations', path: '/api/v1/locations', scope: 'inventory:read' },
  { name: 'rooms', path: '/api/v1/locations/rooms', scope: 'inventory:read' },
  { name: 'products', path: '/api/v1/products?limit=100', scope: 'inventory:read' },
];
const WRITES = [
  { name: 'create category', method: 'POST', path: '/api/v1/categories', scope: 'catalog:write', body: {} },
  { name: 'edit category', method: 'PATCH', path: '/api/v1/categories/probe-nonexistent-id', scope: 'catalog:write', body: {} },
  { name: 'create product', method: 'POST', path: '/api/v1/products', scope: 'catalog:write', body: {} },
  { name: 'edit product', method: 'PATCH', path: '/api/v1/products/probe-nonexistent-id', scope: 'catalog:write', body: {} },
  { name: 'create zone', method: 'POST', path: '/api/v1/locations/zones', scope: 'locations:write', body: {} },
  { name: 'create compartment', method: 'POST', path: '/api/v1/locations/compartments', scope: 'locations:write', body: {} },
  { name: 'receive stock', method: 'POST', path: '/api/v1/stock/receive', scope: 'stock:receive', body: {}, idempotent: true },
  { name: 'take stock', method: 'POST', path: '/api/v1/stock/take', scope: 'stock:take', body: {}, idempotent: true },
];
const PAUSE_MS = 600; // limit is 120 requests per minute

// ---------- args and env ----------
const argv = process.argv.slice(2);
const flag = (f) => argv.includes(f);
const keyType = argv[argv.indexOf('--key') + 1];
if (!KEYS[keyType]) {
  console.error(`Usage: node probe.mjs --key <${Object.keys(KEYS).join('|')}> [--all-pages] [--probe-writes]`);
  process.exit(2);
}
loadEnv(resolve(here, '../../../.env'));
const { env: envName, scope: keyScope } = KEYS[keyType];
const apiKey = (process.env[envName] || '').trim();
if (!apiKey) {
  console.error(`${envName} is empty or missing in the repo-root .env, so ${keyType} is skipped. Paste the key there (it is gitignored).`);
  process.exit(3);
}
if (!apiKey.startsWith('ims_')) console.error(`warning: ${envName} does not start with "ims_"; the API may reject it.`);
const baseUrl = (process.env.IMS_BASE_URL || 'https://ims.siot.solutions').replace(/\/+$/, '');

function loadEnv(path) {
  if (!existsSync(path)) return;
  if (typeof process.loadEnvFile === 'function') return process.loadEnvFile(path);
  for (const line of readFileSync(path, 'utf8').split(/\r?\n/)) {
    const m = line.match(/^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$/);
    if (m && !(m[1] in process.env)) process.env[m[1]] = m[2].trim().replace(/^["']|["']$/g, '');
  }
}

// ---------- helpers ----------
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const redact = (s) => String(s).split(apiKey).join('[REDACTED]');

// Describes a JSON value by structure only: field names, array lengths, types. Never values.
function shape(v, depth = 0) {
  if (Array.isArray(v)) return { type: 'array', length: v.length, item: v.length && depth < 2 ? shape(v[0], depth + 1) : undefined };
  if (v && typeof v === 'object') {
    if (depth >= 2) return { type: 'object', fields: Object.keys(v).length };
    return { type: 'object', fields: Object.fromEntries(Object.entries(v).map(([k, x]) => [k, shape(x, depth + 1)])) };
  }
  return v === null ? 'null' : typeof v;
}

async function call(method, path, { auth = true, body, idem, bearer } = {}) {
  const headers = { Accept: 'application/json' };
  if (auth) headers.Authorization = `Bearer ${bearer ?? apiKey}`;
  if (body !== undefined) headers['Content-Type'] = 'application/json';
  if (idem) headers['idempotency-key'] = randomUUID();
  const t0 = Date.now();
  let res;
  try {
    res = await fetch(baseUrl + path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body), signal: AbortSignal.timeout(20000) });
    if (res.status === 429) {
      const wait = Math.min(Number(res.headers.get('retry-after')) || 10, 60);
      console.log(`  429 rate limited, waiting ${wait}s and retrying once`);
      await sleep(wait * 1000);
      return call(method, path, { auth, body, idem, bearer });
    }
  } catch (e) {
    return { status: 0, ms: Date.now() - t0, error: redact(e.cause?.code || e.message), json: undefined, headers: {} };
  }
  let json;
  try { json = await res.json(); } catch { json = undefined; }
  const rate = {};
  for (const [k, v] of res.headers) if (/ratelimit|retry-after/i.test(k)) rate[k] = v;
  return { status: res.status, ms: Date.now() - t0, json, headers: rate, errorText: res.ok ? undefined : redact(JSON.stringify(json ?? '').slice(0, 200)) };
}

function findArray(json) {
  if (Array.isArray(json)) return json;
  if (json && typeof json === 'object') for (const v of Object.values(json)) if (Array.isArray(v)) return v;
  return [];
}

const results = [];
function record(name, method, path, expected, r, extra = {}) {
  let verdict = '';
  if (expected) verdict = expected(r.status) ? 'PASS' : 'FAIL';
  const row = { name, method, path, status: r.status, ms: r.ms, verdict, ...(r.error && { error: r.error }), ...(r.errorText && { apiError: r.errorText }), ...extra };
  results.push(row);
  console.log(`${String(r.status).padEnd(4)} ${method.padEnd(6)} ${path.padEnd(44)} ${String(r.ms).padStart(5)}ms  ${verdict.padEnd(4)} ${name}${extra.note ? '  (' + extra.note + ')' : ''}`);
  return row;
}

// ---------- run ----------
console.log(`IMS API probe: key type "${keyType}" (scope ${keyScope}) against ${baseUrl}\n`);
const rateHeaders = {};
let firstProductId;

console.log('Authentication behaviour (no secrets involved)');
let r = await call('GET', '/api/v1/products?limit=1', { auth: false });
record('no Authorization header is rejected', 'GET', '/api/v1/products?limit=1', (s) => s === 401, r); await sleep(PAUSE_MS);
r = await call('GET', '/api/v1/products?limit=1', { bearer: 'ims_invalid_probe_value_0000000000000000' });
record('wrong key is rejected', 'GET', '/api/v1/products?limit=1', (s) => s === 401, r); await sleep(PAUSE_MS);

console.log('\nRead endpoints (GET)');
for (const e of READS) {
  r = await call('GET', e.path);
  Object.assign(rateHeaders, r.headers);
  const arr = findArray(r.json);
  const row = record(e.name, 'GET', e.path, keyScope === e.scope ? (s) => s === 200 : null, r,
    r.status === 200 ? { shape: shape(r.json), topLevelArrayLength: arr.length } : {});
  if (e.name === 'products' && r.status === 200) firstProductId = arr[0]?.id;
  await sleep(PAUSE_MS);
}
if (firstProductId) {
  r = await call('GET', `/api/v1/products/${encodeURIComponent(firstProductId)}`);
  record('one product (first from list)', 'GET', '/api/v1/products/:id', keyScope === 'inventory:read' ? (s) => s === 200 : null, r,
    r.status === 200 ? { shape: shape(r.json) } : {}); await sleep(PAUSE_MS);
}

if (flag('--all-pages') && firstProductId) {
  console.log('\nPaging through all products (limit=100)');
  let total = 0; let page = 1; let meta;
  for (; page <= 200; page++) {
    r = await call('GET', `/api/v1/products?limit=100&page=${page}`);
    if (r.status !== 200) break;
    const n = findArray(r.json).length;
    total += n;
    if (page === 1 && r.json && !Array.isArray(r.json)) meta = Object.fromEntries(Object.entries(r.json).filter(([, v]) => typeof v !== 'object'));
    if (n < 100) break;
    await sleep(PAUSE_MS);
  }
  record(`counted ${total} products over ${page} page(s)`, 'GET', '/api/v1/products?page=N', null, r, { totalProductsCounted: total, pagingMeta: meta });
}

if (flag('--probe-writes')) {
  console.log('\nWrite endpoints with EMPTY bodies / non-existent ids (cannot create or change data)');
  for (const e of WRITES) {
    r = await call(e.method, e.path, { body: e.body, idem: e.idempotent });
    const accepted = [400, 404, 409, 422].includes(r.status);
    const read = r.status === 403 ? 'scope missing' : r.status === 401 ? 'key rejected' : accepted ? 'scope accepted' : r.status >= 200 && r.status < 300 ? 'WARNING: 2xx on an empty request, check for unintended changes' : 'unexpected';
    record(e.name, e.method, e.path, keyScope === e.scope ? () => accepted : null, r, { note: read, interpretation: read, requiresScope: e.scope });
    await sleep(PAUSE_MS);
  }
}

// ---------- summary and save ----------
const asserted = results.filter((x) => x.verdict);
const failed = asserted.filter((x) => x.verdict === 'FAIL');
console.log(`\n${asserted.length - failed.length}/${asserted.length} asserted checks passed. Rate-limit headers seen: ${Object.keys(rateHeaders).length ? JSON.stringify(rateHeaders) : 'none'}`);

const stamp = new Date().toISOString().replace(/[-:]/g, '').replace(/\..+/, '').replace('T', '-');
const out = { date: new Date().toISOString(), baseUrl, keyType, scopeTested: keyScope, envVariable: envName, node: process.version, writesProbed: flag('--probe-writes'), results };
const text = JSON.stringify(out, null, 2);
if (text.includes(apiKey)) { console.error('refusing to save: output contains the key'); process.exit(4); }
mkdirSync(join(here, 'results'), { recursive: true });
const file = join(here, 'results', `${keyType}-${stamp}.json`);
writeFileSync(file, text + '\n');
console.log(`saved ${file.replace(here, '.')} (statuses, timings, counts and field names only)`);
process.exitCode = failed.length ? 1 : 0;
