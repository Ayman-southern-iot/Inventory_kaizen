# Connections

How to connect to each system used in this domain. Environment variable **names** only, never values.
Add each new system as a new section at the **bottom**.

## IMS API (Inventory Management System)

| | |
|---|---|
| **Base URL** | `https://ims.siot.solutions` (override with `IMS_BASE_URL`) |
| **Auth method** | API key sent as `Authorization: Bearer <key>`. Keys start with `ims_` |
| **Scopes** | One per key type, chosen in the IMS "Issue key" form. See the table below |
| **Env vars** | `IMS_BASE_URL` (optional) and one variable per key type: `IMS_KEY_INVENTORY_READ`, `IMS_KEY_CATALOG_WRITE`, `IMS_KEY_LOCATIONS_WRITE`, `IMS_KEY_STOCK_RECEIVE`, `IMS_KEY_STOCK_TAKE` |
| **Where the credential lives** | Repo-root `.env` (gitignored, local to each developer). Never committed, never pasted into docs or chat. Names are listed in [the task's `.env.example`](tasks/2026-10-05-ims-api-key-scope-probe/.env.example) |
| **Verified working** | `inventory-read` key: 2026-10-05, [ims-api-key-scope-probe](tasks/2026-10-05-ims-api-key-scope-probe/README.md). Other key types: not yet tested |

### Key types and scopes

| Form label (IMS "Issue key") | Scope | Changes data? | Allows |
|---|---|---|---|
| Inventory | `inventory:read` | No | `GET` catalogue, categories, locations, rooms, products, product by id |
| Catalogue | `catalog:write` | Yes | `POST`/`PATCH` categories, `POST`/`PATCH` products |
| Storage locations | `locations:write` | Yes | `POST` zones, `POST` compartments |
| Receive stock | `stock:receive` | Yes | `POST /api/v1/stock/receive` (needs `idempotency-key` header) |
| Take stock | `stock:take` | Yes | `POST /api/v1/stock/take` (needs `idempotency-key` header; the service account becomes the holder) |

Naming trap: the form says "Catalogue", the scope is spelled `catalog:write` (US spelling), and the read endpoint is `/api/v1/catalogue` (UK spelling) and needs `inventory:read`. Do not mix them up.

### Endpoints verified so far (`inventory-read`, GET)

| Endpoint | Returns (field names only) |
|---|---|
| `/api/v1/catalogue` | object: `generatedAt`, `products[]`, `categories[]` (flat), `locations[]` (flat), `counts{products,categories,locations}` |
| `/api/v1/categories` | array of nested tree nodes: `id`, `name`, `parentId`, `isTrackable`, `isActive`, `productCount`, `productCountInTree`, `children[]`, `createdAt`, `updatedAt` |
| `/api/v1/locations` | array of zones: `id`, `name`, `roomId`, `roomName`, `isActive`, `compartments[]` |
| `/api/v1/locations/rooms` | array of rooms: `id`, `name`, `isActive`, `zones[]` |
| `/api/v1/products?limit=100&page=N` | object: `items[]`, `page`, `limit`, `total` |
| `/api/v1/products/:id` | one product with stock totals (`totalQuantity`, `totalAvailable`, `totalReserved`, `totalOnHand`, `totalQuarantined`, `totalInUse`, `totalOwned`), `placements[]`, `activeBorrows[]` |

### Behaviour worth knowing

- **Errors** are JSON `{ "code": "...", "message": "..." }`. Seen: `UNAUTHENTICATED` (no header, HTTP 401) and `API_KEY_INVALID` (wrong key, HTTP 401).
- **Rate limit:** 120 requests per minute, enforced twice (per key and per key plus address). Responses carry `x-ratelimit-limit-apikey`, `x-ratelimit-remaining-apikey` and `x-ratelimit-reset-apikey` (plus the `-apikeyaddress` variants). Reset is 60 seconds.
- **Paging:** up to 100 records per page; use `page`.
- **Key expiry:** a key that can change data expires within 180 days; the form defaults to 90 days. A key with no expiry works until revoked.
- **Never put a key in a URL.** The API accepts `?api_key=` only for read-only keys, and the key then lands in server logs, browser history and the Referer header. A key that can change data is refused there. Always use the header. We deliberately did not test the URL form.
- **Writes are idempotent by header:** send a fresh UUID in `idempotency-key` for each stock operation; repeating the same value returns the first answer.
- Typical latency from the developer machine: about 80 to 320 ms.

### Steps to connect

1. In IMS, open "Issue key", name it for what uses it (for example "Kaizen data fetching"), tick only the scopes you need, and set an expiry.
2. Copy the key once and paste it into the repo-root `.env` under the matching variable name from the table above. Do not paste it anywhere else.
3. Run the probe to confirm what the key can do (this prints and stores no key):
   ```bash
   node api-data-fetching/tasks/2026-10-05-ims-api-key-scope-probe/probe.mjs --key inventory-read
   ```

Smallest manual check that the connection works (the key stays in the environment variable):
```bash
curl -H "Authorization: Bearer $IMS_KEY_INVENTORY_READ" "https://ims.siot.solutions/api/v1/products?limit=1"
```
