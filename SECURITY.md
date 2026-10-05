# Security

This repo documents how we connect to company systems. It must never contain the credentials themselves.

## Never commit

Service account key files, `.env` files, API keys, tokens, passwords, private keys, or real customer and inventory data. Keep key files outside the repo and read them through environment variables. Documents list variable **names** only.

## Layers of protection

1. `.gitignore` blocks common key and env file names
2. The pre-commit hook scans staged file names and contents (`scripts/validate.mjs`)
3. CI scans every tracked file on each push
4. Claude Code is denied from reading key files and `.env` (`.claude/settings.json`)
5. GitHub secret scanning and push protection (see [docs/repo-admin-checklist.md](docs/repo-admin-checklist.md))

None of these is perfect. Stay careful when pasting errors, logs and responses into docs.

## If a secret was committed or pushed

Do these in order and do not wait:

1. **Tell the repo owner** (see [CODEOWNERS](.github/CODEOWNERS)) straight away.
2. **Rotate the secret** in the system that issued it: disable the old service account key, API key or token and create a new one. Assume it is compromised from the moment it was pushed. Deleting the file in a later commit does **not** remove it from history.
3. **Check for misuse** in that system's audit logs for the time it was exposed.
4. **Remove it from history** if the repo is shared (for example with `git filter-repo`), after rotation, and ask every contributor to re-clone.
5. **Record the incident** as a problem file in the relevant domain's `troubleshooting/` (cause and prevention, never the secret), and use that to improve the checks.

## Reporting a vulnerability

Tell the repo owner directly and privately. Do not open a public issue containing details.
