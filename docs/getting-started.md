# Getting started

Ten minutes from zero to your first documented task.

## You need

- A GitHub account with access to this repo (it must be private, see [repo-admin-checklist.md](repo-admin-checklist.md))
- Git, and [Node.js](https://nodejs.org) 18 or newer (the validator and git hooks run on Node)
- [Claude Code](https://claude.com/claude-code). All work in this repo is done through it

## One-time setup

1. **Clone the repo**
   ```bash
   git clone https://github.com/Ayman-southern-iot/Inventory_kaizen.git
   cd Inventory_kaizen
   ```
2. **Set your own git identity** for this clone, so your commits are attributed to you:
   ```bash
   git config user.name "Your Name"
   git config user.email "your-company-or-github-noreply-address"
   ```
   Use a company address or your GitHub noreply address. Do not use an address you do not want in permanent history.
3. **Enable the safety hooks** (Claude Code does this for you at session start, but do it once yourself too):
   ```bash
   git config core.hooksPath .githooks
   ```
   They scan for secrets, validate the structure and check commit messages. Never bypass them with `--no-verify`.
4. **Keep credentials outside the repo.** Store key files somewhere like `~/.config/inventory-kaizen/` (or a secret manager) and point to them with an environment variable, for example `GOOGLE_APPLICATION_CREDENTIALS`. Check [connections.md](../api-data-fetching/connections.md) in the relevant domain for the exact variable names.

## Do a task

Open Claude Code in the repo folder and say what you are doing. Examples:

- "New task in api-data-fetching: check that the service account can list inventory items and compare the count with the source of truth."
- "New task in voice-search: test how well speech-to-text handles item names, and record the sources you used."
- "I'm starting work on barcode scanning. Create a new domain for it."

Claude Code then creates the task folder, documents sources and obstacles as it goes, runs the validator, commits with a proper message and pushes. See [CLAUDE.md](../CLAUDE.md) for the rules it follows.

## Find existing knowledge

1. [INDEX.md](../INDEX.md): every domain, task and known problem in one place
2. The domain README: owners, scope, tasks, problems
3. Search by exact error text: `git grep -i "the error message"`
4. [glossary.md](glossary.md) for terms used in this project

## Check your work

```bash
node scripts/validate.mjs          # check everything
node scripts/validate.mjs --write  # refresh generated indexes
```

CI runs the same check on every push.
