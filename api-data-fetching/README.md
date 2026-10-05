# api-data-fetching

| | |
|---|---|
| **Owners** | Mahmud |
| **Scope** | Fetching inventory data through the API and verifying that the service account works. |
| **Created** | 2026-10-05 |

This domain checks that the inventory API can be reached with the service account, that the data it returns matches the source of truth, and records exactly how to connect so other domains (such as voice-search) can build on it.

## Start here

- [sources.md](sources.md): sources of truth and sources visited
- [connections.md](connections.md): how to connect (auth, endpoints, variable names)
- [troubleshooting/](troubleshooting/README.md): problems and fixes, one file per problem

## Tasks

<!-- BEGIN GENERATED: tasks -->
| Date | Task | Owner | Status | Goal |
|---|---|---|---|---|
| 2026-10-05 | [ims-api-key-scope-probe](tasks/2026-10-05-ims-api-key-scope-probe/README.md) | Mahmud | in-progress | For each of the five IMS API key types (Inventory, Catalogue, Storage locations, Receive stock, Take stock)... |
<!-- END GENERATED: tasks -->

## Known problems and fixes

<!-- BEGIN GENERATED: problems -->
| Date | Problem |
|---|---|
| 2026-10-05 | [Validator flags the local .env file as a stray root file](troubleshooting/validator-flags-root-env-file.md) |
<!-- END GENERATED: problems -->

<!--
The two blocks above are generated. Do not edit between the markers.
Run `node scripts/validate.mjs --write` to refresh them.
-->
