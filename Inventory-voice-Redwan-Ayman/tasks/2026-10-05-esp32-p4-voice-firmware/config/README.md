# Chip-revision config

Vendored from the ESP32-P4 project suite, originally from
[waveshareteam/ESP32-P4-Platform](https://github.com/waveshareteam/ESP32-P4-Platform) (Apache-2.0).

- **`esp32p4_rev3_x.defaults`** — this board. ESP32-P4 silicon rev v3.0+.
- **`esp32p4_rev1_3.defaults`** — legacy, pre-v3 silicon.
- **`esp32p4_local_defaults.cmake`** — included from the top-level `CMakeLists.txt`;
  selects `rev3_x` unless the caller already set `SDKCONFIG_DEFAULTS`.

**This is not optional boilerplate.** `esp-sr` picks its prebuilt ESP32-P4 library
directory from `CONFIG_ESP32P4_SELECTS_REV_LESS_V3`. With it `n` (correct for rev v3.1)
it *additionally* requires ESP-IDF >= 5.5.3, or it silently links `esp32p4_less_v3`
binaries built for older silicon — nothing errors. Always confirm the build prints:

    -- TARGET_LIB_PATH is set to: esp32p4          <- correct
    -- TARGET_LIB_PATH is set to: esp32p4_less_v3  <- WRONG for this board
