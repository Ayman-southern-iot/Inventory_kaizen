# Inventory_kaizen

Knowledge base for our Inventory Management System project.

Each developer works on a different workstream. Every piece of work is a small task that checks something against the **source of truth** and records the result, so the whole team can see what works, what doesn't, and how to reproduce it.

## Workstreams

| Folder | Covers |
|---|---|
| [api-data-fetching/](api-data-fetching/) | Fetching inventory data through the API; verifying the service account works |
| [voice-search/](voice-search/) | Voice-to-search over inventory data using the API |
| [shared/](shared/) | Code and notes used by more than one workstream |

## Layout

```
<workstream>/<YYYY-MM-DD>-<short-task-name>/
    README.md        # goal, source of truth, how to run, result, status
    ...code...
```

Start a new task by copying [templates/TASK_README.md](templates/TASK_README.md) into the new folder.

## Conventions

- Commits: `<type>(<workstream>): <summary>`, for example `feat(api-data-fetching): add service account auth check`
- Never commit secrets, key files or `.env` files. Document variable names only.
- Full rules (also read by Claude Code): [CLAUDE.md](CLAUDE.md)
