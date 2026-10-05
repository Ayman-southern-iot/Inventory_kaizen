# How to connect

Two services, both bearer-token. **Variable names only below — never paste values.**

## IMS (inventory data)

| | |
|---|---|
| Base URL | `https://ims.siot.solutions/api/v1` |
| Auth | `Authorization: Bearer <IMS_KEY_INVENTORY_READ>` (starts `ims_`) |
| Scope needed | `inventory:read` |

- **Use `GET /catalogue`**, not `/products?search=`. One request returns all products with
  nested `stock{total,available,inUse}` **and** `locations[].label`
  ("Demo Store / Microcontrollers / S2"). The paged search endpoint has no location data
  at all, so it cannot answer "where is it".
- It is **not paginated** — the server refuses above 5000 products rather than truncating.
  Measured at 65 products: **48,807 bytes**. Size the receive buffer with real headroom.
- Cache it. 120 requests/minute per key; the board refreshes every 120 s and matches
  locally, so a spoken question costs no API call.
- HTTPS, so attach the ESP-IDF certificate bundle — and **sync SNTP first**, or every
  certificate looks "not yet valid" because the board boots at the epoch.
- Cloudflare fronts this host and blocks Python's default User-Agent with
  `403 Error 1010`. The ESP32's own User-Agent passes (verified).

## Speech service

| | |
|---|---|
| Base URL | `http://10.10.8.51:8099` |
| Auth | `Authorization: Bearer <VOICE_TOKEN_OPENAI>` (starts `vs_`) |
| Live endpoint | `POST /v1/audio/transcriptions` — multipart WAV + `model=whisper-1` → `{"text": "..."}` |
| Server timing | the `x-processing-time-ms` **response header**, not a JSON field |

- Audio is raw PCM **16-bit signed little-endian, 16 kHz, mono**; the multipart form wants
  it wrapped in a 44-byte WAV header.
- **One request at a time** — the service transcribes serially because it shares a host.
- Retry only on network errors and `5xx`, at most twice. A `4xx` is deterministic.
- An empty `text` means no speech was heard. That is normal, not an error.
- The native `/stt` and `/tts` endpoints return `401` with the older token. **TTS is
  currently unavailable**, which blocks the spoken answer.
- The IP allowlist **cannot identify the board** — everything in the office NATs to one
  public address. The token is the only real access control.

## Required environment variables

Kept in a gitignored `.env`, written into the board's NVS by
`tasks/2026-10-05-esp32-p4-voice-firmware/tools/provision_nvs.py`. Never compiled into the firmware, never in
`sdkconfig.defaults` (which *is* committed), never logged in full.

- `WIFI_SSID` — 2.4 GHz SSID (the onboard ESP32-C6 is 2.4 GHz only)
- `WIFI_PASSWORD` — its passphrase
- `INVENTORY_BASE_URL` — `https://ims.siot.solutions`
- `IMS_KEY_INVENTORY_READ` — read-only IMS key
- `VOICE_TOKEN_OPENAI` — token for `/v1/audio/transcriptions`
- `VOICE_TOKEN` — token for the native `/stt` + `/tts` layer, if revived

> **A key on the board is extractable** unless ESP-IDF flash encryption is enabled.
> Treat these as exposed if a board goes missing, and have them revoked.
