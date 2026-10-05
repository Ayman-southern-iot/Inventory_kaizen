# IMS API key scope probe

| | |
|---|---|
| **Domain** | `api-data-fetching` |
| **Owner** | Mahmud |
| **Date** | 2026-10-05 |
| **Status** | in-progress |

## Goal
For each of the five IMS API key types (Inventory, Catalogue, Storage locations, Receive stock, Take stock), confirm the key authenticates and can do exactly what its scope promises, and record how to connect. Done so far: the Inventory (read) key. The four write key types are still to test.

## Source of truth
The IMS API reference page and the IMS "Issue key" form, which together state which scope each key type grants and which endpoints each scope opens. The reference page says it is "generated from the running API, so it is never out of date", which is why it is trusted for endpoints and scopes. For data counts, the source of truth is the IMS web UI (comparison still pending, see Next steps).

## Sources visited
Every doc, page, API reference, repo, dashboard or sheet opened during this task.

| Date | Source (exact link / name) | What it gave us | Useful? |
|---|---|---|---|
| 2026-10-05 | IMS API reference page, pasted by Mahmud (exact URL not given, so not recorded) | All endpoints with scopes, query parameters, body fields, example `curl` calls, 120 requests per minute limit, 100 records per page, 180-day expiry for write keys | Yes |
| 2026-10-05 | IMS "Issue key" form, screenshot | Five key types and their labels, which ones are marked "Changes data", expiry default of 90 days | Yes |
| 2026-10-05 | Live API, `https://ims.siot.solutions/api/v1/...` | Real response shapes, error codes, rate-limit headers, latency | Yes |

## How to connect
Send the key as `Authorization: Bearer <key>` to `https://ims.siot.solutions`. Full details, scopes and behaviour are in [connections.md](../../connections.md#ims-api-inventory-management-system).

Required environment variables (repo-root `.env`, gitignored; copy the names from [.env.example](.env.example)):
- `IMS_KEY_INVENTORY_READ`: key with the Inventory scope (`inventory:read`)
- `IMS_KEY_CATALOG_WRITE`: key with the Catalogue scope (`catalog:write`)
- `IMS_KEY_LOCATIONS_WRITE`: key with the Storage locations scope (`locations:write`)
- `IMS_KEY_STOCK_RECEIVE`: key with the Receive stock scope (`stock:receive`)
- `IMS_KEY_STOCK_TAKE`: key with the Take stock scope (`stock:take`)
- `IMS_BASE_URL`: optional, defaults to `https://ims.siot.solutions`

The existing variable `api_key_inventory_only_read` was renamed to `IMS_KEY_INVENTORY_READ` (same value) so every key type follows one naming scheme.

## How to run
Needs Node 18 or newer. The script never prints or saves the key and saves no inventory records, only statuses, timings, counts and field names to `results/`.

```bash
# Safe, GET only. Also pages through every product to count them.
node api-data-fetching/tasks/2026-10-05-ims-api-key-scope-probe/probe.mjs --key inventory-read --all-pages

# For a write key type, additionally send EMPTY-BODY write requests (cannot create or change data):
node api-data-fetching/tasks/2026-10-05-ims-api-key-scope-probe/probe.mjs --key catalog-write --probe-writes
```

Key types: `inventory-read`, `catalog-write`, `locations-write`, `stock-receive`, `stock-take`.

How `--probe-writes` reads the answer: 401 means the key was rejected, 403 means the scope is missing, and 400, 404, 409 or 422 mean the scope was accepted and the request stopped at validation (the request body is empty or the id does not exist, so nothing is written). A 2xx is flagged loudly in the output.

## Research notes
- The reference page is the only source of the scope-to-endpoint mapping, so the probe checks it against live behaviour instead of trusting it.
- Chose to treat a validation error (400, 404, 409, 422) on an empty write request as proof the scope was accepted. This is an inference about how the API orders its checks (scope before validation). It must be confirmed with each write key: a key with the wrong scope should return 403 on the same request. If a write endpoint returns 2xx on an empty body, that inference is wrong for that endpoint.
- Deliberately not tested: sending a key in the URL (`?api_key=`). The reference page warns the key then goes into server logs, browser history and the Referer header. Not worth leaking a key to verify a documented refusal.
- Deliberately not tested yet: real writes (creating a category, receiving or taking stock). They change production data and need an agreed test record and clean-up plan first.

## Obstacles and fixes

| # | Problem / exact error (secrets removed) | Cause | Fix | Did not work |
|---|---|---|---|---|
| 1 | `.env: not allowed in the repo root. Task work belongs in <domain>/tasks/...` from `node scripts/validate.mjs` | The validator treated the local, gitignored `.env` as a stray root file | Fixed the validator to skip git-ignored files. See [validator-flags-root-env-file](../../troubleshooting/validator-flags-root-env-file.md) | n/a |

## Result
Inventory (read) key, tested 2026-10-05 against `https://ims.siot.solutions`. All 8 asserted checks passed.

| Check | Result |
|---|---|
| No `Authorization` header | 401 `UNAUTHENTICATED` |
| Wrong key | 401 `API_KEY_INVALID` |
| `GET /api/v1/catalogue` | 200, 65 products, 46 categories (flat), 18 locations (flat) |
| `GET /api/v1/categories` | 200, 10 top-level tree nodes |
| `GET /api/v1/locations` | 200, 8 zones |
| `GET /api/v1/locations/rooms` | 200, 2 rooms |
| `GET /api/v1/products?limit=100` | 200, `{items, page, limit, total}`, 65 items |
| `GET /api/v1/products/:id` | 200, stock totals, 1 placement, 0 active borrows (first product) |
| Page through all products | 65 products over 1 page; `total` = 65 |

- **Three independent counts agree on 65 products:** the `total` in the product list, the product count in the catalogue, and the number counted by paging.
- **Rate limit headers seen:** limit 120 per minute, enforced both per key and per key plus address, reset 60 seconds.
- **Latency:** about 80 to 320 ms per call.
- **Not yet tested:** the four write key types, and the optional `includeInactive` and `allCategories` parameters.
- Response shapes (field names) are in [connections.md](../../connections.md#endpoints-verified-so-far-inventory-read-get). The saved run is in `results/`.

## Mismatches
None found so far between the reference page and live behaviour for the read endpoints. One naming inconsistency to be aware of, not a mismatch: the form says "Catalogue", the scope is `catalog:write`, and the read endpoint is `/api/v1/catalogue` with scope `inventory:read`.

The 10 top-level categories (tree) versus 46 categories (flat list in the catalogue) are consistent with a tree flattening, but this was not verified node by node.

## Next steps
- [ ] Compare the API's counts (65 products, 46 categories, 18 locations, 2 rooms, 8 zones) with the IMS web UI
- [ ] Test `catalog-write` with `--probe-writes`: expect scope accepted on categories and products, scope missing (403) on the rest
- [ ] Test `locations-write` with `--probe-writes`
- [ ] Test `stock-receive` with `--probe-writes`
- [ ] Test `stock-take` with `--probe-writes`
- [ ] Confirm whether write keys can also read inventory (does a write-only key get 403 on `GET /api/v1/products`?). The probe records this for every key
- [ ] Agree a safe real-write test (a clearly named test category and test product) and a clean-up plan, then record it as a separate task
- [ ] Add the exact URL of the API reference page to [sources.md](../../sources.md)
