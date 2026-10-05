# Inventory_kaizen — Claude Code Rules

This repo is the **knowledge base** for our Inventory Management System project. Several developers work here on different domains (API data fetching, voice-to-search, and more). All the work is done through Claude Code, so **Claude Code is responsible for recording everything** that is learned: what was checked, against which source of truth, which sources were visited, what went wrong, and how it was fixed.

The goal: someone who has never seen the work can open a folder and learn how to connect, how to research, what to avoid, and what the right answer is, without asking anyone.

Every piece of work is a small task. Each task checks something against a **source of truth** and records the result.

## 1. Where things go

Never put files in the repo root. Layout:

```
<domain>/                          # one folder per domain (workstream)
    README.md                      # what this domain is, task index
    sources.md                     # living: sources of truth + every source visited
    connections.md                 # living: how to connect (auth, endpoints, env var names)
    troubleshooting.md             # living: problem -> cause -> fix
    <YYYY-MM-DD>-<task-name>/      # one folder per task
        README.md                  # filled from templates/TASK_README.md
        ...code, sample output...
```

Current domains:

| Folder | What it covers |
|---|---|
| `api-data-fetching/` | Fetching inventory data through the API; verifying the service account works |
| `voice-search/` | Voice-to-search over inventory data using the API |
| `shared/` | Code or notes used by more than one domain (API client helpers, anonymised sample data, glossary) |

### New domain = new folder

If a developer starts work in an area that is not in the table above:
1. Create a new top-level folder with a short, relevant `kebab-case` name (for example `stock-alerts`, `barcode-scanning`, `reporting`).
2. Scaffold it by copying everything in `templates/domain/` into it, then fill in the `README.md`.
3. Add a row to the table above and to the root `README.md`.
4. Commit that as its own commit: `docs(<domain>): create <domain> workspace`.

If you are not sure which domain a request belongs to, ask. Do not guess, and never write into another domain's folder unless the user explicitly says to.

Naming: lowercase `kebab-case` for folders and files. Python modules may use `snake_case`. Code used by two or more domains goes in `shared/`, not copied.

## 2. Document as you work, not at the end

Do not wait until the task is done. Update the files while working, so nothing is lost if the session stops.

| When this happens | Record it here |
|---|---|
| You decide what the source of truth is | Task README **Source of truth**, and `<domain>/sources.md` |
| You read a doc, web page, API reference, repo, dashboard or sheet | Task README **Sources visited** (link, what it gave you, date). Add to `<domain>/sources.md` if it is useful beyond this task |
| You work out how to authenticate or connect | Task README **How to connect**, and `<domain>/connections.md` |
| You search or research something | Task README **Research notes**: what you searched for, what you found, what you ruled out |
| You hit an error or obstacle | Task README **Obstacles and fixes** (exact error, cause, fix), and a generalised entry in `<domain>/troubleshooting.md` |
| The API or code disagrees with the source of truth | Task README **Mismatches**, with an example |
| You finish | Task README **Result** and **Status** |

### What good entries look like
- **Sources visited**: the exact URL, doc name or endpoint, what you took from it, the date. "https://example.com/docs/auth — explains the token scopes; need `inventory.read`" is good. "Looked at docs" is not.
- **Obstacles**: paste the exact error text (with secrets removed), say what the real cause was, and what fixed it. Include fixes that did **not** work, so nobody repeats them.
- **Results**: facts and real output. "Returned 200 with 143 items; 3 SKUs missing vs the sheet" beats "works fine".
- **Dead ends and failures are knowledge.** Record them.

### Promote reusable learnings
After a task, anything another person could hit again moves into the domain's living files:
- A new connection detail or auth step → `connections.md`
- A problem and its fix → `troubleshooting.md` (format: symptom, exact error, cause, fix, link to the task)
- A new reliable source or a source found to be wrong or outdated → `sources.md`

Keep the task README as the detailed story and the living files as the quick reference. Link between them.

## 3. Task README is required

Copy `templates/TASK_README.md` into every new task folder and fill in all sections. A task without a completed README is not finished. If a section does not apply, write "n/a" with a reason; do not delete it.

## 4. Secrets: never commit them

This project uses a service account and API credentials. Never commit:
- Service account JSON key files, `.env` files, API keys, tokens, passwords
- Real customer or inventory data that is not already public. Use small anonymised samples.

Rules:
- Read secrets from environment variables. In docs, list only the **variable names** (for example `GOOGLE_APPLICATION_CREDENTIALS`) and give a `.env.example` with placeholders.
- When pasting errors, logs or responses into docs, remove tokens, keys, private emails, and private URLs first.
- Before every commit, inspect the staged files for anything credential-like. If you find one, stop, unstage it, and tell the user.
- If a secret was already committed, tell the user immediately. It must be rotated; deleting the file in a later commit does not remove it from history.

## 5. Commits

Use [Conventional Commits](https://www.conventionalcommits.org/) with the **domain as the scope**:

```
<type>(<domain>): <short imperative summary>
```

Types: `feat` (new task or capability), `fix`, `docs`, `test`, `refactor`, `chore`.

Good:
- `feat(api-data-fetching): add service account auth check for inventory API`
- `docs(api-data-fetching): add 403 troubleshooting for missing inventory.read role`
- `docs(voice-search): record accuracy results for Bengali queries`
- `fix(shared): handle pagination in API client`

Bad: `update`, `changes`, `first commit`, `final`, `fix stuff`.

Rules:
- Summary under 72 characters, imperative mood.
- Add a body when the *why* or the outcome matters (for example "service account returns 403 on /items; needs inventory.read role").
- **One logical change per commit, and one domain per commit.** If work touches two domains, make two commits.
- Stage specific paths (`git add api-data-fetching/<task>/`), never `git add .` or `git add -A`.
- Run `git status` and `git diff --staged` before committing.
- End commits with the `Co-Authored-By` line the harness provides.

## 6. Pushing

The team relies on this repo being up to date, so **push when a task is finished**:
1. All README sections and living files are updated (section 2).
2. Staged files are checked for secrets (section 4).
3. Commit (section 5), then `git pull --rebase origin main`, then `git push origin main`.

Safety rules:
- Never force-push. Never rewrite history that is already pushed.
- If the pull brings conflicts, stop and show the user. Do not resolve another developer's changes on your own.
- Check `git config user.name` and `git config user.email` at the start of a session and tell the user whose identity commits will use, so commits are attributed to the right person.
- If the push is rejected or authentication fails, report it. Do not try workarounds like changing remotes or credentials.

If the user says "don't push" or "just commit", follow that.

## 7. Code style

- Match the language already used in the task. Keep scripts small and runnable on their own, with dependencies listed in a `requirements.txt` or `package.json` in the task folder.
- Put results in the README, not only in chat.
- If the API response disagrees with the source of truth, say so plainly, with an example.

## 8. Before you finish a task

1. The task folder is in the right domain (or a new domain folder was created per section 1).
2. Task README is complete: goal, source of truth, sources visited, how to connect, research notes, obstacles and fixes, result, status.
3. Living files (`sources.md`, `connections.md`, `troubleshooting.md`) have the reusable learnings, and the domain README task table has a new row.
4. No secrets or large data dumps are staged.
5. The commit follows section 5, and was pushed per section 6.
6. Tell the user what you added, where it is, and whether it was pushed.
