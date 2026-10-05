# Validator flags the local .env file as a stray root file

| | |
|---|---|
| **Domain** | `api-data-fetching` |
| **Date** | 2026-10-05 |
| **Task** | [2026-10-05-ims-api-key-scope-probe](../tasks/2026-10-05-ims-api-key-scope-probe/README.md) |

## Symptom
After creating a repo-root `.env` to hold API keys, `node scripts/validate.mjs` (and therefore the pre-commit hook) failed:

```text
- .env: not allowed in the repo root. Task work belongs in <domain>/tasks/<date>-<name>/ (see CLAUDE.md)
```

## Cause
The root layout check listed every file in the repo root, including files git ignores. The `.env` is gitignored and never committed, so it should not count as a layout problem.

## Fix
Update to the latest `scripts/validate.mjs`, which skips root entries that git ignores. The `.env` stays in the repo root, gitignored. Do not move it into a task folder or commit it.

## Tried and did not work
Nothing else was tried. Adding `.env` to the allowed root files list would also have worked, but would have hidden a real problem if `.env` were ever force-added to a commit.

## Prevention
The validator now only checks files git would track. A forced `git add -f .env` is still caught by the file-name check in the pre-commit hook.
