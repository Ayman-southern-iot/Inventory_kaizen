<!-- GENERATED FILE. Do not edit by hand. Run: node scripts/validate.mjs --write -->
# Index

Live list of every domain, task and known problem in this knowledge base. Generated from the repo contents.

## Domains

| Domain | Scope | Owners | Tasks | Problems |
|---|---|---|---|---|
| [Inventory-voice-Redwan-Ayman](Inventory-voice-Redwan-Ayman/) | ESP32-P4 hardware that answers spoken inventory questions out loud | Redwan (firmware), Ayman (speech VM + IMS) | 1 | 3 |
| [api-data-fetching](api-data-fetching/) | Fetching inventory data through the API and verifying that the service account works. | Mahmud | 2 | 2 |
| [shared](shared/) | Code and notes used by more than one domain: API client helpers, anonymised sample data, glossary | Mahmud, Redwan | 0 | 0 |
| [voice-search](voice-search/) | Voice-to-search over inventory data using the API. | Redwan, Mahmud | 0 | 0 |

## Tasks

| Date | Domain | Task | Owner | Status | Goal |
|---|---|---|---|---|---|
| 2026-10-05 | api-data-fetching | [ims-api-key-scope-probe](api-data-fetching/tasks/2026-10-05-ims-api-key-scope-probe/README.md) | Mahmud | in-progress | For each of the five IMS API key types (Inventory, Catalogue, Storage locations, Receive stock, Take stock)... |
| 2026-10-05 | api-data-fetching | [live-product-cards](api-data-fetching/tasks/2026-10-05-live-product-cards/README.md) | Mahmud | working | Show every IMS product as a card in a simple web page using the Inventory (read) key, and have the cards up... |
| 2026-10-05 | Inventory-voice-Redwan-Ayman | [esp32-p4-voice-firmware](Inventory-voice-Redwan-Ayman/tasks/2026-10-05-esp32-p4-voice-firmware/README.md) | Redwan | in-progress | Can an ESP32-P4 answer a spoken question about the stores — "where is arduino?" — by |

## Known problems and fixes

| Date | Domain | Problem |
|---|---|---|
| 2026-10-05 | api-data-fetching | [Browser says "localhost refused to connect" on the live cards page](api-data-fetching/troubleshooting/localhost-refused-to-connect.md) |
| 2026-10-05 | api-data-fetching | [Validator flags the local .env file as a stray root file](api-data-fetching/troubleshooting/validator-flags-root-env-file.md) |
| 2026-10-05 | Inventory-voice-Redwan-Ayman | [A server on the device is unreachable, though the device reaches everything else](Inventory-voice-Redwan-Ayman/troubleshooting/ap-client-isolation-blocks-device-server.md) |
| 2026-10-05 | Inventory-voice-Redwan-Ayman | [403 FORBIDDEN_NETWORK although the client's IP is allowlisted](Inventory-voice-Redwan-Ayman/troubleshooting/ip-allowlist-useless-behind-nat.md) |
| 2026-10-05 | Inventory-voice-Redwan-Ayman | [Whisper returns invented text instead of nothing](Inventory-voice-Redwan-Ayman/troubleshooting/whisper-hallucinates-on-quiet-audio.md) |
