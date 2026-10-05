# Connections

How to connect to each system used in this domain. Environment variable **names** only, never values.
Add each new system as a new section at the **bottom**.

## IMS API (Inventory Management System)

Voice search uses the same IMS API as the `api-data-fetching` domain. Connection details are kept in one place so they do not drift: see [api-data-fetching/connections.md](../api-data-fetching/connections.md#ims-api-inventory-management-system).

| | |
|---|---|
| **Key type needed** | Inventory (read), scope `inventory:read`. A search feature should never need a key that changes data |
| **Env var** | `IMS_KEY_INVENTORY_READ` in the repo-root `.env` (gitignored) |
| **Verified working** | 2026-10-05, [ims-api-key-scope-probe](../api-data-fetching/tasks/2026-10-05-ims-api-key-scope-probe/README.md) |

Useful for voice search (from the findings so far):
- `GET /api/v1/catalogue` returns every product with name, code, description, unit, category path, stock (total, available, in use) and shelf labels in one response. 65 products at the time of testing, so the whole list is small enough to match spoken names against locally.
- `GET /api/v1/products?search=<text>` is documented as a server-side search. **Not tested yet** in this domain.
- Keep the key on a server. Do not put it in a browser page or a mobile app. The pattern is shown in [live-product-cards](../api-data-fetching/tasks/2026-10-05-live-product-cards/README.md): a small local server holds the key and the page talks only to that server.
- Rate limit: 120 requests per minute per key. Voice queries should be matched against a cached copy of the catalogue, refreshed every few seconds, instead of calling the API per spoken word.
- Product names in the demo data are ordinary text such as "16x2 Character LCD with I2C Backpack" and codes such as `DEMO-COM-007`, which matters for speech-to-text accuracy (model numbers, units, symbols).

Smallest command that proves the connection works (the key stays in the environment variable):
```bash
curl -H "Authorization: Bearer $IMS_KEY_INVENTORY_READ" "https://ims.siot.solutions/api/v1/products?limit=1"
```
