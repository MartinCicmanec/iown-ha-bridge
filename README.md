# iown-ha-bridge

Control **Somfy io-homecontrol® 1W blinds** from Home Assistant with a LilyGO
TTGO LoRa32 T3 V1.6.1 (ESP32 + SX1276, 868 MHz). The board emulates your 1W
remote and exposes each blind as an MQTT-discovered `cover` plus a "My" button.

Built on the protocol research of
[Velocet/iown-homecontrol](https://github.com/Velocet/iown-homecontrol), included
as a git submodule (see [Attribution](#attribution)).

> Verified only with a *Somfy Situo Variation A/M io Pure II* remote and its
> motors. 1W profile only. Use it only on equipment you own.

## Contents

| Path | What |
| --- | --- |
| `src/sniffer.cpp` | Receive-only sniffer firmware (env `ttgo-lora32-v21-sniffer`) |
| `src/tx.cpp` | One-shot 1W transmit test (env `ttgo-lora32-v21-tx`) |
| `src/mqtt.cpp` | Home Assistant / MQTT bridge (env `ttgo-lora32-v21-mqtt`) |
| `scripts/Iown-Capture.py` | Record sniffer serial output to a log |
| `scripts/Iown-Decode.py` | Decode frames, check CRC and 1W MAC |
| `scripts/Iown-VerifyQRKey.py` | Check that the QR productKey reproduces sniffed MACs |
| `scripts/Iown-GenKeyHeader.py` | Generate the git-ignored `include/iown_key.h` |
| `scripts/mqtt-test.sh` | Drive the MQTT firmware with mosquitto clients |
| `scripts/upstream.py` | Loads upstream's Python crypto modules from the submodule |
| `include/*.example.h` | Templates for the git-ignored key and WiFi/MQTT headers |
| `captures/` | Real captured frames from a Situo remote |
| `extern/iown-homecontrol` | Upstream submodule, pinned to `b7a526d` |
| `extern/LoRa32` | Upstream Velocet/LoRa32 submodule (board pins), pinned to `4d4a167` |

## Quick start

```bash
git clone --recurse-submodules <this repo> && cd iown-ha-bridge
python3 -m venv .venv && .venv/bin/pip install platformio pyserial
```

Both upstream projects come in as submodules, so nothing else needs to be
cloned. Already cloned without them? Run `git submodule update --init`.

### Board macro workaround

The board pins come from upstream [LoRa32](https://github.com/Velocet/LoRa32),
used unmodified. Its `LoRa32.h` detects the TTGO LoRa32 v2.1 by the macro
`ARDUINO_TTGO_LORA32_V21NEW`, but PlatformIO's `ttgo-lora32-v21` board defines
`ARDUINO_TTGO_LoRa32_v21new` (different case). Without a match, `LORA32_RADIO`
is never defined and the build fails with `'LORA32_RADIO' does not name a type`.

`platformio.ini` therefore defines the spelling LoRa32 expects:

```ini
build_flags = ...
              -DARDUINO_TTGO_LORA32_V21NEW
```

That selects LoRa32's TTGO v2.1 pins: SX1276, SPI 18/27/19/5 (SS, MOSI, MISO,
SCK), radio 23/26/33/32 (RESET, IO0, IO1, IO2). Remove the flag once LoRa32
also accepts the PlatformIO spelling.

Then follow the guides:

- [Sniff, decode and test](docs/sniff-decode-test.md): capture your remote, decode
  frames, verify the key, test a transmit and the MQTT firmware.
- [Home Assistant / MQTT guide](docs/home-assistant-mqtt-guide.md): protocol
  background, configuration, HA setup, topics, the rolling counter, and limitations.

## Attribution

The protocol knowledge and the crypto in this repo come from upstream
**iown-homecontrol** by Velocet and contributors
(<https://github.com/Velocet/iown-homecontrol>, submodule `extern/iown-homecontrol`).
Below is everything taken from it.

### Code ported from upstream

All of these are C++ ports of upstream `scripts/Iown-ioCrypto.py`:

| Here | Upstream function | Notes |
| --- | --- | --- |
| `ivChecksum()` in `src/tx.cpp`, `src/mqtt.cpp` | `computeChecksum()` | IV checksum bytes |
| `buildIV()` in `src/tx.cpp`, `src/mqtt.cpp` | `constructInitialValue()` | 1W sequence-number IV (data padded with `0x55`) |
| `create1wMac()` in `src/tx.cpp`, `src/mqtt.cpp` | `create_1W_hmac()` | MAC = AES(key, IV)[:6] |
| `deriveKey()` in `src/mqtt.cpp` | `encrypt_1W_key()` | key = AES(transfer key, IV(address)) XOR productKey |
| `TRANSFER_KEY` in `src/mqtt.cpp` | `transfer_key` | the public io-homecontrol transfer key |
| `crc8408()` in `src/tx.cpp`, `src/mqtt.cpp` | `compute_crc_8408()` | also documented in upstream `docs/radio.md` |

### Upstream code used directly

- `extern/iown-homecontrol/src/IoHome.h`: the `IOHOME_SYNC_WORD` and
  `IOHOME_SYNC_WORD_LEN` constants, used by all three firmwares.
- `extern/iown-homecontrol/scripts/Iown-ioCrypto.py` and `Iown-AES.py`: imported
  at runtime by `Iown-Decode.py`, `Iown-VerifyQRKey.py` and `Iown-GenKeyHeader.py`
  through `scripts/upstream.py`.

### LoRa32 (submodule `extern/LoRa32`)

[Velocet/LoRa32](https://github.com/Velocet/LoRa32) (MIT), used unmodified as a
PlatformIO library. It provides the radio type and pins: `LORA32_RADIO`,
`LORA32_SPI_CS`, `LORA32_RADIO_RST`, `LORA32_RADIO_IO0` and `LORA32_RADIO_IO1`,
used by all three firmwares. See [Board macro workaround](#board-macro-workaround).

### Taken from upstream documentation

| Topic | Upstream source | Used in |
| --- | --- | --- |
| Radio: 868.95 MHz (channel 2), 2-FSK, 38.4 kbps, NRZ | `docs/radio.md`, `src/main.cpp` | all firmwares |
| UART byte framing (start 0, 8 bits LSB first, stop 1) | `docs/radio.md` | `uartEncode()` / `uartDecode()` / `decodeUart()` |
| Frame layout: ctrl0/ctrl1, dest, src, data, counter, MAC, CRC | `docs/linklayer.md` | `buildFrame()`, `Iown-Decode.py` |
| MAC test vector (key `0102..16`, seq `1234` → `19e81ec43d5e`) | `docs/linklayer.md` | `selfTest()` in `src/tx.cpp`, `Iown-VerifyQRKey.py` |
| Command `0x00` (execute function), main parameter (`0x0000`–`0xC800` %, `0xD200` current) | `docs/commands.md` | command constants, `Iown-Decode.py` |
| Command `0x20` (manufacturer private, `0x02` = Somfy) | `docs/commands.md` | `MY_SEQ` in `src/mqtt.cpp`, `Iown-Decode.py` |
| Key obfuscation with a shared transfer key (1W) | `docs/linklayer.md` | `deriveKey()` |

### Our own work

These are not from upstream:

- The MQTT / Home Assistant bridge: discovery, topics, NVS counter store, passive
  counter tracking, and counter resync.
- The 3-frame *go to My* sequence and the observed button codes, captured from a
  Situo remote (`captures/`).
- The Situo QR payload layout (address at `qr[2:8]`, productKey at `qr[8:40]`).
  We found it experimentally with `Iown-VerifyQRKey.py`; upstream does not
  document it.
- The sniffer flow, the capture/decode/test scripts, and the guides.

### Other dependencies

| Dependency | Use | Licence |
| --- | --- | --- |
| [RadioLib](https://github.com/jgromes/RadioLib) | SX1276 FSK driver | MIT |
| [LoRa32](https://github.com/Velocet/LoRa32) (submodule) | board pin definitions | MIT |
| [PubSubClient](https://github.com/knolleary/pubsubclient) | MQTT client | MIT |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | discovery payloads | MIT |
| ESP32 Arduino core (mbedTLS, Preferences, WiFi) | AES, NVS, WiFi | Apache-2.0 / LGPL |

## Licence

[CC-BY-SA-4.0](LICENSE). Upstream iown-homecontrol is ambiguous about its own
licence: its `LICENSE` file is CC0-1.0, but its `library.json` says
CC-BY-SA-4.0. This repo uses CC-BY-SA-4.0 so that it complies under either.
