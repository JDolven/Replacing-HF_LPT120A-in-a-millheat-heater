# Mill Gen2 UART protocol

What is known about the serial protocol between the heater's own microcontroller (MCU) and the
HF-LPT120A Wi-Fi module that this project replaces. Nothing here comes from Mill: it is pieced
together from captures, from other projects and from the original module's firmware.

The heater MCU keeps the thermostat, the display, the buttons and all safety functions. The Wi-Fi
module only relays messages between the MCU and the cloud, and drives the Wi-Fi icon.

Confidence is marked as **confirmed** (seen in two or more independent sources, or in the module
firmware and a capture), **firmware** (read from the module firmware only) or **guess**.

## Physical layer

- UART, 9600 baud, 8N1, 3.3 V logic. **confirmed**
- Header on the heater board: `GND`, `+3.3V`, `WRST`, `WRX`, `WTX`. `WRST` is not needed.

## Frame format (confirmed)

```
5A | LEN_HI LEN_LO | MSGID | FLAG | CODE | data ... | SUM | 5B
```

| Byte | Meaning |
|---|---|
| `5A` | Start of frame. |
| `LEN_HI LEN_LO` | Length of the **whole frame**, big-endian, including `5A` and `5B`. A command frame is `00 10` = 16 bytes. |
| `MSGID` | Message ID. The original module copied it from the cloud message, so it changes from frame to frame. The MCU does not seem to care about its value. |
| `FLAG` | `00` in every frame seen so far. The module firmware inserts 4 extra bytes before the data when it is `01`. **firmware** |
| `CODE` | Message type, see below. |
| data | 8 bytes for commands (probably also status), 2 bytes for the Wi-Fi icon, 124/125 bytes for registration. |
| `SUM` | 8-bit sum of every byte from `LEN_HI` up to the last data byte. |
| `5B` | End of frame. |

Receivers must use the length field to find the end of a frame. Data bytes can be `0x0A`
(10 °C) or `0x5B`, so scanning for an end byte or a newline cuts frames short. The original module
reads frames this way, rejects lengths above 600, and drops frames with a bad checksum.

Example, set 14 °C (captured from the original module):

```
5A 00 10 1B 00 46 01 00 0E 00 00 00 00 00 80 5B
SUM = 00+10+1B+00+46+01+00+0E = 0x80
```

## Cloud → MCU (sent by the Wi-Fi module, so by the ESP here)

The module passes these through from the cloud without looking at the data, so their meaning
is defined by the MCU and the Mill app, not by the module firmware.

| Code | Data (8 bytes) | Meaning | Confidence |
|---|---|---|---|
| `46` | `01 00 TT 00 00 00 00 00` | Set target temperature `TT` in whole °C. | confirmed |
| `47` | `PP 00 00 00 00 00 00 00` | Power: `PP` = `00` off, `01` on (panel heaters). | confirmed |
| `44` | `PP 00 00 00 00 00 00 00` | Power on convection heaters. | capture only |
| `48` | `FF 00 00 00 00 00 00 00` | Fan off/on (`00`/`01`) on convection heaters. | capture only |
| `50` | `00 00 00 00 00 00 00 00` | Sent by the app right after every `46`. Meaning unknown (save?). | capture only |

## MCU → cloud

| Code | Meaning | Confidence |
|---|---|---|
| `C9` | Status, see below. | confirmed |

### Status frame `C9`

Data byte offsets (byte 0 is the first byte after `CODE`):

| Data byte | Meaning | Confidence |
|---|---|---|
| 0 | unknown | |
| 1 | Target temperature, whole °C. `0` = no reading. | confirmed |
| 2 | Room temperature, whole °C. `0` = no reading. | confirmed |
| 3 | unknown | |
| 4 | Mode: `00` off, `01` heat. | confirmed |
| 5 | unknown | |
| 6 | Heating element: `00` idle, `01` heating. | confirmed |
| 7 and up | unknown | |

The total length of a status frame is still to be confirmed (probably 16 bytes); it is in the
frame's own length field, so the sniffer log shows it.

## Wi-Fi icon (module → MCU)

The module sends 10-byte frames with `MSGID` `01` to drive the Wi-Fi icon on the display. The
codes and data bytes are in the module firmware, and the icon colours were captured
independently in issue #10. **confirmed**

| Frame | Icon | Original meaning |
|---|---|---|
| `5A 00 0A 01 00 02 F0 0F 0C 5B` | green, blinking | Wi-Fi connected |
| `5A 00 0A 01 00 03 F1 1F 1E 5B` | off | Wi-Fi disconnected |
| `5A 00 0A 01 00 04 F2 2F 30 5B` | green | cloud connected |
| `5A 00 0A 01 00 05 F3 3F 42 5B` | red | cloud disconnected |

With `wifi_icon: true` the component sends: off without Wi-Fi, blinking with Wi-Fi only, green
while Home Assistant is connected, red after Home Assistant has disconnected.

Issue #10 reports that the buttons behave differently once the icon is driven (as if a
"perceived temperature" calibration starts). This is not understood yet, which is why the
option is off by default.

## MCU → module

The original module handles these codes itself instead of forwarding them to the cloud. **firmware**

| Code | Data | Notes |
|---|---|---|
| `00` | | Purpose unknown. |
| `07` | 124 or 125 bytes (frame length `0x84`/`0x85`) | Probably registration: device ID, type and key for the AbleCloud service. The 133-byte `settInit` frame in issue #10 is this. |
| `08` | `D8 8D` | Purpose unknown. The module replies with code `08` (to the MCU or the cloud, not yet traced). |
| `09` | `E9 9E` | Purpose unknown. The module replies with code `09` (to the MCU or the cloud, not yet traced). |
| `0A` | `FA AF` | Purpose unknown. The module replies with code `0A` (to the MCU or the cloud, not yet traced). |
| `0B` | `B7 7B` | Purpose unknown. The module replies with code `0B` (to the MCU or the cloud, not yet traced). |
| `0C` | text | Module name or ID; the handler works with the string `Mill Device G2`. |
| `0D` | | Purpose unknown. |
| `0E` | | Purpose unknown. |
| `24` | `A6 6A` | Purpose unknown. The module replies with code `24` (to the MCU or the cloud, not yet traced). |

The fixed byte pairs look like checks against line noise. These are probably sent by button
combinations on the heater (Wi-Fi reset, pairing mode, factory reset). The component logs them
at INFO level and does not answer.

All other codes from the MCU are forwarded to the cloud.

## Open questions

- Meaning of status data bytes 0, 3, 5 and 7 and up, and the full status length.
- What `50` does, and whether the panel heaters have more commands (child lock, display, open
  window, eco or night mode, timers).
- Which button combinations send `08`, `09`, `0A`, `0B`, `24`, and what the MCU expects back.
- Whether the MCU sends `07` (registration) at every boot, and whether it waits for an answer.

The data meaning for the cloud codes is most likely in the old AbleCloud-based Mill Android app.

## How to help

1. Set `sniffer: true` on the `mill` climate and watch the logs (`esphome logs`).
2. Every frame is logged in hex, and for status frames a line like
   `Status changed: b7 15->16 b12 00->01` shows which bytes changed (byte 0 is `5A`).
3. Do one thing at a time on the heater (change the setpoint, turn it off, press each button or
   button combination), note the time and what you did, and post the log in an issue.

## Sources

- The HF-LPT120A firmware dump attached to issue #12 (frame parser, frame builder, Wi-Fi icon,
  MCU → module codes).
- Captures in issue #10 (Wi-Fi icon colours, registration frame).
- Captures of the original module in
  [metrafonic/MillHeat-ESPHome](https://github.com/metrafonic/MillHeat-ESPHome) (convection heater).
- [JanOveSaltvedt/mill-mod-matter](https://github.com/JanOveSaltvedt/mill-mod-matter) `src/mill.rs`
  (status layout, checksum).
- The [Home Assistant community thread](https://community.home-assistant.io/t/guide-millheat-hardware-modification-making-it-open-source/129800).
