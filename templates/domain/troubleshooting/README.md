# Troubleshooting

One file per problem, named `<kebab-case-slug>.md`. Copy [templates/PROBLEM.md](../../templates/PROBLEM.md) to start.

Why one file per problem: two people can add problems at the same time without merge conflicts, and each fix can be found by its slug or by searching the exact error text.

The list of problems is generated into the domain [README](../README.md) and the root [INDEX](../../INDEX.md). Run `node scripts/validate.mjs --write` after adding a file.
