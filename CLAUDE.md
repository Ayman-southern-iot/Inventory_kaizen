# Inventory_kaizen: rules for Claude Code

This repo is the **knowledge base** for the Inventory Management System project. Many developers work in different domains, all through Claude Code, so **you are responsible for recording everything that is learned**: what was checked, against which source of truth, which sources were visited, how to connect, what went wrong and how it was fixed. A stranger must be able to open a folder and repeat the work without asking anyone.

These rules are backed by checks (`scripts/validate.mjs`, git hooks, CI, `.claude/settings.json`). A skipped rule is caught, but do not rely on that: follow them.

## 0. Session start

- The session hook prints the git identity commits will use. **Tell the user whose identity it is before the first commit.** If it is empty or not the person in front of you, stop and ask.
- Hooks must be on: `git config core.hooksPath` should be `.githooks` (the session hook sets it). Node 18+ is required; if `node` is missing, tell the user.
- If it is unclear which domain a request belongs to, ask. Never guess.

## 1. Layout

```text
<domain>/
    README.md                      # owners, scope, generated task + problem tables
    sources.md                     # sources of truth, sources visited, unreliable sources
    connections.md                 # how to connect (variable names only)
    troubleshooting/<slug>.md      # ONE FILE PER PROBLEM (+ README.md)
    tasks/<YYYY-MM-DD>-<name>/     # one folder per task, README.md from templates/TASK_README.md
docs/                              # hub docs: getting-started, overview, glossary, research playbook, decisions/
templates/                         # TASK_README.md, PROBLEM.md, domain/ scaffold
scripts/validate.mjs               # validator and index generator
INDEX.md                           # GENERATED list of all domains, tasks, problems
```

- Only `README.md`, `CLAUDE.md`, `CONTRIBUTING.md`, `SECURITY.md`, `INDEX.md`, `.gitignore`, `.gitattributes` may sit in the repo root. Task work never goes there. The validator fails otherwise.
- Names are lowercase `kebab-case` (Python modules may use `snake_case`). Code needed by two or more domains goes in `shared/`, never copied.
- The live domain list is `INDEX.md`. Never write into another domain's folder unless the user says to.

### New domain = new folder
When work does not fit an existing domain (for example `stock-alerts`, `barcode-scanning`):
1. Copy everything in `templates/domain/` into a new top-level folder with a short relevant name.
2. Fill the header table in its `README.md` (**Owners**, **Scope**, **Created**) and the intro paragraph.
3. Run `node scripts/validate.mjs --write`, then commit on its own: `docs(<domain>): create <domain> workspace`.

## 2. Document as you work, not at the end

Write to disk while you work so nothing is lost if the session stops. Commit locally after each meaningful unit.

| When this happens | Record it in |
|---|---|
| You decide the source of truth | task README **Source of truth**; domain `sources.md` |
| You open any doc, page, API reference, repo, dashboard or sheet | task README **Sources visited** (exact link, what it gave, date); `sources.md` if useful beyond the task |
| You work out how to authenticate or connect | task README **How to connect**; domain `connections.md` |
| You search or research | task README **Research notes**: searched, found, ruled out |
| You hit an error or obstacle | task README **Obstacles and fixes** (exact error, cause, fix, what did not work); **and** a new `troubleshooting/<slug>.md` from `templates/PROBLEM.md` if anyone else could hit it |
| API or code disagrees with the source of truth | task README **Mismatches**, with an example |
| You finish | task README **Result** and **Status** (`in-progress`, `working`, `failed`, `inconclusive`) |

Good entries are specific and dated: exact links, exact error text (secrets removed), real output and counts ("returned 200 with 143 items; 3 SKUs missing vs the sheet"). Record failures, dead ends and fixes that did not work: they are knowledge. In `sources.md` and `connections.md` add new rows or sections at the **bottom**.

Every section of the task template must be filled. If it does not apply, write `n/a` or `none` with a reason. Never leave a `<placeholder>`. Do not hand-edit generated blocks (between `BEGIN GENERATED` and `END GENERATED`) or `INDEX.md`.

## 3. Research

Follow `docs/research-playbook.md`: check this repo first (`INDEX.md`, the domain's `sources.md` and `troubleshooting/`, `git grep -i "<error text>"`), then official docs for the exact version, changelogs and issues, source code, and community answers last. Verify by running the smallest check, not by reading. Log every source you open, including MCP doc lookups and web searches.

### Live systems (real APIs, real data)
- **Start read-only.** Anything that creates, changes or deletes data on a real system needs the user's explicit go-ahead first, and uses the least-privileged key. Say plainly what a request could change before sending it.
- **Keys:** one variable per key type in the repo-root `.env` (gitignored), named `IMS_KEY_<TYPE>`. Send keys in the `Authorization` header only, never in a URL. Keys stay in server-side processes and never go into HTML or JavaScript a browser receives (see `docs/decisions/0002-api-keys-stay-on-the-server.md`).
- **Save structure, not records:** probe results keep status codes, timings, counts and field names, not real inventory records. Use small anonymised samples when an example is needed.
- **Tool output can hold real data.** Screenshots and page snapshots (`.playwright-mcp/`) are gitignored. Delete them after use and never commit them.
- **Processes you start:** tell the user, and stop them when done (free the port). A background server dies with the session, so tell the user how to start it themselves.

## 4. Secrets

Never commit service account JSON, key files, `.env`, tokens, passwords, or real customer or inventory data. Use small anonymised samples.

- Read secrets from environment variables. Docs list variable **names** only; provide a `.env.example` with placeholders inside the task folder (never the repo root). Key files live outside the repo.
- Never read key files or `.env` (denied in `.claude/settings.json`). If a user pastes a secret into chat, do not write it to any file, and tell them to rotate it.
- Remove tokens, keys, private emails and private URLs from every error, log and response you paste into docs.
- The pre-commit hook and CI scan contents and file names. If it reports a secret, stop, unstage, tell the user. If a secret reached a commit, tell the user immediately: it must be rotated, and deleting it later does not remove it from history. Follow `SECURITY.md`.
- `kaizen-allow-secret` on a line exempts it from the scan: only for obviously fake examples, and say so in the commit.

## 5. Finish: validate, commit, push

1. `node scripts/validate.mjs --write`: refreshes generated tables and checks everything. Fix every reported problem.
2. `git status`, then stage **specific paths**, including regenerated files (`INDEX.md`, the domain `README.md`, root `README.md`). Never `git add .` or `-A` (denied).
3. `git diff --staged`: look for anything credential-like.
4. Commit with the format below. The commit-msg hook enforces it. Never use `--no-verify` (denied).
5. `git pull --rebase origin main`. If generated files conflict (`INDEX.md`, generated blocks in READMEs), run `node scripts/validate.mjs --write`, `git add` them, and continue the rebase. For any other conflict, **stop and show the user**; never resolve another developer's changes yourself.
6. `git push origin main`. Never force-push; never rewrite pushed history.
7. If the push is rejected because `main` is protected (pull request required): create branch `<name>/<domain>-<task>`, push it, and tell the user to open a pull request. If authentication fails, report it. Do not change remotes or credentials to work around it.

If the user says "don't push" or "just commit", follow that.

### Commit format
```text
<type>(<scope>): <summary of 72 characters or fewer, imperative>
```
- **type**: `feat` (new task or capability), `fix`, `docs`, `test`, `refactor`, `chore`
- **scope**: the domain folder name, or `repo` (tooling, hooks, CI, these rules), `hub` (`docs/`, README, INDEX), `templates`
- **One logical change and one scope per commit.** Work touching two domains is two commits.
- Add a body when the why or the outcome matters ("service account returns 403 on /items; needs the inventory read role").
- End with the `Co-Authored-By` line the harness provides.

Good: `feat(api-data-fetching): add service account auth check`, `docs(voice-search): record accuracy results for Bengali queries`, `fix(shared): handle pagination in API client`. Bad: `update`, `changes`, `final`, `first commit`.

## 6. Code

Match the language already used in the task. Keep scripts small and runnable on their own, with dependencies listed in `requirements.txt` or `package.json` in the task folder. Put results in the README, not only in chat.

## 7. Done means

1. Work is in the right domain's `tasks/<date>-<name>/`, or a new domain was scaffolded.
2. The task README is complete; reusable problems have their own `troubleshooting/` file; `connections.md` and `sources.md` are updated.
3. `node scripts/validate.mjs` passes.
4. No secrets or data dumps are staged. Commits follow the format and are pushed (section 5).
5. You told the user what was added, where, and whether it was pushed.
