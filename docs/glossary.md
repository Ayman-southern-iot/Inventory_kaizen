# Glossary

Add new terms in alphabetical order. Use the exact words here in READMEs so everyone means the same thing.

| Term | Meaning in this repo |
|---|---|
| **ADR** | Architecture decision record. A short note in [decisions/](decisions/README.md) saying what was decided, why, and what else was considered. |
| **Domain** | A top-level folder for one area of work (for example `api-data-fetching`). Has an owner, a scope, living files and tasks. |
| **Generated block** | A table between `<!-- BEGIN GENERATED -->` and `<!-- END GENERATED -->` markers that `scripts/validate.mjs --write` rewrites. Never edit by hand. |
| **Living file** | A domain file that keeps growing: `sources.md`, `connections.md`, and the `troubleshooting/` folder. |
| **Obstacle** | Anything that blocked or slowed a task: an error, missing permission, unclear doc. Recorded with cause and fix. |
| **Service account** | A non-human identity used to call the inventory API. Its key file is a secret and never enters the repo. |
| **Source of truth** | The one system or document a result is checked against, and trusted when sources disagree. |
| **Source visited** | Any doc, page, repo, dashboard or sheet opened during a task, recorded with link, what it gave, and date. |
| **Task** | One small, dated piece of work in `<domain>/tasks/<date>-<name>/` that answers a single question and records the result. |
