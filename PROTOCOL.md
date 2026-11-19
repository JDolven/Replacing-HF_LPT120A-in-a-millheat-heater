# Mill gen2 serial protocol

Reverse-engineered from the UART between the Wi-Fi module socket (HF-LPT120A, replaced by a Wemos D1 mini)
and the heater's control board. Captured on a Mill panel heater gen2 in October 2026 using ESPHome's
`uart: debug:` and by sending frames from the ESP.

Anything marked *unknown* has been seen but not understood yet.

## Physical layer

- 9600 baud, 8N1, 3.3 V
- ESP TX = GPIO1, RX = GPIO3 (the hardware UART), so the ESPHome logger must use `baud_rate: 0`

## Frame format

```
5A | LEN_HI | LEN | SEQ | 00 | CMD | data ... | SUM | 5B
```

| Field | Description |
|---|---|
| `5A` | Start marker |
| `LEN_HI` | High byte of the length. Always 0 in practice; the original module reads `LEN_HI LEN` as one 16-bit length and rejects anything above 600. |
| `LEN` | Total frame length in bytes, from `5A` to `5B` inclusive |
| `SEQ` | Ignored by the heater as far as tested (`00`, `06`, `16` and `22` all work). The heater always sends `00`, except in the Wi-Fi button frame (`01`). |
| `00` | Always 0 |
| `CMD` | Command / message type |
| data | `LEN - 8` bytes |
| `SUM` | Sum of all bytes between `5A` and `SUM`, truncated to 8 bits. The original module drops frames with a wrong sum. |
| `5B` | End marker |

There is no escaping. `5A`, `5B` and `0A` can appear inside data and checksum, so a receiver must frame on
`LEN`, not on the end marker. The heater did not answer frames whose checksum happened to be `5A` or `5B`.

Commands from the ESP are 16 bytes long (`LEN = 0x10`, 8 data bytes). The status frame from the heater is
17 bytes long.

### Acknowledgement

The heater acknowledges every command it recognises by sending the frame back unchanged (an "echo"),
within about 100 ms. In most cases a `C9` status frame follows. Commands it does not recognise are
silently ignored. If the ESP transmits while the heater is sending, the command can be lost without an
echo, so the sender should wait for the echo and retry.

## Commands: ESP → heater

Bytes are numbered from `5A` = byte 0. Data starts at byte 6.

| CMD | Data | Reply | Meaning |
|---|---|---|---|
| `41` | none | echo, `C9`, `CB` | Request status. Also seemed to wake the display backlight (seen once). |
| `43` | `VV` | echo, `C9` | *Unknown*. Nothing visible changed with `00` or `01`. |
| `44` | none | echo, `C9`, `CB` | *Unknown*. Behaves like `41`. |
| `45` | none | echo, `C9`, `24` frame | *Unknown*. Triggers a short `24 A6 6A` frame (version/ID?). |
| `46` | `01 00 TT` | echo, `C9` | Set target temperature to `TT` °C. Byte 6 (`01`) can also be `02` with the same effect. |
| `47` | `PP` | echo, `C9` | Power: `01` = on (heat), `00` = off |
| `48` | `RR` | echo, `C9` | Set the reported room temperature to `RR` °C. The heater keeps following its sensor afterwards, so this behaves like a calibration offset. **Sending `00` makes the heater think the room is 0 °C and start heating.** |
| `50` | none | `50 00 TT RR PP ...` | Read target (`TT`), room (`RR`) and power (`PP`) |
| `51` | `VV` | echo, `C9` | Display indicator: `01` shows "56" on the display, `00` clears it. `02`, `03`, `0A` and `38` showed nothing. Meaning *unknown*, possibly a Wi-Fi/cloud status code from the original module. |
| `52` | `VV` | echo, `C9` | *Unknown*. Nothing visible changed with `00` or `01`. |

All other values from `00` to `FF` gave no reply when sent with all-zero data.

Example, set the target to 12 °C:

```
5A 00 10 00 00 46 01 00 0C 00 00 00 00 00 63 5B
```

## Messages: heater → ESP

| CMD | Frame | When | Meaning |
|---|---|---|---|
| `C9` | `5A 00 11 00 00 C9 00 TT RR 00 PP 00 HH 00 00 SUM 5B` | Every ~60 s, after every change and after commands | Status. `TT` target °C, `RR` room °C, `PP` power (0/1), `HH` heating relay on (0/1). |
| `CA` | `5A 00 10 00 00 CA RR 00 ... SUM 5B` | When the room temperature changes | `RR` = new room temperature °C |
| `CB` | `5A 00 10 00 00 CB 00 ... DB 5B` | Every ~120 s, ~1 s after a `C9` | *Unknown*, all data is zero. Possibly a poll that the original Wi-Fi module answered. |
| `08` | `5A 00 0A 01 00 08 D8 8D 78 5B` | Wi-Fi button pressed | *Unknown* payload, always the same. Probably a request to enter pairing mode. |
| `24` | `5A 00 0A 00 00 24 A6 6A 3E 5B` | After command `45` | *Unknown* payload |

The settings ("gear") button on the panel did not produce a frame of its own.

### Pairing mode

After the Wi-Fi button is pressed (`08` frame), the Wi-Fi LED blinks for about 2 minutes. During that time
the heater ignores all commands: no echo and no effect. It answers normally again once the LED turns off.

## Wi-Fi icon: ESP → heater

The original module drives the Wi-Fi icon with short 10-byte frames, always with `SEQ` = `01`. The codes and
data bytes are in the module firmware (issue #12), and the icon colours were captured independently in
issue #10. The component sends these when `wifi_icon: true` is set.

| Frame | Icon | Original meaning |
|---|---|---|
| `5A 00 0A 01 00 02 F0 0F 0C 5B` | green, blinking | Wi-Fi connected |
| `5A 00 0A 01 00 03 F1 1F 1E 5B` | off | Wi-Fi disconnected |
| `5A 00 0A 01 00 04 F2 2F 30 5B` | green | cloud connected |
| `5A 00 0A 01 00 05 F3 3F 42 5B` | red | cloud disconnected |

Issue #10 reports that the buttons behave differently once the icon is driven, as if a "perceived
temperature" calibration starts. This is not understood yet, which is why the option is off by default.

## What the original module firmware shows

A flash dump of the original HF-LPT120A (issue #12, firmware "Mill Device G2" from December 2021, built on
the AbleCloud SDK) contains the module's side of this protocol. The module is mostly a pipe: every frame
from the heater is forwarded to the cloud, and every command from the Mill app is sent to the heater
unchanged. So the commands `41`–`52` and the messages `C9`–`CB` are defined by the heater and the app, and the
module firmware says nothing about what their data means.

The module handles these codes from the heater itself instead of forwarding them:

| CMD | Data | Notes |
|---|---|---|
| `00` | | *Unknown* |
| `07` | 124 or 125 bytes (`LEN` `84`/`85`) | Probably the heater's registration with the cloud: device ID, type and key. The 133-byte `settInit` frame in issue #10 is this. |
| `08` | `D8 8D` | Wi-Fi button (measured above). The module replies with a code `08` frame. |
| `09` | `E9 9E` | *Unknown*, probably another button combination. The module replies with code `09`. |
| `0A` | `FA AF` | *Unknown*. The module replies with code `0A`. |
| `0B` | `B7 7B` | *Unknown*. The module replies with code `0B`. |
| `0C` | text | Module name or ID (the handler uses the string `Mill Device G2`). |
| `0D`, `0E` | | *Unknown* |
| `24` | `A6 6A` | Seen after command `45` (measured above). The module replies with code `24`. |

Whether those replies go back to the heater or to the cloud has not been traced yet.

## Open questions

- What `43` and `52` set, and what the "56" shown by `51 01` means.
- What the original module answered to `08`, so that pairing mode ends sooner. The firmware builds a code `08`
  reply; its bytes and destination still need to be traced.
- What the `08`, `24` and `CB` payloads mean, and what the original module replied.
- Whether `48` is stored across power cycles.

## Sources

- Measurements on a Mill panel heater gen2, October 2026 (most of this document).
- The HF-LPT120A firmware dump in issue #12 (frame parser, Wi-Fi icon, codes handled by the module).
- Wi-Fi icon and registration captures in issue #10.
