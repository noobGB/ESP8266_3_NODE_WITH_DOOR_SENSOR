# ESP8266_3_NODE_WITH_DOOR_SENSOR

A NodeMCU (ESP8266) home-automation node built on [Blynk](https://blynk.io/) that controls three
230V AC relay loads and reports a magnetic door sensor's state to the Blynk app.

## Features

- **3 relay channels**, each drivable from a physical latching switch or from the Blynk app
  (virtual pins V12/V13/V14), with local physical-switch state always taking priority over the
  app so the lights work even if WiFi/Blynk is down.
- **Door sensor** (reed/magnetic switch) reporting open/closed state and push notifications on
  change via Blynk.
- **WiFiManager captive portal** for first-time/on-demand WiFi + Blynk token setup, triggered by a
  dedicated reset button — no credentials are hardcoded in source.
- **HTTP OTA firmware updates**, checked against a version file and pulled from a configured
  update server, triggerable from the Blynk app.
- **Arduino OTA** (password-protected) as a secondary update path for local-network flashing.
- Config (WiFi credentials, Blynk token, LED states, hostname) persisted to SPIFFS, with the
  Blynk token additionally backed up to EEPROM in case SPIFFS mounting fails and auto-formats.

## Hardware

| Signal | NodeMCU pin |
|---|---|
| Relay/LED 1 | GPIO0 (D3) |
| Relay/LED 2 | GPIO14 (D5) |
| Relay/LED 3 | GPIO13 (D7) |
| Switch 1 | GPIO4 (D2) |
| Switch 2 | GPIO12 (D6) |
| Switch 3 | GPIO3 (Rx) |
| Door sensor (magnetic/reed switch) | GPIO5 (D1) |
| WiFi/config reset button | GPIO2 (D4) |
| Built-in status LED | GPIO16 (D0) |

**This drives 230V AC relay loads** — treat wiring, relay module isolation, and enclosure the same
as any mains-voltage project.

## Build and flash

This is a [PlatformIO](https://platformio.org/) project targeting a NodeMCU v2 (ESP8266) board.

```
pio run                        # build
pio run -t upload              # flash
pio device monitor -b 115200   # view serial output
```

## Configuration

On first boot (or by holding the reset button), the device opens a WiFiManager captive portal
where you set WiFi credentials, the Blynk auth token, and a server ID (Blynk cloud vs. local
server). None of these are hardcoded — the sketch falls back to `"default"` placeholder values
until configured.
