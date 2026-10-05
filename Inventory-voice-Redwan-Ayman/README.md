# Inventory-voice-Redwan-Ayman

| | |
|---|---|
| **Owners** | Redwan (firmware), Ayman (speech VM + IMS) |
| **Scope** | ESP32-P4 hardware that answers spoken inventory questions out loud |
| **Created** | 2026-10-05 |

A voice console for the stores. Someone with both hands full says *"Hi ESP … where is
arduino?"* and the board answers aloud, shows the whole pipeline on a dashboard it hosts
itself, and lights an LED when the lookup succeeded.

The firmware lives in [the task folder](tasks/2026-10-05-esp32-p4-voice-firmware/README.md). Work split across three pieces:

- **Board (ESP32-P4)** — wake word, microphone capture, the IMS query, the dashboard,
  the LED. Holds the IMS key in NVS, caches the catalogue, does the matching.
- **Speech VM (`10.10.8.51:8099`)** — Whisper speech-to-text and Piper text-to-speech.
  Deliberately a dumb appliance: audio in, text out. **The IMS key never reaches it.**
- **IMS (`ims.siot.solutions`)** — the inventory data, read-only.

Why that split: English text-to-speech does not exist on ESP32 (Espressif ships Chinese
only), Whisper cannot run in 768 KB of SRAM, and on-device command recognition forbids
numerals — only 5 of the 65 real product names would qualify. Each of those was measured,
not assumed. Full reasoning in [tasks/2026-10-05-esp32-p4-voice-firmware/FIRMWARE.md](tasks/2026-10-05-esp32-p4-voice-firmware/FIRMWARE.md).

**Status: in-progress.** Network, TLS, the IMS catalogue lookup, the dashboard and
speech-to-text all work on real hardware. Transcription *accuracy* is the open problem,
and text-to-speech is blocked. See [tasks/2026-10-05-esp32-p4-voice-firmware/README.md](tasks/2026-10-05-esp32-p4-voice-firmware/README.md).

## Start here

- [sources.md](sources.md): sources of truth and sources visited
- [connections.md](connections.md): how to connect (auth, endpoints, variable names)
- [troubleshooting/](troubleshooting/README.md): problems and fixes, one file per problem
- [tasks/2026-10-05-esp32-p4-voice-firmware/README.md](tasks/2026-10-05-esp32-p4-voice-firmware/README.md): the task record — what was done, what broke
- [tasks/2026-10-05-esp32-p4-voice-firmware/FIRMWARE.md](tasks/2026-10-05-esp32-p4-voice-firmware/FIRMWARE.md): architecture, gate table, build steps

## Tasks

<!-- BEGIN GENERATED: tasks -->
| Date | Task | Owner | Status | Goal |
|---|---|---|---|---|
| 2026-10-05 | [esp32-p4-voice-firmware](tasks/2026-10-05-esp32-p4-voice-firmware/README.md) | Redwan | in-progress | Can an ESP32-P4 answer a spoken question about the stores — "where is arduino?" — by |
<!-- END GENERATED: tasks -->

## Known problems and fixes

<!-- BEGIN GENERATED: problems -->
| Date | Problem |
|---|---|
| 2026-10-05 | [A server on the device is unreachable, though the device reaches everything else](troubleshooting/ap-client-isolation-blocks-device-server.md) |
| 2026-10-05 | [403 FORBIDDEN_NETWORK although the client's IP is allowlisted](troubleshooting/ip-allowlist-useless-behind-nat.md) |
| 2026-10-05 | [Whisper returns invented text instead of nothing](troubleshooting/whisper-hallucinates-on-quiet-audio.md) |
<!-- END GENERATED: problems -->

<!--
The two blocks above are generated. Do not edit between the markers.
Run `node scripts/validate.mjs --write` to refresh them.
-->
