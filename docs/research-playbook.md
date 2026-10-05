# Research playbook

How to research a question in this repo so the next person can trust and repeat it. Claude Code follows this when asked to look something up.

## 1. Write the question first

One sentence, in the task README's **Goal**. If you cannot state the question, the task is too big: split it.

## 2. Name the source of truth before searching

Decide what the answer will be checked against (a live API response, a spreadsheet, the vendor's reference docs, the production database). Write it in **Source of truth** with an exact link or endpoint and why it is trusted. If two sources disagree, the source of truth wins, and the disagreement goes in **Mismatches**.

## 3. Search in this order

1. **This repo first.** Check [INDEX.md](../INDEX.md), the domain's `sources.md` and `troubleshooting/`, and `git grep -i "<exact error text>"`. Someone may have solved it.
2. **Official documentation** for the exact version in use. For libraries and APIs prefer current docs over memory, since APIs change.
3. **Vendor changelogs, status pages and issue trackers** for known bugs and recent changes.
4. **Source code** of the library or SDK when docs are unclear.
5. **Community answers** (forums, Q&A sites) last. Treat them as leads, never as proof. Verify by running something.

## 4. Record every source as you go

In the task README **Sources visited**: date, exact link, what it gave, and whether it helped. Add sources that will help others to the domain's `sources.md`. Add sources that were wrong or outdated to its "Unreliable or outdated sources" table.

## 5. Verify by running, not by reading

A source is a claim. Confirm with the smallest runnable check: one request, one query, one script. Record the exact command (no secrets) and the real output in **How to run** and **Result**. Say which versions and which date the result holds for.

## 6. Record what did not work

In **Research notes** and **Obstacles and fixes**: leads that were wrong, fixes that looked right but failed, dead ends. These save the most time later.

## 7. Promote what is reusable

| You learned | Where it goes |
|---|---|
| How to connect or authenticate | domain `connections.md` |
| A problem and its fix | `<domain>/troubleshooting/<slug>.md` |
| A reliable or unreliable source | domain `sources.md` |
| A decision that affects several domains | [decisions/](decisions/README.md) |
| A new term | [glossary.md](glossary.md) |

## Quality bar

- Facts with numbers: "returned 200 with 143 items; 3 SKUs missing vs the sheet", not "works fine".
- Exact error text, copy-pasted, with secrets removed.
- Dated. Knowledge goes stale: put dates on sources, results and "last verified".
- Someone with no context can repeat the task from the README alone.
