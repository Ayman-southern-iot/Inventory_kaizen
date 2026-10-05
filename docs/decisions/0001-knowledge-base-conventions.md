# 0001: Knowledge base conventions

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: repo owner and contributors

## Context
Several developers work on different areas of one Inventory Management System project, all through Claude Code. We need one place that records what was checked, against which source of truth, how to connect, and which obstacles were hit, in a form that stays organised as the project and the team grow. Advice written only in prose gets skipped, especially by an AI assistant working fast.

## Decision
1. **Layout**: one top-level folder per domain; inside it `tasks/<date>-<name>/` for each small task, plus living files `sources.md`, `connections.md` and a `troubleshooting/` folder with one file per problem. Nothing but a fixed set of files lives in the repo root.
2. **Enforcement, not just advice**: `scripts/validate.mjs` (Node, no dependencies) checks structure, required README sections, links, generated indexes and scans for secrets. It runs in a pre-commit hook, in CI, and Claude Code runs it before committing. A commit-msg hook enforces `<type>(<scope>): <summary>`.
3. **Generated indexes**: domain task and problem tables, the root domain list and `INDEX.md` are generated, so they never drift and nobody edits them by hand.
4. **Secrets**: never in the repo; variable names only. Layered defence: `.gitignore`, content and filename scan in hook and CI, Claude Code deny rules, GitHub push protection.
5. **Pushing**: for now Claude Code pushes finished tasks straight to `main` after validation and `pull --rebase`. Never force-push.

## Alternatives considered
- **Pull request for every change**: safer review, but heavy for small knowledge notes with a small team. Kept as the upgrade path (see [repo-admin-checklist.md](../repo-admin-checklist.md)).
- **Hand-maintained indexes**: simple, but they drifted in our own review, so we generate them.
- **Python validator**: not installed reliably on contributors' machines. Node is already present on CI runners and common on developer machines.
- **A wiki**: separate from the code and the history, with no review, and not read by Claude Code by default.

## Consequences
- Contributors need Node 18+ for the hooks. The hook fails with a clear message if it is missing.
- Rules live in `CLAUDE.md` and are backed by checks, so a skipped rule is caught instead of silently shipped.
- Revisit direct pushes to `main` when the team passes about five regular contributors, or after the first bad merge or leaked secret.
