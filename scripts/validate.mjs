#!/usr/bin/env node
// Validates the Inventory_kaizen knowledge base and keeps generated indexes current.
// Zero dependencies. Needs Node 18+.
//
//   node scripts/validate.mjs                  check everything (CI, local)
//   node scripts/validate.mjs --staged         same, but secret-scan only staged files (pre-commit)
//   node scripts/validate.mjs --write          regenerate README/INDEX/domain blocks, then check
//   node scripts/validate.mjs --commit-msg F   check the commit message in file F
import { readFileSync, writeFileSync, existsSync, readdirSync, statSync } from 'node:fs';
import { join, dirname, relative, resolve, sep } from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);

// ---------- configuration ----------
const NON_DOMAIN_DIRS = new Set(['docs', 'templates', 'scripts', 'node_modules']);
const ROOT_FILES = new Set(['README.md', 'CLAUDE.md', 'CONTRIBUTING.md', 'SECURITY.md', 'INDEX.md', '.gitignore', '.gitattributes']);
const DOMAIN_ITEMS = new Set(['README.md', 'sources.md', 'connections.md', 'troubleshooting', 'tasks']);
const EXTRA_COMMIT_SCOPES = ['repo', 'hub', 'templates'];
const COMMIT_TYPES = ['feat', 'fix', 'docs', 'test', 'refactor', 'chore'];
const STATUSES = new Set(['in-progress', 'working', 'failed', 'inconclusive']);
const TASK_SECTIONS = ['goal', 'source of truth', 'sources visited', 'how to connect', 'how to run',
  'research notes', 'obstacles and fixes', 'result', 'mismatches', 'next steps'];
const PROBLEM_SECTIONS = ['symptom', 'cause', 'fix'];
const DATE_RE = /^\d{4}-\d{2}-\d{2}$/;
const TASK_DIR_RE = /^(\d{4}-\d{2}-\d{2})-[a-z0-9][a-z0-9-]*$/;
const SLUG_FILE_RE = /^[a-z0-9][a-z0-9-]*\.md$/;
const MAX_FILE_BYTES = 5 * 1024 * 1024;
const ALLOW_MARKER = 'kaizen-allow-secret';

const SECRET_PATTERNS = [
  ['private key block', /-----BEGIN (?:[A-Z]+ )*PRIVATE KEY-----/],
  ['service account key JSON', /"type"\s*:\s*"service_account"/],
  ['private_key field', /"private_key"\s*:\s*"/],
  ['AWS access key id', /\bAKIA[0-9A-Z]{16}\b/],
  ['Google API key', /\bAIza[0-9A-Za-z_-]{35}\b/],
  ['GitHub token', /\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{36,}\b|\bgithub_pat_[A-Za-z0-9_]{20,}\b/],
  ['Slack token', /\bxox[baprs]-[A-Za-z0-9-]{10,}/],
  ['LLM provider API key', /\bsk-(?:ant-)?[A-Za-z0-9_-]{20,}/],
  ['JWT', /\beyJ[A-Za-z0-9_-]{10,}\.eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}/],
  ['Bearer token', /\bBearer\s+[A-Za-z0-9._~+/-]{24,}/],
  ['hard-coded secret assignment', /\b(?:api[_-]?key|secret|token|passw(?:or)?d|client[_-]?secret)\b["']?\s*[:=]\s*["'][A-Za-z0-9_\-/+=.]{16,}["']/i],
  ['URL with embedded password', /\b[a-z][a-z0-9+.-]*:\/\/[^\s:@/]+:[^\s:@/]{3,}@[^\s/]+/i],
];
const BAD_NAME_PARTS = ['service-account', 'service_account', 'serviceaccount', 'credential', 'secret', 'private-key', 'private_key'];

// ---------- small helpers ----------
const errors = [];
const warnings = [];
const err = (m) => errors.push(m);
const warn = (m) => warnings.push(m);
const norm = (s) => s.replace(/\r\n/g, '\n');
const readText = (p) => norm(readFileSync(p, 'utf8'));
const posix = (p) => p.split(sep).join('/');
const rel = (p) => posix(relative(ROOT, p));
const esc = (s) => String(s).replace(/\|/g, '\\|').replace(/\n/g, ' ');

function git(args) {
  try {
    return execFileSync('git', args, { cwd: ROOT, maxBuffer: 512 * 1024 * 1024, stdio: ['ignore', 'pipe', 'ignore'] });
  } catch {
    return null;
  }
}

function walk(dir, out = []) {
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    if (e.name === '.git' || e.name === 'node_modules') continue;
    const p = join(dir, e.name);
    e.isDirectory() ? walk(p, out) : out.push(rel(p));
  }
  return out;
}

function allFiles() {
  const tracked = git(['ls-files', '-z']);
  if (tracked === null) return walk(ROOT).sort();
  const others = git(['ls-files', '-z', '-o', '--exclude-standard']);
  const set = new Set();
  for (const buf of [tracked, others]) {
    if (buf) buf.toString('utf8').split('\0').filter(Boolean).forEach((f) => set.add(f));
  }
  return [...set].filter((f) => existsSync(join(ROOT, f))).sort();
}

function stagedFiles() {
  const out = git(['diff', '--cached', '--name-only', '--diff-filter=ACMR', '-z']);
  return out ? out.toString('utf8').split('\0').filter(Boolean) : [];
}

const isDir = (p) => existsSync(p) && statSync(p).isDirectory();

// "| **Key** | value |" rows -> { key: value }
function meta(text) {
  const m = {};
  for (const line of text.split('\n')) {
    const r = line.match(/^\|\s*\*\*([^*|]+)\*\*\s*\|\s*(.*?)\s*\|?\s*$/);
    if (r) m[r[1].trim().toLowerCase()] = r[2].replace(/^`+|`+$/g, '').trim();
  }
  return m;
}

// level-2 sections -> Map(lowercase heading -> body), ignoring headings inside code fences
function sections(text) {
  const map = new Map();
  let cur = null;
  let fence = false;
  for (const line of text.split('\n')) {
    if (/^```/.test(line)) fence = !fence;
    const h = !fence && line.match(/^##\s+(.+?)\s*$/);
    if (h) {
      cur = h[1].toLowerCase();
      map.set(cur, []);
    } else if (cur) {
      map.get(cur).push(line);
    }
  }
  return new Map([...map].map(([k, v]) => [k, v.join('\n')]));
}

function findSection(map, name) {
  for (const [k, v] of map) if (k === name || k.startsWith(name)) return v;
  return null;
}

// Returns a problem string if the section body is empty, a leftover template placeholder,
// or a table with no filled rows. Returns null when it looks filled in.
function sectionProblem(body) {
  const text = body.replace(/<!--[\s\S]*?-->/g, '');
  const lines = text.split('\n').filter((l) => l.trim());
  if (!lines.length) return 'is empty (write "n/a" or "none" with a reason if it does not apply)';
  if (lines.some((l) => /^\s*<[^<>\n]{2,}>\.?\s*$/.test(l))) return 'still has a template <placeholder> line';
  const rows = lines.filter((l) => /^\s*\|/.test(l));
  if (rows.length) {
    const data = rows.filter((l, i) => i > 0 && !/^\s*\|[\s:|-]+\|\s*$/.test(l));
    const nonTable = lines.filter((l) => !/^\s*\|/.test(l));
    const filled = data.some((l) => l.split('|').slice(1, -1).some((c) => c.trim()));
    if (!filled && nonTable.length <= 2) return 'has an empty table (put "none" or "n/a" in the first cell if nothing applies)';
  }
  return null;
}

function firstLine(body) {
  const l = body.split('\n').map((x) => x.trim()).find((x) => x && !x.startsWith('<') && !x.startsWith('|') && !x.startsWith('```'));
  if (!l) return '';
  return l.length > 110 ? `${l.slice(0, 107)}...` : l;
}

function checkMetaDate(where, value) {
  if (!DATE_RE.test(value || '') || Number.isNaN(Date.parse(value))) err(`${where}: Date must be a real YYYY-MM-DD date (got "${value || ''}")`);
}

// ---------- structure checks ----------
function checkRoot() {
  for (const e of readdirSync(ROOT, { withFileTypes: true })) {
    if (e.name.startsWith('.') && e.isDirectory()) continue;
    if (e.isFile() && !ROOT_FILES.has(e.name)) {
      err(`${e.name}: not allowed in the repo root. Task work belongs in <domain>/tasks/<date>-<name>/ (see CLAUDE.md)`);
    }
  }
}

function listDomains() {
  return readdirSync(ROOT, { withFileTypes: true })
    .filter((e) => e.isDirectory() && !e.name.startsWith('.') && !NON_DOMAIN_DIRS.has(e.name))
    .map((e) => e.name)
    .sort();
}

function checkTask(domain, dirName) {
  const readme = join(ROOT, domain, 'tasks', dirName, 'README.md');
  const where = `${domain}/tasks/${dirName}/README.md`;
  if (!existsSync(readme)) {
    err(`${where} missing (copy templates/TASK_README.md)`);
    return null;
  }
  const text = readText(readme);
  const m = meta(text);
  for (const k of ['domain', 'owner', 'date', 'status']) {
    if (!m[k] || m[k].startsWith('<')) err(`${where}: fill in the **${k[0].toUpperCase()}${k.slice(1)}** row of the header table`);
  }
  if (m.domain && !m.domain.startsWith('<') && m.domain !== domain) err(`${where}: Domain says "${m.domain}" but the folder is in "${domain}"`);
  if (m.date && !m.date.startsWith('<')) checkMetaDate(where, m.date);
  if (m.status && !m.status.startsWith('<') && !STATUSES.has(m.status)) err(`${where}: Status "${m.status}" must be one of ${[...STATUSES].join(', ')}`);
  const secs = sections(text);
  for (const name of TASK_SECTIONS) {
    const body = findSection(secs, name);
    if (body === null) {
      err(`${where}: missing section "## ${name}"`);
      continue;
    }
    const p = sectionProblem(body);
    if (p) err(`${where}: section "${name}" ${p}`);
  }
  return { date: dirName.match(TASK_DIR_RE)[1], name: dirName, owner: m.owner || '', status: m.status || '', goal: firstLine(findSection(secs, 'goal') || '') };
}

function checkProblem(domain, file) {
  const where = `${domain}/troubleshooting/${file}`;
  const text = readText(join(ROOT, domain, 'troubleshooting', file));
  const m = meta(text);
  const title = (text.match(/^#\s+(.+?)\s*$/m) || [])[1] || '';
  if (!title || title.startsWith('<')) err(`${where}: needs a real "# title" line`);
  if (m.domain && m.domain !== domain) err(`${where}: Domain says "${m.domain}" but the file is in "${domain}"`);
  if (!m.date || m.date.startsWith('<')) err(`${where}: fill in the **Date** row of the header table`);
  else checkMetaDate(where, m.date);
  const secs = sections(text);
  for (const name of PROBLEM_SECTIONS) {
    const body = findSection(secs, name);
    if (body === null) {
      err(`${where}: missing section "## ${name}"`);
      continue;
    }
    const p = sectionProblem(body);
    if (p) err(`${where}: section "${name}" ${p}`);
  }
  return { slug: file.replace(/\.md$/, ''), file, title, date: m.date || '' };
}

function checkDomain(name) {
  const d = join(ROOT, name);
  const readme = join(d, 'README.md');
  if (!existsSync(readme)) {
    err(`${name}/README.md missing. A new top-level folder is a new domain: scaffold it from templates/domain/`);
    return null;
  }
  const text = readText(readme);
  const m = meta(text);
  for (const k of ['owners', 'scope']) {
    if (!m[k] || m[k].startsWith('<')) err(`${name}/README.md: fill in the **${k[0].toUpperCase()}${k.slice(1)}** row of the header table`);
  }
  const info = { name, owners: m.owners || '', scope: m.scope || '', tasks: [], problems: [] };
  if (name === 'shared') return info;

  for (const f of ['sources.md', 'connections.md', 'troubleshooting/README.md']) {
    if (!existsSync(join(d, f))) err(`${name}/${f} missing (copy from templates/domain/)`);
  }
  if (!isDir(join(d, 'tasks'))) err(`${name}/tasks/ missing`);
  for (const key of ['tasks', 'problems']) {
    if (!text.includes(`<!-- BEGIN GENERATED: ${key} -->`) || !text.includes(`<!-- END GENERATED: ${key} -->`)) {
      err(`${name}/README.md: missing the generated "${key}" block markers (copy from templates/domain/README.md)`);
    }
  }
  for (const e of readdirSync(d, { withFileTypes: true })) {
    if (!DOMAIN_ITEMS.has(e.name)) err(`${name}/${e.name}: unexpected. Work goes in ${name}/tasks/<date>-<name>/, problems in ${name}/troubleshooting/<slug>.md`);
  }
  if (isDir(join(d, 'tasks'))) {
    for (const e of readdirSync(join(d, 'tasks'), { withFileTypes: true })) {
      if (e.isFile()) {
        if (e.name !== '.gitkeep') err(`${name}/tasks/${e.name}: files are not allowed here, use a task folder`);
        continue;
      }
      if (!TASK_DIR_RE.test(e.name)) {
        err(`${name}/tasks/${e.name}: task folders must be named <YYYY-MM-DD>-<kebab-case-name>`);
        continue;
      }
      const t = checkTask(name, e.name);
      if (t) info.tasks.push(t);
    }
  }
  if (isDir(join(d, 'troubleshooting'))) {
    for (const e of readdirSync(join(d, 'troubleshooting'), { withFileTypes: true })) {
      if (e.name === 'README.md' || e.name === '.gitkeep') continue;
      if (!e.isFile() || !SLUG_FILE_RE.test(e.name)) {
        err(`${name}/troubleshooting/${e.name}: use one lowercase kebab-case .md file per problem`);
        continue;
      }
      info.problems.push(checkProblem(name, e.name));
    }
  }
  info.tasks.sort((a, b) => b.date.localeCompare(a.date) || a.name.localeCompare(b.name));
  info.problems.sort((a, b) => b.date.localeCompare(a.date) || a.slug.localeCompare(b.slug));
  return info;
}

// ---------- links ----------
function checkLinks(files) {
  for (const f of files) {
    if (!f.endsWith('.md') || f.startsWith('templates/')) continue;
    const text = readText(join(ROOT, f)).replace(/^```[\s\S]*?^```/gm, '');
    for (const m of text.matchAll(/\[[^\]]*\]\(([^)\s]+)\)/g)) {
      let target = m[1];
      if (/^(https?:|mailto:|#)/.test(target) || /[<>]/.test(target)) continue;
      target = decodeURIComponent(target.split('#')[0]);
      if (!target) continue;
      if (!existsSync(resolve(ROOT, dirname(f), target))) err(`${f}: broken link -> ${m[1]}`);
    }
  }
}

// ---------- secrets ----------
function badFilename(path) {
  const base = path.split('/').pop();
  const lower = base.toLowerCase();
  if (lower === '.env' || (lower.startsWith('.env.') && !/\.(example|sample|template)$/.test(lower))) return 'env file';
  if (/\.(pem|p12|pfx|key|jks|keystore)$/.test(lower)) return 'key/certificate file';
  if (lower.endsWith('.json')) {
    if (BAD_NAME_PARTS.some((p) => lower.includes(p))) return 'looks like a credentials JSON file';
    if (/(^|[-_.])keys?([-_.]|\.json$)/.test(lower)) return 'looks like a key file';
    if (/-[0-9a-f]{12}\.json$/.test(lower)) return 'looks like a Google Cloud service account key file (project-<hash>.json)';
  }
  return null;
}

function scanSecrets(files, staged) {
  for (const f of files) {
    if (f === 'scripts/validate.mjs') continue;
    const why = badFilename(f);
    if (why) err(`${f}: ${why}. Keep it outside the repo and read it via an environment variable`);
    const buf = staged ? git(['show', `:${f}`]) : readFileSync(join(ROOT, f));
    if (!buf) continue;
    if (buf.length > MAX_FILE_BYTES) {
      err(`${f}: larger than ${MAX_FILE_BYTES / 1024 / 1024} MB. Do not commit data dumps; use a small anonymised sample`);
      continue;
    }
    if (buf.subarray(0, 8000).includes(0)) continue;
    const lines = buf.toString('utf8').split(/\r?\n/);
    let hits = 0;
    lines.forEach((line, i) => {
      if (hits >= 3 || line.includes(ALLOW_MARKER)) return;
      for (const [label, re] of SECRET_PATTERNS) {
        if (re.test(line)) {
          err(`${f}:${i + 1}: possible secret (${label}). Remove it and rotate it if it is real; add "${ALLOW_MARKER}" on the line only for a fake example`);
          hits++;
          break;
        }
      }
    });
  }
}

// ---------- generated content ----------
const domainsTable = (domains) => (domains.length
  ? ['| Domain | Scope | Owners | Tasks | Problems |', '|---|---|---|---|---|',
    ...domains.map((d) => `| [${d.name}](${d.name}/) | ${esc(d.scope)} | ${esc(d.owners)} | ${d.tasks.length} | ${d.problems.length} |`)].join('\n')
  : '_No domains yet._');

function tasksTable(rows, withDomain) {
  if (!rows.length) return '_No tasks yet._';
  const head = withDomain ? '| Date | Domain | Task | Owner | Status | Goal |' : '| Date | Task | Owner | Status | Goal |';
  const sep2 = withDomain ? '|---|---|---|---|---|---|' : '|---|---|---|---|---|';
  const body = rows.map((t) => {
    const link = `[${t.name.slice(11)}](${t.prefix}tasks/${t.name}/README.md)`;
    const cells = withDomain ? [t.date, t.domain, link, t.owner, t.status, t.goal] : [t.date, link, t.owner, t.status, t.goal];
    return `| ${cells.map(esc).join(' | ')} |`;
  });
  return [head, sep2, ...body].join('\n');
}

function problemsTable(rows, withDomain) {
  if (!rows.length) return '_No problems recorded yet._';
  const head = withDomain ? '| Date | Domain | Problem |' : '| Date | Problem |';
  const sep2 = withDomain ? '|---|---|---|' : '|---|---|';
  const body = rows.map((p) => {
    const link = `[${p.title}](${p.prefix}troubleshooting/${p.file})`;
    return `| ${(withDomain ? [p.date, p.domain, link] : [p.date, link]).map(esc).join(' | ')} |`;
  });
  return [head, sep2, ...body].join('\n');
}

function replaceBlock(text, key, content) {
  const re = new RegExp(`(<!-- BEGIN GENERATED: ${key} -->)[\\s\\S]*?(<!-- END GENERATED: ${key} -->)`);
  return re.test(text) ? text.replace(re, (_, a, b) => `${a}\n${content}\n${b}`) : null;
}

function generated(domains) {
  const out = new Map();
  const flat = (kind) => domains.flatMap((d) => d[kind].map((x) => ({ ...x, domain: d.name, prefix: `${d.name}/` })));
  const allTasks = flat('tasks').sort((a, b) => b.date.localeCompare(a.date) || a.domain.localeCompare(b.domain) || a.name.localeCompare(b.name));
  const allProblems = flat('problems').sort((a, b) => b.date.localeCompare(a.date) || a.domain.localeCompare(b.domain) || a.slug.localeCompare(b.slug));

  out.set('INDEX.md', norm(`<!-- GENERATED FILE. Do not edit by hand. Run: node scripts/validate.mjs --write -->
# Index

Live list of every domain, task and known problem in this knowledge base. Generated from the repo contents.

## Domains

${domainsTable(domains)}

## Tasks

${tasksTable(allTasks, true)}

## Known problems and fixes

${problemsTable(allProblems, true)}
`));

  const rootReadme = join(ROOT, 'README.md');
  if (existsSync(rootReadme)) {
    const next = replaceBlock(readText(rootReadme), 'domains', domainsTable(domains));
    if (next === null) err('README.md: missing the generated "domains" block markers');
    else out.set('README.md', next);
  }
  for (const d of domains) {
    if (d.name === 'shared') continue;
    const p = join(ROOT, d.name, 'README.md');
    if (!existsSync(p)) continue;
    let text = readText(p);
    const withTasks = replaceBlock(text, 'tasks', tasksTable(d.tasks.map((t) => ({ ...t, prefix: '' })), false));
    if (withTasks === null) continue;
    text = withTasks;
    const withProblems = replaceBlock(text, 'problems', problemsTable(d.problems.map((p2) => ({ ...p2, prefix: '' })), false));
    if (withProblems === null) continue;
    out.set(`${d.name}/README.md`, withProblems);
  }
  return out;
}

function syncGenerated(domains, write) {
  const stale = [];
  for (const [file, content] of generated(domains)) {
    const p = join(ROOT, file);
    const current = existsSync(p) ? readText(p) : '';
    if (current === content) continue;
    if (write) {
      writeFileSync(p, content, 'utf8');
      console.log(`updated ${file}`);
    } else {
      stale.push(file);
    }
  }
  if (stale.length) err(`generated content out of date in: ${stale.join(', ')}. Run: node scripts/validate.mjs --write, then stage the files`);
}

// ---------- commit message ----------
function checkCommitMsg(file) {
  const first = (readText(file).split('\n').find((l) => l.trim() && !l.startsWith('#')) || '').trim();
  if (/^(Merge|Revert|fixup!|squash!)/.test(first)) return;
  const m = first.match(new RegExp(`^(${COMMIT_TYPES.join('|')})\\(([a-z0-9-]+)\\): (.+)$`));
  const scopes = [...listDomains(), ...EXTRA_COMMIT_SCOPES];
  if (!m) {
    err(`commit message must look like "<type>(<scope>): <summary>" with type in [${COMMIT_TYPES.join(', ')}] and scope in [${scopes.join(', ')}]. Got: "${first}"`);
    return;
  }
  if (!scopes.includes(m[2])) err(`commit scope "${m[2]}" is not a domain folder or one of [${EXTRA_COMMIT_SCOPES.join(', ')}]. Known scopes: ${scopes.join(', ')}`);
  if (m[3].length > 72) err(`commit summary is ${m[3].length} characters; keep it at 72 or fewer`);
  if (/^(update|changes?|final|fix stuff|wip)\b/i.test(m[3])) err(`commit summary "${m[3]}" is too vague; say what changed`);
}

// ---------- main ----------
function main() {
  if (has('--help')) {
    console.log(readFileSync(fileURLToPath(import.meta.url), 'utf8').split('\n').slice(1, 8).map((l) => l.replace(/^\/\/ ?/, '')).join('\n'));
    return;
  }
  const msgIdx = argv.indexOf('--commit-msg');
  if (msgIdx !== -1) {
    checkCommitMsg(argv[msgIdx + 1]);
  } else {
    const staged = has('--staged');
    checkRoot();
    const domains = listDomains().map(checkDomain).filter(Boolean);
    syncGenerated(domains, has('--write'));
    const files = allFiles();
    checkLinks(files);
    scanSecrets(staged ? stagedFiles() : files, staged);
    if (!domains.length) warn('no domains found');
  }
  for (const w of warnings) console.warn(`warning: ${w}`);
  if (errors.length) {
    console.error(`\n${errors.length} problem(s) found:\n`);
    for (const e of errors) console.error(`  - ${e}`);
    console.error('');
    process.exitCode = 1;
  } else {
    console.log('ok: knowledge base is valid');
  }
}

main();
