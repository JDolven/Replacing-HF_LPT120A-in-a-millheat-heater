# Replacing HF-LPT120A in a millheat heater
Replacing HF_LPT120A in a millheat heater (gen2) using a Wemos D1 mini (or an ESP32) and ESPHome (external component)

Check this post for connecting the Wemos D1 mini https://community.home-assistant.io/t/guide-millheat-hardware-modification-making-it-open-source/129800/34?u=jdolven

The heater keeps its own thermostat, display and safety functions. The ESP only replaces the Wi-Fi module and talks to the heater over UART (9600 baud). See [PROTOCOL.md](PROTOCOL.md) for what is known about the protocol.

## Setup

Requires ESPHome 2025.11.0 or newer. Use [Varmeovn.yaml](Varmeovn.yaml) as a starting point:

- Put `wifi_ssid`, `wifi_password`, `fallback_wifi_pw`, `Secret_KEY` (API encryption key) and `HA_API` (OTA password) in your `secrets.yaml`.
- Set `heater_watts` to the rated power of your heater. The power and daily energy sensors are estimates based on when the element is on.
- On a D1 mini the heater uses the hardware UART (GPIO1/GPIO3), so keep `logger: baud_rate: 0`.

## Options

```yaml
climate:
  - platform: mill
    name: Heater
    uart_id: uart_bus
    wifi_icon: false     # drive the heater's Wi-Fi icon from the Wi-Fi / Home Assistant connection
    sniffer: false       # log every UART frame in hex and which status bytes changed
    link_timeout: 5min   # optional: warn when the heater has sent no status for this long
```

`id(heater).link_ok()` is false while the link timeout has expired, so templates can stop reporting power.

## Help map the protocol

Set `sniffer: true`, do one thing at a time on the heater, and post the log in an issue. See [PROTOCOL.md](PROTOCOL.md#how-to-help).
