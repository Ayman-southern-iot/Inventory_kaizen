# Browser says "localhost refused to connect" on the live cards page

| | |
|---|---|
| **Domain** | `api-data-fetching` |
| **Date** | 2026-10-05 |
| **Task** | [2026-10-05-live-product-cards](../tasks/2026-10-05-live-product-cards/README.md) |

## Symptom
Opening <http://localhost:5177> in the browser shows:

```text
This site can't be reached
localhost refused to connect.
ERR_CONNECTION_REFUSED
```

## Cause
Nothing was listening on port 5177: the live cards server was not running. Confirmed by checking that no process held the port, then starting the server, after which the page loaded and showed 65 products. The server only runs while its process is alive, so it is gone when the terminal is closed, when the computer restarts, or when the Claude Code session that started it ends. A server started in the background by Claude Code stops with that session.

## Fix
Start the server in a terminal opened in the repo folder, and leave that terminal open:

```bash
node api-data-fetching/tasks/2026-10-05-live-product-cards/server.mjs
```

It prints `Live product cards: http://localhost:5177 ...` and then `connected to IMS`. If it prints an error instead:

| Message | Meaning and fix |
|---|---|
| `IMS_KEY_INVENTORY_READ is empty or missing` | The Inventory (read) key is not in the repo-root `.env`. Paste it there |
| `EADDRINUSE` | Another program uses port 5177. Start with another port: `PORT=5178 node ...` (PowerShell: `$env:PORT=5178; node ...`) and open that port |
| `node` is not recognised | Node.js is not installed or not on that terminal's PATH. Install Node 18 or newer |
| `Cannot find module` or file not found | The command was run from the wrong folder. Run it from the repo root |

## Tried and did not work
Reloading the page. It cannot help while the server is stopped.

## Prevention
Keep the server's terminal open while using the page. If it must run all the time, run it under a process manager or a Windows startup task. That is not set up yet.
