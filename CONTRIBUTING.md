# Contributing

All work here is done through Claude Code, which follows [CLAUDE.md](CLAUDE.md). This page is the short version for humans.

1. Set up once: [docs/getting-started.md](docs/getting-started.md).
2. Tell Claude Code which domain and what question you are answering. Small tasks, one question each.
3. Review what it wrote. The task README must say what was checked, against which source of truth, which sources were visited, and which obstacles were hit and fixed.
4. Run `node scripts/validate.mjs`. Fix anything it reports.
5. Commit as `<type>(<scope>): <summary>` and push (Claude Code does this at the end of a task).

Never commit secrets or real customer data. If you think you did, read [SECURITY.md](SECURITY.md) right away.

If the team has switched to pull requests, work on a branch and fill in the pull request template.
