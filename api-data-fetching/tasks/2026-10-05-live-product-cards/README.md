# Live product cards

| | |
|---|---|
| **Domain** | `api-data-fetching` |
| **Owner** | Mahmud |
| **Date** | 2026-10-05 |
| **Status** | working |

## Goal
Show every IMS product as a card in a simple web page using the Inventory (read) key, and have the cards update by themselves, with no page refresh, when the inventory changes in IMS.

## Source of truth
The live IMS catalogue, `GET /api/v1/catalogue`, which the API reference describes as "everything a search screen needs, in one response". The cards must show what IMS shows. Comparison with the IMS web UI is still a manual step (see Next steps).

## Sources visited
Every doc, page, API reference, repo, dashboard or sheet opened during this task.

| Date | Source (exact link / name) | What it gave us | Useful? |
|---|---|---|---|
| 2026-10-05 | IMS API reference page (same one as [ims-api-key-scope-probe](../2026-10-05-ims-api-key-scope-probe/README.md)) | `/api/v1/catalogue` returns products with category path, shelves as labels, and total, available and in-use counts | Yes |
| 2026-10-05 | Live `GET https://ims.siot.solutions/api/v1/catalogue` | Real field names of a product (`id, code, name, description, unit, category{path}, stock{total,available,inUse}, locations[]`) | Yes |
| 2026-10-05 | IMS API reference page, "Limits" section | 120 requests per minute, which sets the polling interval | Yes |

## How to connect
Uses the Inventory (read) key from the repo-root `.env` as `IMS_KEY_INVENTORY_READ`. The key stays inside the local Node server. The browser never sees it. Details: [connections.md](../../connections.md#ims-api-inventory-management-system).

Required environment variables:
- `IMS_KEY_INVENTORY_READ`: Inventory (`inventory:read`) key
- `IMS_BASE_URL`: optional, defaults to `https://ims.siot.solutions`
- `PORT`: optional, defaults to 5177
- `POLL_SECONDS`: optional, defaults to 5, minimum 2

## How to run
Needs Node 18 or newer. No install step.

```bash
node api-data-fetching/tasks/2026-10-05-live-product-cards/server.mjs
```

Then open <http://localhost:5177>. Press Ctrl+C to stop.

Files:
- `server.mjs`: local server. Polls the catalogue, detects changes, pushes them to open pages
- `index.html`: the page. Cards, search, category filter, "in stock only", live status indicator

## Research notes
- **Why not a plain HTML file with the key inside?** Anything in an HTML file is readable by whoever opens the page or its source, and the key would sit in browser history and caches. So the key stays in a small local server and the page only talks to that server.
- **Why polling and server-sent events?** The API documents no websocket, webhook or streaming feed. The server polls the catalogue every 5 seconds (12 requests per minute out of the 120 allowed, and the count stays the same however many pages are open). When the catalogue differs from the last one, the server pushes the new data to every open page over server-sent events (`/events`), which the browser reconnects by itself.
- **Why `/catalogue` and not `/products`?** One request returns the whole picture a card needs, including the category path and readable shelf labels, so a poll is a single call.
- **Change detection ignores `generatedAt`,** which differs on every response and would otherwise look like a change every poll.
- **Worst-case delay** from a change in IMS to the card updating is the poll interval (5 seconds by default) plus under a second to push it.

## Obstacles and fixes

| # | Problem / exact error (secrets removed) | Cause | Fix | Did not work |
|---|---|---|---|---|
| 1 | Browser console: `Failed to load resource: 404 /favicon.ico` | The server had no route for the favicon request the browser makes on its own | The server now answers `/favicon.ico` with 204 | n/a |
| 2 | Browser-tool output folder `.playwright-mcp/` appeared in the repo, holding screenshots of real inventory | The browser test tool saves screenshots and page snapshots in the working directory | Deleted it and added it to `.gitignore` | n/a |

## Result
Working against the real IMS API on 2026-10-05.

- The page showed all 65 of 65 products as cards, matching the 65 products found in [ims-api-key-scope-probe](../2026-10-05-ims-api-key-scope-probe/README.md). The category filter lists all 46 categories. Searching "relay" narrowed it to 1 product. No console errors after the favicon fix.
- **Sync logic tested against a fake IMS server** (13 of 13 checks passed), so no production data was touched:
  - first data arrives on connect;
  - unchanged inventory sends no updates (despite `generatedAt` changing every call);
  - a stock change from 10 to 7 reached the page in 736 ms with a 2-second poll;
  - a rejected key shows a clear banner and the page keeps the last data;
  - the server recovers by itself when the key works again;
  - a network failure is reported without crashing;
  - the key never appears in the HTML, the data endpoint, the event stream or the server log;
  - requests with a foreign `Host` header and non-GET requests are refused;
  - product text is rendered as plain text (`textContent`), so hostile text in a product name or description cannot run as script.
- **Not yet done:** a real end-to-end check where a product is changed in IMS and the open page updates. The inventory read key cannot change data, so this needs someone to edit a product or stock in the IMS UI while the page is open.

## Mismatches
None found. Product count on the page (65) equals the catalogue count and the earlier probe.

## Next steps
- [ ] With the page open, change a product or its stock in the IMS web UI and confirm the card updates within about 5 seconds (the one test that needs a human)
- [ ] Compare the cards with the IMS web UI: 65 products, 46 categories, shelf labels and quantities
- [ ] Decide whether items with no stock should be sorted or flagged differently (the API gives no low-stock threshold, so none was invented)
- [ ] If other people need to open the page over the network, put the server behind proper authentication first. It listens on `127.0.0.1` only and refuses foreign `Host` headers on purpose
