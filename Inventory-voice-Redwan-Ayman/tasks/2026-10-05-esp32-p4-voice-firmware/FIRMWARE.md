# ESP32-P4 Voice Inventory Console (firmware)

> Imported from the ESP32-P4 project suite (https://github.com/RedwanRifat07/esp32-p4-docs), where it is `projects/23-voice-inventory-query`
> and sits alongside the board documentation and the other 22 projects it reuses code from.
> References below to "project 04/08/22" mean those sibling projects.

Ask the board a spoken question about the stores — *"Hi ESP … where is arduino?"* — and it answers
out loud, shows the whole pipeline on a web dashboard it hosts itself, and lights an LED when the
lookup succeeded.

**Status (2026-10-05):** 7 of 9 startup gates PASS on real hardware against the **live** IMS API.
Voice capture and playback are not built yet; the speech service link is blocked by a server-side
allowlist. See [Status and gates](#status-and-gates).

---

## 1. What we are building

A hands-free inventory lookup for the workshop. Someone with both hands full says:

> "Hi ESP."  *(board wakes)*
> "Where is arduino?"

and hears:

> "Arduino Mega 2560 R3. Thirty-five available. Demo Store, Microcontrollers, shelf S2."

Three outputs, every time:

| Output | Purpose |
|---|---|
| **Spoken answer** | the actual product of the feature — hands-free use |
| **Web dashboard** | left pane = live pipeline trace, right pane = the result. Hosted by the board |
| **LED (GPIO26)** | lights on a *successful fetch*, dark on failure. Glanceable truth |

The dashboard is not decoration. Every stage publishes to one internal event bus, and the dashboard
renders that bus — so what you see is what the firmware actually did, not a parallel story that can
drift from it.

---

## 2. Why the architecture is what it is

Each of these was **measured**, not assumed, and each one removed an option:

| Finding | Evidence | Consequence |
|---|---|---|
| **No English TTS exists on ESP32** | Espressif's P4 doc: *"Currently Only supports Chinese language."* On disk the sole offering is `esp-tts/esp_tts_chinese/` with `libesp_tts_chinese.a` and `esp_tts_parser_chinese()` | The spoken answer **must** come from a server |
| **Whisper cannot run on the P4** | 768 KB on-chip SRAM, 400 MHz dual RISC-V; smallest Whisper needs hundreds of MB | Transcription **must** be off-board |
| **MultiNet forbids digits and special characters** | Espressif docs, verified against the real catalogue: **only 5 of 65** product names are registrable as commands (`'2.4 GHz 3 dBi RP-SMA Antenna'`, `'830-Point Solderless Breadboard'`, …) | On-device closed-vocabulary recognition of product names is impossible |
| **Wake word works beautifully on-device** | Project 22: 4 detections, `wn9_hiesp` + `mn7_en`, fully offline | Wake word **stays** on the board |
| **Board→internet and board→VM both work** | HTTPS 200 with cert validation; `403` from the VM proves a full cross-subnet round trip | The split is viable |
| **Both office SSIDs isolate clients** | SAH and Auro: ARP resolves, unicast 100% lost, broadcast arrives | Dashboard must be served over the board's **own SoftAP** |
| **The VM has no AVX2** | Ayman measured on the real host: `small.en` 7.4 s, `base.en` 2.3 s, **`tiny.en` 1.2 s** | `tiny.en` is correct there; my `large-v3-turbo` estimate came from an AVX2 CPU and did not apply |

**Conclusion: wake word on the edge, speech on the VM, data and decisions on the board.**

---

## 3. System design

```
┌─ ESP32-P4 ────────────────────────────┐   ┌─ VM 10.10.8.51:8099 ─┐   ┌─ IMS ────────────┐
│                                       │   │                      │   │                  │
│  mic ─▶ AFE ─▶ WakeNet "Hi ESP"       │   │  POST /stt           │   │ GET /api/v1/     │
│                    │ wake             │   │    PCM -> text       │   │     catalogue    │
│         record ~4 s, stop on silence  │   │    (whisper tiny.en) │   │                  │
│                    │ ─────────────────┼──▶│                      │   │  65 products,    │
│         transcript ◀──────────────────┼───│                      │   │  stock + shelf   │
│                    │                  │   │  POST /tts           │   │  labels, 48 KB   │
│         extract term ("arduino")      │   │    text -> PCM       │   │                  │
│                    │                  │   │    (piper)           │   │                  │
│         match cached catalogue ───────┼───┼──────────────────────┼──▶│  HTTPS + Bearer  │
│                    │                  │   │                      │   │                  │
│         compose sentence              │   │                      │   └──────────────────┘
│                    │ ─────────────────┼──▶│                      │
│         PCM answer ◀──────────────────┼───│                      │
│                    │                  │   └──────────────────────┘
│         ES8311 ─▶ speaker             │
│                                       │
│  event bus ─┬─▶ WebSocket ─▶ dashboard (SoftAP, http://192.168.4.1/)
│             └─▶ LED GPIO26            │
└───────────────────────────────────────┘
```

### Division of labour, and why

**The board keeps the brain.** It holds the IMS key in NVS, caches the catalogue, does the matching,
composes the sentence, hosts the dashboard, and drives the LED. The VM is a dumb speech appliance —
audio in, text out; text in, audio out. Consequences that matter:

- The **IMS key never goes near the speech VM**.
- If the VM is down, the dashboard and polling keep working; only voice stops.
- Swapping the speech model is a server-side change with **nothing to alter in firmware**.

**Why `/api/v1/catalogue` and not `/products?search=`.** The paged search endpoint returns **no
location data at all**, so it cannot answer "where is it". The catalogue returns everything —
nested `stock{total,available,inUse}` plus `locations[].label` ("Demo Store / Microcontrollers / S2")
— in **one request**, which also means a second spoken question costs no extra API call. IMS's own
docs agree: *"Best single call… fetch and cache, don't call the API once per user action."*

**Why the SoftAP.** Both office SSIDs drop unicast station-to-station traffic, so nothing on the LAN
can reach a server on the board. Running the C6 in `WIFI_MODE_APSTA` keeps the station link for
outbound API calls while broadcasting its own network for the dashboard. No router change needed,
and it works on any network the board is moved to.

---

## 4. Status and gates

Measured on real hardware, ESP-IDF v5.5.4, ESP32-P4 rev v3.1, against the **live** IMS API.

| # | Gate | Status | Evidence |
|---|---|---|---|
| 1 | Radio / scan | ✅ PASS | 15–20 APs; strongest −47 dBm |
| 2 | Associate + DHCP | ✅ PASS | `got IP 192.168.68.134 gw 192.168.68.100` |
| 3 | DNS + HTTP | ✅ PASS | `example.com -> 104.20.23.154`, 200 |
| 4 | SNTP clock | ✅ PASS | `2026-10-05T07:15:23Z` — needed before any TLS |
| 5 | HTTPS + cert | ✅ PASS | real cert validation via `esp_crt_bundle` |
| 6 | IMS catalogue | ✅ PASS | `GET /api/v1/catalogue -> 200`, 65 products, 48,807 B, **368–663 ms** |
| 7 | Dashboard serves | ✅ PASS | loopback `GET /` → 200, body length matches embedded page exactly |
| 8 | Voice `/health` | ❌ **BLOCKED** | `403 FORBIDDEN_NETWORK` — server-side allowlist, not our code |
| 9 | Voice `/tts` round trip | ⏸ gated on 8 | |

Gate 7 is deliberately a **loopback fetch**, not "the server started". Project 08 was marked done on
that weaker basis and had never served a byte.

### Blocked on others

| Item | Owner | Detail |
|---|---|---|
| Voice service allowlist | **Ayman** | Board and laptop both get `403` with a valid token. Marked probes left in his log (`redwan-155320`). Recommendation: drop the IP filter — everything NATs to one address so it cannot identify the board, and the token is the real control |
| Rotate `API_TOKEN` | **Ayman** | It travelled through a channel that logs |
| Rotate GitHub PAT + SIOT gateway token | **Redwan** | I exposed them earlier via a shell idiom that failed open |

---

## 5. Work remaining

Each phase has a gate that is a line in a real log or an observed behaviour. No phase advances
without it. Real `idf.py` only — the `esp-idf-eim` MCP tools fake success.

| # | Work | PASS gate |
|---|---|---|
| **A** | **Mic capture.** Port `board_audio.c` from project 22 (proven: BSP mic, 16 kHz/16-bit, channel 0, AFE format `"MN"`). Record on demand with silence-stop and a hard 15 s cap | Per-channel RMS responds to speech; a 3 s capture yields 96,000 bytes of plausible PCM |
| **B** | **Speaker playback.** BSP speaker path from project 22 + `esp_codec_dev_write` | A synthetic tone, then a `/tts` clip, is **audible** — user-confirmed |
| **C** | **Wake word.** WakeNet `wn9_hiesp` + AFE feed/detect, as in project 22 | "Hi ESP" logs `WAKE WORD DETECTED` repeatedly; 60 s of silence logs nothing |
| **D** | **STT round trip.** Wake → record → `POST /stt` → transcript | A spoken question returns matching text; silence returns empty `text` and is handled as "didn't catch that", not an error |
| **E** | **Term extraction + match.** Strip question words (`where`, `is`, `how`, `many`, `find`), test remaining content words against the cached catalogue | *"where is arduino"* → `arduino` → the 4 real matches |
| **F** | **Spoken answer.** Compose ≤500 chars, `POST /tts`, play | The sentence is audible and correct; LED lights on success and **stays dark** on a forced failure |
| **G** | **Dashboard + docs.** Voice stages on the event bus; README transcript; index row; commit | Every stage appears live in the process pane |

**Order matters:** A and B are independent of Ayman and testable standalone, so they come first. C is
pure reuse of proven code. Only D needs the allowlist fixed.

### Deliberately out of scope for now

- **Writes to IMS** (`/stock/receive`, `/stock/take`). The ledger is append-only and **no key can
  undo its own write** — only a human Inventory Manager can correct it. That demands a read-back and
  an explicit spoken "yes" before any write, plus `Idempotency-Key` per request. Worth doing
  properly, separately, once reads are solid.
- Multi-turn disambiguation ("I found 4 — which one?").
- Per-client tokens on the speech service (Ayman's side).

---

## 6. Hardware

- **Onboard:** SMD microphone + ES8311 codec, speaker header, ESP32-C6 (Wi-Fi over SDIO).
- **External:** 1× LED + 220–470 Ω resistor; a speaker on the MX1.25 header for the spoken answer.

| LED leg | To | Header pin |
|---|---|---|
| Anode (+) | **GPIO26** via the resistor | **31** |
| Cathode (−) | GND | **30** or **34** |

> ⚠️ This board has **no GPIO-controllable onboard LED** — its only discrete LED is the hardwired PWR
> indicator; every other LED-named net belongs to the IP101 Ethernet PHY or the CH334F USB hub.
> Do not use pin 35 (GPIO53 = audio power-amp enable).

> ⚠️ The speaker header and the 3.5 mm jack are **mutually exclusive by hardware design** —
> `EARPHONE_DETECT` mutes the speaker PA whenever a plug is in the jack. Unplug headphones to hear
> the answer.

---

## 7. Build, provision, run

```sh
source ~/esp/esp-idf/export.sh
cd projects/23-voice-inventory-query

cp .env.example .env && $EDITOR .env      # WIFI_*, IMS_KEY_INVENTORY_READ, VOICE_TOKEN
rm -f sdkconfig                           # else sdkconfig.defaults is ignored
idf.py set-target esp32p4 && idf.py build
idf.py -p /dev/cu.usbmodem1101 flash
python tools/provision_nvs.py --port /dev/cu.usbmodem1101
```

Then join Wi-Fi **`ESP32-P4-Inventory`** and open **http://192.168.4.1/**.

### Secrets

Secrets live in **NVS only** — never in source, and never in `sdkconfig.defaults`, which *is*
committed. `tools/provision_nvs.py` reads the gitignored `.env`, builds an NVS image in a temp dir
**outside** the repo, flashes it, and deletes the temp file. All logging is redacted to
`<N chars, hidden>` — never a prefix, because four characters of a password is four an attacker
does not have to guess.

**A key on the board is extractable** unless flash encryption is on. Treat the IMS key as exposed if
a board goes missing, and have it revoked.

---

## 8. Files

| File | Role |
|---|---|
| `main/voice_inventory_main.c` | Startup gate sequence, query orchestration |
| `main/net_wifi.c` | STA + SoftAP (APSTA), DHCP, DNS/HTTP reachability, UDP inbound probe |
| `main/secrets.c` | NVS-backed credentials, redacted logging |
| `main/app_time.c` | SNTP — mandatory before TLS |
| `main/inventory_api.c` | **The only file that knows IMS's shape.** Catalogue fetch, tolerant parsing, 429 retry |
| `main/voice_svc.c` | STT/TTS client: bearer auth, one-call-at-a-time mutex, bounded retries |
| `main/event_bus.c` | Structured pipeline events + retained ring buffer |
| `main/web_server.c` | Dashboard + WebSocket + loopback self-test |
| `main/led_action.c` | Success/failure LED (copied from project 22) |
| `web/index.html` | Self-contained dashboard, gzipped to 3,759 B and embedded |
| `tools/provision_nvs.py` | `.env` → NVS, secrets never in the repo |
| `tools/mock_inventory.py` | IMS-shaped mock for offline development |

---

## 9. Gotchas that shaped this build

Recorded in the firmware suite repo (https://github.com/RedwanRifat07/esp32-p4-docs) under `.planning/knowledge/gotchas/`:

- **`esp_wifi_set_config()` must precede `esp_wifi_start()`** over ESP-Hosted, or it times out.
- **A failed hosted RPC poisons the channel** — six timed-out scan calls broke the *next*
  `esp_wifi_connect()`. Never issue an unsupported hosted call "just to see".
- **esp_hosted rejects every non-NULL scan config.** Pass `NULL`.
- **`esp_wifi_scan_get_ap_num()` before `get_ap_records()`** — the latter frees the list. This bug
  faked a "dead radio" for a month.
- **SNTP `num_of_servers` is the array length**, not a slot count — mismatching it panics with a
  Load access fault.
- **`rm -f sdkconfig`** after editing `sdkconfig.defaults`, then `grep` the generated file. Three
  separate options silently failed to apply before this became a habit.
- **Cloudflare fronts `ims.siot.solutions`** and blocks Python's default User-Agent with
  `403 Error 1010`. The ESP32's own UA passes — verified explicitly.
- **IMS `/catalogue` is 48,807 bytes** against what was a 48 KB buffer cap. One more product would
  have silently truncated the JSON. Now 128 KB; it is unpaginated and refuses above 5000 products.
- **Speech must be loud and close** — detection needed **−27 dBFS**; −41 dBFS never matched.
