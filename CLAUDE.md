# Inventory_kaizen — Claude Code Rules

This repo is the **knowledge base** for our Inventory Management System project. Several developers work on different workstreams here (API data fetching, voice-to-search, and more). Each piece of work is a small, self-contained task that checks one thing against the **source of truth** and records the result.

Your job as Claude Code: keep this repo organised so anyone can find what was tried, how to run it, and what was learned.

## 1. Where code goes (most important rule)

Never put files in the repo root. Every task lives in its own folder inside a workstream folder:

```
<workstream>/<YYYY-MM-DD>-<short-task-name>/
```

Example: `api-data-fetching/2026-10-05-service-account-auth-check/`

Current workstreams (top-level folders):

| Folder | What it covers |
|---|---|
| `api-data-fetching/` | Fetching inventory data through the API; verifying the service account works |
| `voice-search/` | Voice-to-search over inventory data using the API |
| `shared/` | Code or notes used by more than one workstream (API client helpers, sample data, glossary) |

Rules:
- Before creating files, ask (or infer from the user's request) **which workstream** the work belongs to. If it is unclear, ask. Do not guess.
- If the work does not fit an existing workstream, create a new top-level folder in `kebab-case` and add a `README.md` for it, then add a row to the table above and to the root `README.md`.
- Never write into another workstream's folder unless the user explicitly says to.
- Reusable code that two workstreams need goes in `shared/`, not copied between folders.
- Folder and file names: lowercase `kebab-case`. Python modules may use `snake_case`.

## 2. Every task folder must have a README.md

Copy `templates/TASK_README.md` into the new task folder and fill it in. It must state:
- **Goal**: the one question this task answers
- **Source of truth**: which system, endpoint, sheet or doc the result is checked against
- **How to run**: exact commands and required env vars (names only, never values)
- **Result**: what actually happened, with real output or numbers
- **Status**: `working`, `failed`, or `inconclusive`, plus next steps

A task without a README is not finished. Record failures and dead ends too: they are knowledge.

## 3. Secrets: never commit them

This project uses a service account and API credentials. Never commit:
- Service account JSON key files, `.env` files, API keys, tokens, passwords
- Real customer or inventory data that is not already public. Use small anonymised samples.

Rules:
- Read secrets from environment variables. In docs, list only the **variable names** (for example `GOOGLE_APPLICATION_CREDENTIALS`), and provide a `.env.example` with placeholder values.
- Before every commit, check the staged files for anything credential-like. If you find one, stop, unstage it, and tell the user.
- If a secret was already committed, tell the user immediately. It must be rotated, and deleting the file in a later commit does not remove it from history.

## 4. Commits

Use [Conventional Commits](https://www.conventionalcommits.org/) with the **workstream as the scope**:

```
<type>(<workstream>): <short imperative summary>
```

Types: `feat` (new task or capability), `fix`, `docs`, `test`, `refactor`, `chore`.

Good:
- `feat(api-data-fetching): add service account auth check for inventory API`
- `docs(voice-search): record accuracy results for Bengali queries`
- `fix(shared): handle pagination in API client`

Bad: `update`, `changes`, `first commit`, `final`, `fix stuff`.

Rules:
- Keep the summary under 72 characters, imperative mood ("add", not "added").
- Add a body when the *why* is not obvious, or when a result matters (for example "service account returns 403 on /items; needs inventory.read role").
- **One logical change per commit, and one workstream per commit.** If a change touches two workstreams, make two commits.
- Stage specific paths (`git add api-data-fetching/<task>/`), never `git add .` or `git add -A`.
- Run `git status` and `git diff --staged` before committing.
- End commits with the `Co-Authored-By` line the harness provides.

## 5. Pushing

- Only push when the user asks you to.
- Run `git pull --rebase origin main` before pushing. Never force-push. Never rewrite history that is already pushed.
- If the pull brings conflicts, stop and show the user. Do not resolve another developer's changes on your own.
- Check `git config user.name` and `git config user.email` before the first commit in a session and tell the user whose identity will be used, so commits are attributed to the right person.

## 6. Writing style for docs and code

- Match the language already used in the task (Python, Node, etc.). Keep scripts small and runnable on their own, with the dependencies listed in a `requirements.txt` or `package.json` in the task folder.
- Put the result in the README, not in chat only. Paste real (non-secret) output.
- Prefer facts over opinions: "returned 200 with 143 items" beats "works fine".
- If the API response disagrees with the source of truth, say so plainly in the README, with an example of the mismatch.

## 7. Before you finish a task

1. The task folder is inside the right workstream.
2. `README.md` is filled in, including the real result.
3. No secrets or large data dumps are staged.
4. The commit message follows section 4.
5. Tell the user what you added, where it is, and whether it is pushed.
