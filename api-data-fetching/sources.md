# Sources

Add new rows at the **bottom** of each table (this avoids merge conflicts when two people edit at once).

## Sources of truth
What results are checked against. State why each one is trusted and who owns it.

| Name | Link / location | What it is authoritative for | Owner | Last verified |
|---|---|---|---|---|
| IMS API reference page | Page in IMS served from the running API (exact URL not recorded yet: ask the IMS owner to add it here). States it is "generated from the running API, so it is never out of date" | Endpoints, scopes, query parameters, body fields, rate limit, key expiry | IMS owner (to confirm) | 2026-10-05 (content matched live behaviour for 6 read endpoints and 2 auth checks; write endpoints not yet tested) |
| IMS "Issue key" form | IMS web app, key issuing screen | Which scope each key type grants (Inventory, Catalogue, Storage locations, Receive stock, Take stock) | IMS owner (to confirm) | 2026-10-05 |
| IMS web UI inventory counts | IMS web app | What the data should look like to a person (product, category and location counts) | IMS owner (to confirm) | Not yet compared: pending a manual check against the API's 65 products |

## Sources visited
Useful docs, references and pages found while working. The detailed per-task list lives in each task README.

| Date | Source | What it is good for | Task |
|---|---|---|---|
| 2026-10-05 | IMS API reference page (pasted by Mahmud) | Full endpoint list with scopes, parameters, example `curl` calls, limits | [ims-api-key-scope-probe](tasks/2026-10-05-ims-api-key-scope-probe/README.md) |
| 2026-10-05 | IMS "Issue key" form (screenshot) | Scope names and which ones change data | [ims-api-key-scope-probe](tasks/2026-10-05-ims-api-key-scope-probe/README.md) |
| 2026-10-05 | Live API at `https://ims.siot.solutions/api/v1/...` | Real response shapes, error codes, rate-limit headers | [ims-api-key-scope-probe](tasks/2026-10-05-ims-api-key-scope-probe/README.md) |
| 2026-10-05 | Live `GET /api/v1/catalogue` | Product fields for a card or search screen; no push feed, so poll | [live-product-cards](tasks/2026-10-05-live-product-cards/README.md) |

## Unreliable or outdated sources
Sources that turned out to be wrong, so nobody trusts them again.

| Source | Problem | Date found |
|---|---|---|
| | | |
