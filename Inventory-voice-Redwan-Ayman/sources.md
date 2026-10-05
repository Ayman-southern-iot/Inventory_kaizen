# Sources

Add new rows at the **bottom** of each table (this avoids merge conflicts when two people edit at once).

## Sources of truth
What results are checked against. State why each one is trusted and who owns it.

| Name | Link / location | What it is authoritative for | Owner | Last verified |
|---|---|---|---|---|
| IMS catalogue | `GET https://ims.siot.solutions/api/v1/catalogue` | Products, stock totals, readable shelf labels. The only read endpoint carrying location data | Ayman | 2026-10-05 |
| Speech service | `POST http://10.10.8.51:8099/v1/audio/transcriptions` | Speech-to-text. OpenAI-compatible layer | Ayman | 2026-10-05 |
| Board serial log | `idf.py monitor` | What the firmware actually did. Device-side claims are checked here, never against expectations | Redwan | 2026-10-05 |
| Captured audio, measured off-board | WAV mirrored off the device, analysed for band energy and zero-crossing rate | Whether the microphone really captured speech | Redwan | 2026-10-05 |

## Sources visited
Useful docs, references and pages found while working. The detailed per-task list lives in each task README.

| Date | Source | What it is good for | Task |
|---|---|---|---|
| 2026-10-05 | ESP-SR docs, `speech_synthesis` (ESP32-P4) | States "Currently Only supports Chinese language" — rules out on-device English TTS | firmware |
| 2026-10-05 | ESP-SR docs, `speech_command_recognition` | "command word cannot contain Arabic numerals and special characters" | firmware |
| 2026-10-05 | espressif/esp-sr 2.5.5 component source | ESP32-P4 libraries exist; `mn7_en` is the only English model on P4 | firmware |
| 2026-10-05 | `GET /api/v1/products?search=` (live) | Returns flat `totalQuantity`/`totalAvailable`, and **no location** | firmware |
| 2026-10-05 | Ayman's `INTEGRATION.md` | `/stt`, `/tts`, error codes, rate limits | firmware |

## Unreliable or outdated sources
Sources that turned out to be wrong, so nobody trusts them again.

| Source | Problem | Date found |
|---|---|---|
| `INTEGRATION.md` §3 (`/stt` + 64-hex token) | Returns `401 UNAUTHENTICATED`. The live path is `/v1/audio/transcriptions` with the `vs_` token | 2026-10-05 |
| `INTEGRATION.md` §4 (nested `stock{}` for `/products`) | `/products` actually returns flat `totalQuantity`/`totalAvailable`/`totalInUse`. Only `/catalogue` is nested | 2026-10-05 |
| Any faster-whisper benchmark quoting "35x real-time" | Assumes an AVX2 CPU. The VM has none; measured there: `tiny.en` 1.2 s, `base.en` 2.3 s, `small.en` 7.4 s | 2026-10-05 |
