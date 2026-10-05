# ESP32-P4 voice inventory console (firmware)

| | |
|---|---|
| **Domain** | `Inventory-voice-Redwan-Ayman` |
| **Owner** | Redwan |
| **Date** | 2026-10-05 |
| **Status** | in-progress |

Deep technical detail, architecture rationale and the full gate table live in
[FIRMWARE.md](FIRMWARE.md). This file is the task record.

Imported from the ESP32-P4 project suite
(<https://github.com/RedwanRifat07/esp32-p4-docs>), where it is
`projects/23-voice-inventory-query` and reuses code from sibling projects 04 (audio),
08 (HTTP server), 09 (Wi-Fi) and 22 (speech).

## Goal

Can an ESP32-P4 answer a spoken question about the stores — "where is arduino?" — by
transcribing it on the VM, querying IMS itself, and showing the whole pipeline on a
dashboard it hosts, with an LED confirming success?

## Source of truth

- **Inventory data:** `GET https://ims.siot.solutions/api/v1/catalogue`. Chosen over
  `/products?search=` because the paged search response carries **no location data**,
  so it cannot answer "where is it". The catalogue returns nested
  `stock{total,available,inUse}` plus `locations[].label` in one request.
- **Speech:** `POST http://10.10.8.51:8099/v1/audio/transcriptions` (OpenAI-compatible
  layer on Ayman's VM).
- Device-side claims are checked against the board's own serial log and against audio
  captured off the board and measured independently, not against expectations.

## Sources visited

| Date | Source (exact link / name) | What it gave us | Useful? |
|---|---|---|---|
| 2026-10-05 | `GET /api/v1/catalogue` (live) | 65 products, 48,807 bytes, nested stock + readable shelf labels | Yes — the endpoint we use |
| 2026-10-05 | `GET /api/v1/products?search=arduino` (live) | 4 matches, but **flat** `totalQuantity`/`totalAvailable`, no location | Yes — informed the field parser |
| 2026-10-05 | `GET /api/v1/products/:id` (live) | `placements[]` with room/zone/compartment | Partly — superseded by `/catalogue` |
| 2026-10-05 | `voice-svc` INTEGRATION.md (Ayman) | `/stt`, `/tts`, error table, rate limits | Yes, with mismatches (see below) |
| 2026-10-05 | ESP-SR docs, speech_synthesis (ESP32-P4) | "Currently Only supports Chinese language" | Yes — ruled out on-device English TTS |
| 2026-10-05 | ESP-SR docs, speech_command_recognition | "command word cannot contain Arabic numerals and special characters" | Yes — ruled out on-device product-name recognition |
| 2026-10-05 | espressif/esp-sr 2.5.5 (component source) | P4 libs exist; `mn7_en` is the only English model on P4 | Yes |
| 2026-10-05 | Open ASR / faster-whisper benchmarks | model size vs WER vs CPU speed | Partly — the figures assume AVX2, which the VM lacks |

## How to connect

Two services, both bearer-token:

- **IMS** — `https://ims.siot.solutions/api/v1`, `Authorization: Bearer <read key>`,
  scope `inventory:read`. HTTPS, so the board attaches the ESP-IDF certificate bundle.
  Requires a correct clock (SNTP) or every certificate looks "not yet valid".
- **Speech** — `http://10.10.8.51:8099`, `Authorization: Bearer <voice token>`. The
  service allowlists a **source address**; see mismatches.

Secrets live in the board's **NVS**, never in source and never in `sdkconfig.defaults`
(which is committed). `tools/provision_nvs.py` reads a gitignored `.env`, builds an NVS
image in a temp dir **outside** the repo, flashes it, then deletes the temp file. All
logging is redacted to `<N chars, hidden>` — never a prefix.

Required environment variables:
- `WIFI_SSID`: 2.4 GHz SSID (the onboard C6 is 2.4 GHz only)
- `WIFI_PASSWORD`: its passphrase
- `INVENTORY_BASE_URL`: `https://ims.siot.solutions`
- `IMS_KEY_INVENTORY_READ`: read-only IMS key
- `VOICE_TOKEN_OPENAI`: token for `/v1/audio/transcriptions`
- `VOICE_TOKEN`: token for the native `/stt` + `/tts` layer, if still in use

## How to run

```bash
source ~/esp/esp-idf/export.sh
cp .env.example .env && $EDITOR .env

rm -f sdkconfig                    # else sdkconfig.defaults is silently ignored
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash
python tools/provision_nvs.py --port /dev/cu.usbmodem1101
idf.py -p /dev/cu.usbmodem1101 monitor

# offline development against an IMS-shaped mock:
python tools/mock_inventory.py --host 0.0.0.0 --port 8080 --api-key ims_mock
```

Dashboard: `http://<board-ip>/`. Both office SSIDs tested so far block
station-to-station traffic, so a SoftAP build (`CONFIG_INV_SOFTAP_ENABLE=y`) serves it
at `http://192.168.4.1/` instead.

## Research notes

Three measurements decided the architecture, each ruling an option out:

1. **No English TTS on ESP32.** Espressif's P4 doc says "Currently Only supports
   Chinese language", and the only library shipped is `libesp_tts_chinese.a` with
   `esp_tts_parser_chinese()`. The spoken answer must come from the VM.
2. **Whisper cannot run on the P4** — 768 KB SRAM at 400 MHz against hundreds of MB.
3. **On-device command recognition cannot handle product names.** MultiNet forbids
   numerals and special characters; checked against the live catalogue, **only 5 of 65**
   product names qualify (`'2.4 GHz 3 dBi RP-SMA Antenna'`, `'830-Point Solderless
   Breadboard'`, …).

So: wake word on the board (proven offline in project 22), transcription on the VM,
data and decisions on the board. The board keeps the IMS key and the catalogue cache,
which means the key never reaches the speech VM, and swapping speech models needs no
firmware change.

Model sizing was researched (`large-v3-turbo` at int8 is ~1.5 GB RAM / ~35x real-time)
but those figures assume an AVX2 CPU. Ayman measured the actual VM — no AVX2 — at
`small.en` 7.4 s, `base.en` 2.3 s, `tiny.en` 1.2 s. **Measurement beat the estimate;
`tiny.en` is correct there.**

## Obstacles and fixes

| # | Problem / exact error | Cause | Fix | Did not work |
|---|---|---|---|---|
| 1 | `Wi-Fi Scan Results (0 APs found)` for a month | `esp_wifi_scan_get_ap_records()` frees the AP list, so a later `get_ap_num()` returns 0 | Call `get_ap_num()` **first** | Suspecting the antenna/RF — the radio was always fine (16–20 APs) |
| 2 | `esp_wifi_set_config` timed out (5 s RPC) | Over ESP-Hosted it must precede `esp_wifi_start()`, unlike plain ESP-IDF | Reorder to init → set_mode → set_config → start | Retrying; the slave never answers |
| 3 | `esp_wifi_connect` then failed too | Six earlier unsupported scan-config RPCs timed out and poisoned the RPC channel | Never issue unsupported hosted calls | — |
| 4 | `esp_wifi_scan_start` → `ESP_FAIL` for any custom config | esp_hosted 1.4.7 rejects all non-NULL `wifi_scan_config_t` | Pass `NULL` | Long dwell, passive, per-channel — all six failed |
| 5 | Boot loop, `Guru Meditation: Load access fault, MTVAL 0x1` | SNTP `num_of_servers` is the **array length**, not a slot count; 2 declared with a 1-entry list read out of bounds | Match the count to the list | — |
| 6 | Three Kconfig options silently had no effect | An existing generated `sdkconfig` overrides `sdkconfig.defaults` | `rm -f sdkconfig`, then **grep the generated file** to confirm | Editing defaults alone |
| 7 | `403 Error 1010 browser_signature_banned` from IMS | Cloudflare blocks Python's default User-Agent | Browser-like UA for tooling. **The ESP32's own UA passes** (verified) | — |
| 8 | `/catalogue` is 48,807 bytes against a 48 KB buffer cap | 345 bytes of headroom; one more product would have truncated the JSON silently | Raised to 128 KB | — |
| 9 | Dashboard unreachable although the server worked | AP **client isolation**: broadcast arrives, unicast is dropped. Proven with a UDP probe (8/8 broadcasts received, unicast never left the sender) | SoftAP (`WIFI_MODE_APSTA`) | Moving SSIDs — both isolate |
| 10 | `403 FORBIDDEN_NETWORK` from the speech service | The allowlist held the board's LAN IP, but NAT presents the office public address | Ayman allowlisted the NAT address | Changing SSID; the board's own IP is never what the VM sees |
| 11 | Transcripts are fabrications ("Thank you very much" ×12) | Whisper hallucinates on low-SNR audio rather than returning empty | **Unresolved** — see Next steps | Gain 30/32/42 dB; mono vs stereo; level gating; a repetition filter |

The three reusable ones are written up in full:
- [`ap-client-isolation-blocks-device-server.md`](../../troubleshooting/ap-client-isolation-blocks-device-server.md) (#9)
- [`ip-allowlist-useless-behind-nat.md`](../../troubleshooting/ip-allowlist-useless-behind-nat.md) (#10)
- [`whisper-hallucinates-on-quiet-audio.md`](../../troubleshooting/whisper-hallucinates-on-quiet-audio.md) (#11)

## Result

Working and measured on real hardware (ESP32-P4 rev v3.1, ESP-IDF v5.5.4):

```
got IP 192.168.68.134  mask 255.255.255.0  gw 192.168.68.100
SNTP: 2026-10-05T07:15:23Z
HTTPS probe: ok (cert validated)
GET /api/v1/catalogue -> 200 (877 ms, 48807 bytes); cached 65 products
"arduino" matches 4:
  DEMO-MCU-006  Arduino Mega 2560 R3        avail 35/35 pcs @ Demo Store / Microcontrollers / S2
  DEMO-MCU-005  Arduino Nano (ATmega328P)   avail 40/40 pcs @ Demo Store / Microcontrollers / S1
  DEMO-MCU-004  Arduino Uno R3 (ATmega328P) avail 20/20 pcs @ Demo Store / Microcontrollers / S1
dashboard self-test GET / -> 200, 3759 bytes (matches embedded page)
```

Speech, from the board, with its own microphone:

```
POST /v1/audio/transcriptions  multipart, 73051 bytes, model=whisper-1
HTTP 200 in 1501 ms   (x-processing-time-ms: 1267)
{"text":"I-E-S-P. Where is Arduino?"}
```

**Transcription accuracy is the open problem.** Across ~45 recordings spanning three
gain settings and both channel configurations, **one** transcript was correct. The rest
were Whisper hallucinations. Measured cause: poor SNR — speech-band energy barely
exceeds high-frequency hash (ratio 1.17, where clean speech is 3–10).

What is *not* the cause, each tested and eliminated: WAV format (verified 1 ch /
16-bit / 16 kHz / exact duration), DC offset (0), clipping (0% at 30 dB), and channel
deinterleaving (mono and stereo both fail).

## Mismatches

Against `INTEGRATION.md`:

1. **The native `/stt` + 64-hex token path returns `401 UNAUTHENTICATED`.** The live
   path is `/v1/audio/transcriptions` with the `vs_` token. The guide's contract is
   stale, and the two API layers return different error envelopes
   (`{"error":{...,"type","code"}}` vs `{"code","message"}`).
2. **`/tts` is therefore unverified** — if it only accepts the dead token, there is no
   way to produce a spoken answer yet.
3. **The IP allowlist cannot identify the board.** Everything in the office NATs to one
   public address, so the filter admits any machine on that network. The **token** is
   the only real access control; the IP list is not a security boundary.
4. **`/products` returns flat `totalQuantity`/`totalAvailable`/`totalInUse`**, not the
   nested `stock{}` the guide describes. `/catalogue` *is* nested. The parser accepts
   both; a single-shape assumption would have silently produced `-1` stock.
5. The guide advises "search with ONE distinctive word" via `/products?search=`. Not
   used — that endpoint has no location data, so it cannot answer "where is it".

## Next steps

- [ ] Route the microphone through **ESP-SR's AFE** (noise suppression + AGC + VADNet)
      before uploading. This is the main untried fix and the path project 22 succeeded
      with; raw mic audio was used here, which plausibly explains #11.
- [ ] Ask Ayman to confirm whether `/stt` and `/tts` are retired, and for an
      OpenAI-compatible speech endpoint (`/v1/audio/speech`) if so. **TTS blocks the
      spoken answer entirely.**
- [ ] Server-side hallucination suppression: `no_speech_threshold`,
      `condition_on_previous_text=false`, `compression_ratio_threshold` (that last one
      detects exactly these repetitive loops).
- [ ] Re-evaluate `base.en` (+1.1 s) once SNR is fixed — not before, since the current
      failures are not model-limited.
- [ ] Rotate both speech tokens: they passed through chat transcripts.
- [ ] Writes (`/stock/receive`, `/stock/take`) are deliberately out of scope until
      reads are solid. The ledger is append-only and no key can undo its own write, so
      they need spoken read-back plus confirmation and per-request idempotency keys.
