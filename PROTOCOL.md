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
5A | 00 | LEN | SEQ | 00 | CMD | data ... | SUM | 5B
```

| Field | Description |
|---|---|
| `5A` | Start marker |
| `00` | Always 0 |
| `LEN` | Total frame length in bytes, from `5A` to `5B` inclusive |
| `SEQ` | Ignored by the heater as far as tested (`00`, `06`, `16` and `22` all work). The heater always sends `00`, except in the Wi-Fi button frame (`01`). |
| `00` | Always 0 |
| `CMD` | Command / message type |
| data | `LEN - 8` bytes |
| `SUM` | Sum of all bytes between `5A` and `SUM`, truncated to 8 bits |
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
| `41` | none | echo, `C9`, `CB` | Request status |
| `43` | none | echo, `C9` | *Unknown*. Nothing visible changed with all-zero data. |
| `44` | none | echo, `C9`, `CB` | *Unknown*. Behaves like `41`. |
| `45` | none | echo, `C9`, `24` frame | *Unknown*. Triggers a short `24 A6 6A` frame (version/ID?). |
| `46` | `01 00 TT` | echo, `C9` | Set target temperature to `TT` °C. Byte 6 (`01`) can also be `02` with the same effect. |
| `47` | `PP` | echo, `C9` | Power: `01` = on (heat), `00` = off |
| `48` | `RR` | echo, `C9` | Set the reported room temperature to `RR` °C. The heater keeps following its sensor afterwards, so this behaves like a calibration offset. **Sending `00` makes the heater think the room is 0 °C and start heating.** |
| `50` | none | `50 00 TT RR PP ...` | Read target (`TT`), room (`RR`) and power (`PP`) |
| `51` | none | echo | *Unknown* |
| `52` | none | echo | *Unknown* |

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

## Open questions

- What `43`, `51` and `52` set. Try non-zero data while watching the display.
- What the `08`, `24` and `CB` payloads mean, and what the original module replied.
- Whether `48` is stored across power cycles.
