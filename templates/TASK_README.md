# <Task title>

| | |
|---|---|
| **Domain** | `<domain-folder-name>` |
| **Owner** | <name> |
| **Date** | <YYYY-MM-DD> |
| **Status** | <in-progress / working / failed / inconclusive> |

<!--
Copy this file to <domain>/tasks/<YYYY-MM-DD>-<kebab-case-name>/README.md.
Fill in every section while you work. If a section does not apply, write "n/a" or "none" and say why.
Never paste secrets: names of environment variables only. Remove tokens, keys and private data from errors and logs.
-->

## Goal
<The one question this task answers.>

## Source of truth
<Which system, endpoint, sheet or doc the result is checked against. Exact endpoint or link. Why this one is trusted.>

## Sources visited
Every doc, page, API reference, repo, dashboard or sheet opened during this task.

| Date | Source (exact link / name) | What it gave us | Useful? |
|---|---|---|---|
| | | | |

## How to connect
<Auth method, base URL, scopes/roles needed, the steps that finally worked. Environment variable names only.>

Required environment variables:
- `EXAMPLE_VAR`: <what it is>

## How to run
```bash
# exact commands, in order
```

## Research notes
<What was searched for, what was found, what was ruled out and why.>

## Obstacles and fixes

| # | Problem / exact error (secrets removed) | Cause | Fix | Did not work |
|---|---|---|---|---|
| 1 | | | | |

Reusable problems also get their own file in `../../troubleshooting/<slug>.md`.

## Result
<What actually happened. Real, non-secret output: status codes, counts, sample records, screenshots.>

## Mismatches
<Where the API or code disagrees with the source of truth, with an example. "none" if none.>

## Next steps
- [ ] <follow-up>
