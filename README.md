# Replacing HF-LPT120A in a millheat heater
Replacing HF_LPT120A in a millheat heater (gen2) using a Wemos D1 mini and ESPhome (external component)

Check this post for connecting the Wemos D1 mini https://community.home-assistant.io/t/guide-millheat-hardware-modification-making-it-open-source/129800/34?u=jdolven

The serial protocol between the ESP and the heater is documented in [PROTOCOL.md](PROTOCOL.md).

## Setup

Requires ESPHome 2025.11.0 or newer. Use [Varmeovn.yaml](Varmeovn.yaml) as a starting point:

- Put `wifi_ssid`, `wifi_password`, `fallback_wifi_pw`, `Secret_KEY` (API encryption key) and `HA_API` (OTA password) in your `secrets.yaml`.
- Set `heater_watts` to the rated power of your heater. The power and daily energy sensors are estimates based on when the heating relay is on.
- The heater is on the hardware UART (GPIO1/GPIO3), so keep `logger: baud_rate: 0`.

## Options

```yaml
climate:
  - platform: mill
    name: Heater
    id: heater
    uart_id: uart_bus
    wifi_icon: false    # show the Wi-Fi / Home Assistant connection on the heater's Wi-Fi icon
    sniffer: false      # log every UART frame in hex at INFO level, and which status bytes changed
    link_timeout: 3min  # warn when the heater has sent nothing for this long (0s turns it off)
```

`id(heater).link_ok()` is false while the link timeout has expired, so templates can stop reporting power.

## Help map the protocol

Set `sniffer: true`, do one thing at a time on the heater, and post the log in an issue together with what you did.
