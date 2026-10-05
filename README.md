# Inventory_kaizen

Knowledge base for our Inventory Management System project.

Developers work on different domains, all through Claude Code. Every piece of work is a small task that checks something against the **source of truth** and records everything learned: sources visited, how to connect, how to research, the obstacles hit and how they were fixed. Anyone should be able to pick up a domain later without asking around.

## Domains

| Folder | Covers |
|---|---|
| [api-data-fetching/](api-data-fetching/) | Fetching inventory data through the API; verifying the service account works |
| [voice-search/](voice-search/) | Voice-to-search over inventory data using the API |
| [shared/](shared/) | Code and notes used by more than one domain |

New area of work? Create a new folder with a relevant name from [templates/domain/](templates/domain/). Claude Code does this when asked, see [CLAUDE.md](CLAUDE.md).

## Layout

```
<domain>/
    README.md                    # what the domain is, task index
    sources.md                   # sources of truth and sources visited
    connections.md               # how to connect (variable names only)
    troubleshooting.md           # problem -> cause -> fix
    <YYYY-MM-DD>-<task-name>/
        README.md                # goal, source of truth, sources visited, how to connect,
                                 # research notes, obstacles and fixes, result, status
        ...code...
```

Start a task from [templates/TASK_README.md](templates/TASK_README.md).

## Conventions

- Commits: `<type>(<domain>): <summary>`, for example `feat(api-data-fetching): add service account auth check`
- Never commit secrets, key files or `.env` files. Document variable names only.
- Full rules (also read by Claude Code): [CLAUDE.md](CLAUDE.md)
