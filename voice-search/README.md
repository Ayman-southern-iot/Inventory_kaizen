# voice-search

| | |
|---|---|
| **Owners** | Redwan, Mahmud |
| **Scope** | Voice-to-search over inventory data using the API. |
| **Created** | 2026-10-05 |

This domain explores searching inventory by voice: speech input, turning it into an API query, and checking the results against the source of truth. It depends on the connection details recorded in api-data-fetching.

## Start here

- [sources.md](sources.md): sources of truth and sources visited
- [connections.md](connections.md): how to connect (auth, endpoints, variable names)
- [troubleshooting/](troubleshooting/README.md): problems and fixes, one file per problem

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
