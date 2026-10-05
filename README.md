# Inventory_kaizen

The knowledge base for our Inventory Management System project.

Developers work on different domains, all through Claude Code. Every piece of work is a small task that is checked against a **source of truth** and records everything learned: sources visited, how to connect, how to research, the obstacles hit and how they were fixed. Anyone can pick up a domain later without asking around.

## Start here

| I want to... | Go to |
|---|---|
| Set up and run my first task | [docs/getting-started.md](docs/getting-started.md) |
| See every domain, task and known problem | [INDEX.md](INDEX.md) |
| Understand how the project fits together | [docs/project-overview.md](docs/project-overview.md) |
| Look up a term | [docs/glossary.md](docs/glossary.md) |
| Research something the right way | [docs/research-playbook.md](docs/research-playbook.md) |
| Know why we work this way | [docs/decisions/](docs/decisions/README.md) |
| Report a leaked secret | [SECURITY.md](SECURITY.md) |
| Read the rules Claude Code follows | [CLAUDE.md](CLAUDE.md) |

## Domains

<!-- BEGIN GENERATED: domains -->
| Domain | Scope | Owners | Tasks | Problems |
|---|---|---|---|---|
| [api-data-fetching](api-data-fetching/) | Fetching inventory data through the API and verifying that the service account works. | Mahmud | 0 | 0 |
| [shared](shared/) | Code and notes used by more than one domain: API client helpers, anonymised sample data, glossary | Mahmud, Redwan | 0 | 0 |
| [voice-search](voice-search/) | Voice-to-search over inventory data using the API. | Redwan, Mahmud | 0 | 0 |
<!-- END GENERATED: domains -->

New area of work? Ask Claude Code to create a new domain. It scaffolds a folder from [templates/domain/](templates/domain/).

## How it is organised

```text
<domain>/
    README.md                    owners, scope, task and problem tables (generated)
    sources.md                   sources of truth and sources visited
    connections.md               how to connect (variable names only)
    troubleshooting/<slug>.md    one file per problem: symptom, cause, fix
    tasks/<YYYY-MM-DD>-<name>/   one folder per task
        README.md                goal, source of truth, sources visited, how to connect,
                                 research notes, obstacles and fixes, result, status
```

## Guard rails

- No secrets, ever: variable names only. A pre-commit hook and CI scan every change.
- Commit messages: `<type>(<scope>): <summary>`, for example `feat(api-data-fetching): add service account auth check`.
- `node scripts/validate.mjs` checks structure, links, indexes and secrets. CI runs it on every push.
