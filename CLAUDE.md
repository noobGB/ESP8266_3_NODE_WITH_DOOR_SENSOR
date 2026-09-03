# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A single-sketch PlatformIO/Arduino project for ESP8266 (NodeMCU): a Blynk-connected 3-relay
switch node with a door sensor, WiFiManager config portal, and dual OTA update paths (HTTP + Arduino
OTA). See [README.md](README.md) for pin mapping and feature summary. All logic lives in
[src/main.cpp](src/main.cpp) — there is no other code to navigate.

## Commands

```
pio run                        # build
pio run -t upload              # flash to a connected NodeMCU
pio device monitor -b 115200   # view serial output
```

No test suite or linter is configured (`test/` is the empty PlatformIO scaffold directory).

## Notes for changes

- Physical switch state always takes priority over Blynk app state — this is intentional (the
  in-code changelog comments call this out explicitly: "Priority given to physical switches").
  Don't invert that priority without a clear reason; it's what keeps the lights controllable when
  WiFi/Blynk is unavailable.
- WiFi credentials and the Blynk token are never hardcoded — they default to the literal string
  `"default"` and are only ever set via the WiFiManager captive portal, then persisted to SPIFFS
  (`/config.json`) and mirrored to EEPROM as a SPIFFS-corruption fallback. Keep that pattern for
  any new credential/config value — don't hardcode real values into `main.cpp`.
- `ESP.wdtFeed()` calls inside long-running blocks (e.g. `BLYNK_CONNECTED()`) exist to prevent the
  hardware watchdog from resetting the board mid-sync — preserve them if restructuring that code.
- The in-file comment block at the top is a changelog (`GB <date> : <change>`) going back to 2019;
  continue that convention for non-trivial behavioral changes rather than relying solely on git
  history, since this firmware is often read/flashed without git context.
