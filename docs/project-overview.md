# Project overview

This repo is the shared memory of the Inventory Management System project. Many developers work on different areas at the same time. Each one runs small tasks through Claude Code, and everything learned ends up here in a fixed shape, so nobody has to rediscover it.

## How knowledge flows

```text
task (small, dated)  ->  records sources, how to connect, research, obstacles, result
        |
        +-- reusable problem  ->  <domain>/troubleshooting/<slug>.md
        +-- connection detail ->  <domain>/connections.md
        +-- source found      ->  <domain>/sources.md
        |
        v
validator regenerates  ->  domain README tables, INDEX.md, root README domain list
```

Every claim is checked against a **source of truth** (see [glossary.md](glossary.md)). A task that disagrees with the source of truth says so plainly, with an example.

## Domains

The live list, with owners and counts, is generated in [INDEX.md](../INDEX.md). Domains that exist today are also listed in the root [README](../README.md).

How domains depend on each other (keep this up to date when you find a dependency):

| Domain | Depends on | Why |
|---|---|---|
| voice-search | api-data-fetching | Uses the API connection and service account details verified there |

## Systems of record

Fill this in as tasks establish what is authoritative for what. Detailed notes stay in each domain's `sources.md`.

| System / document | Authoritative for | Owner | Domains that use it | Details |
|---|---|---|---|---|
| | | | | |

## Rules of the road

- Work lives in `<domain>/tasks/<date>-<name>/`. Never in the repo root.
- No secrets, ever. Variable names only. See [SECURITY.md](../SECURITY.md).
- Commits: `<type>(<scope>): <summary>`, scope is a domain folder or `repo`, `hub`, `templates`.
- Big decisions are recorded in [decisions/](decisions/README.md).
- Research follows the [research playbook](research-playbook.md).
