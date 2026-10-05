# Repo admin checklist (GitHub settings)

These settings live on GitHub, so they cannot be set from the repo files. The repo owner completes this once, and re-checks when ownership or team size changes. Tick items off and put the date next to them.

## Ownership and access

- [ ] The repo is **private** (Settings, General, Danger zone, Change visibility). It holds connection details and findings about a company system
- [ ] The repo belongs to the **company organisation**, not a personal account, so it survives people leaving (Settings, General, Transfer ownership)
- [ ] Every contributor uses a GitHub account with **two-factor authentication**
- [ ] Access is granted through a **team** (for example `inventory-developers`) with Write permission, not by adding people one at a time
- [ ] At least two people have Admin
- [ ] `.github/CODEOWNERS` has real handles or teams for each domain (it currently defaults to the repo owner)

## Secret protection (Settings, Code security)

- [ ] **Secret scanning** enabled
- [ ] **Push protection** enabled (blocks pushes that contain recognised secrets)
- [ ] **Dependabot alerts** enabled (cheap, and useful once scripts gain dependencies)
- [ ] An owner is named in [SECURITY.md](../SECURITY.md) for rotating leaked credentials

## Branch rules (Settings, Rules, Rulesets)

The team starts with direct pushes to `main` (see [ADR 0001](decisions/0001-knowledge-base-conventions.md)). Turn on the stricter setup when any of these happens: more than about five regular contributors, a first bad merge, or a leaked secret.

- [ ] Now: block **force pushes** and **branch deletion** on `main`
- [ ] Now: require the **validate** status check (from `.github/workflows/validate.yml`) if you want a red build to be visible on `main`
- [ ] Later: require a **pull request** with 1 approval and **code owner review** before merging to `main`. Then tell Claude Code to use branches (see CLAUDE.md, "If push is rejected")

## Repository hygiene

- [ ] Description and topics set (for example `knowledge-base`, `inventory`, `claude-code`)
- [ ] Default branch is `main`
- [ ] Actions are enabled so the validate workflow runs
- [ ] Issues are enabled (the repo ships a "New domain" and a "Knowledge gap" template)
- [ ] Wiki and Projects are off unless the team decides to use them. One home for knowledge is this repo

## Periodic review (monthly)

- [ ] Remove people who left from the team
- [ ] Skim [INDEX.md](../INDEX.md) for tasks stuck in `in-progress` and sources not verified recently
- [ ] Check the validate workflow is still green on `main`
