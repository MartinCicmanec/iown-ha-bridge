# Sniff, decode and test

A short, practical workflow for capturing io-homecontrol 1W frames from your
remote, decoding them, checking your key, and testing transmission, all with a
single LilyGO TTGO LoRa32 T3 V1.6.1. Background on the protocol and the Home
Assistant setup is in the [Home Assistant / MQTT guide](home-assistant-mqtt-guide.md).

| Step | Firmware env | Script |
| --- | --- | --- |
| 1. Sniff | `ttgo-lora32-v21-sniffer` | `scripts/Iown-Capture.py` |
| 2. Decode | – | `scripts/Iown-Decode.py` |
| 3. Check the key | – | `scripts/Iown-VerifyQRKey.py`, `scripts/Iown-GenKeyHeader.py` |
| 4. Test a transmit | `ttgo-lora32-v21-tx` | – |
| 5. Test MQTT | `ttgo-lora32-v21-mqtt` | `scripts/mqtt-test.sh` |

## 0. Setup

```bash
git clone --recurse-submodules <this repo> && cd iown-ha-bridge
python3 -m venv .venv
.venv/bin/pip install platformio pyserial
```

The board shows up as `/dev/ttyACM0` (Linux) in the examples below; pass
`--port` / `--upload-port` if yours differs.

## 1. Sniff

Flash the receive-only sniffer and record what it hears:

```bash
.venv/bin/pio run -e ttgo-lora32-v21-sniffer -t upload
.venv/bin/python scripts/Iown-Capture.py captures/my-remote.log --port /dev/ttyACM0
```

Hold the remote within a metre or two of the board and press buttons in a
sequence you write down, e.g. *Up, Stop/My, Down, Up*, with a pause between
presses. Press Ctrl+C when done. Each line looks like:

```
RX rssi=-20.0 raw=... frame=F80000003FB51A3F000143D20020D2000036AC03C02838B74CFBC8 err@27
```

Your presses read roughly −15 to −70 dBm. Lines near −100 dBm are distant
traffic from other io devices; ignore them.

For a multi-channel remote, repeat a press on each channel. Each channel uses
its own source address.

## 2. Decode

```bash
python3 scripts/Iown-Decode.py captures/my-remote.log --unique --frames-out captures/my-remote_frames.txt
```

One line per distinct frame:

```
[-20.0 dBm] 1W ctrl0=F8 (len 24) ctrl1=00  dest=00003F src=B51A3F  cmd=00 (execute function) params=0143D20020D20000  position=current (stop / My)  counter=36AC mac=03C02838B74C  crc=ok
```

What to note down:

- **`src`**: the channel address. You need it for `BLINDS[]` in `src/mqtt.cpp`
  and, for the tx test, `IOWN_NODE_ID`.
- **`counter`**: the highest counter seen for each channel. A press uses about
  3 counter values: one `cmd=00` and two `cmd=20` (Somfy private) frames.
- **`crc=BAD`** frames are corrupted receptions. `--frames-out` keeps only
  CRC-valid frames.

`captures/situo-variation-am-io-pure-ii_2026-10-18.log` is a real example to try
the decoder on.

## 3. Check the key

Save the QR text of your remote to a private file outside the repo, e.g.
`~/.config/iown/situo-qr.txt` (`chmod 600`). Then:

```bash
# does the QR productKey reproduce the MACs you sniffed?
python3 scripts/Iown-VerifyQRKey.py ~/.config/iown/situo-qr.txt captures/my-remote_frames.txt
#   success: "productKey decrypted (IV from node ID)  MAC match N/N"

# write the git-ignored key header (the key is never printed)
python3 scripts/Iown-GenKeyHeader.py ~/.config/iown/situo-qr.txt include/iown_key.h [channel-address-hex]

# every frame should now show mac=ok
python3 scripts/Iown-Decode.py captures/my-remote_frames.txt --key-header include/iown_key.h
```

`mac=BAD` with `crc=ok` means the key or the address is wrong. Check that you
passed the right channel address to `Iown-GenKeyHeader.py`.

## 4. Test a transmit

`src/tx.cpp` sends one *Up* command once at boot, as the channel in
`IOWN_NODE_ID` / `IOWN_KEY`. The motor only accepts a counter **above** the last
one it saw, so use the highest counter from step 2 plus one (or a bit more):

```bash
PLATFORMIO_BUILD_FLAGS="-DIOWN_TX_COUNTER=0x36B5" .venv/bin/pio run -e ttgo-lora32-v21-tx -t upload
.venv/bin/pio device monitor -b 115200
#   [TX] counter=0x36B5 ... [TX] rep 0..3 state 0 ... [TX] done
```

The blind should go up. Add `-DIOWN_TX_LEVEL=0xC8` to the build flags for *Down*.

- Resetting the board resends the **same** counter, which the motor ignores.
  Rebuild with a higher counter for each new test.
- The board impersonates your remote. After the board has used higher counters,
  the physical remote may need a few presses before the motor obeys it again.

## 5. Test the MQTT firmware

Fill in `include/iown_net.h` (copy from `include/iown_net.example.h`) and
`BLINDS[]` in `src/mqtt.cpp`, then flash and watch the serial log:

```bash
.venv/bin/pio run -e ttgo-lora32-v21-mqtt -t upload
.venv/bin/pio device monitor -b 115200
#   [SELFTEST] key derivation + MAC OK
#   [WIFI] connecting... <ip>
#   [MQTT] connecting... ok
```

In a second terminal, drive it with the mosquitto clients (package `mosquitto`
or `mosquitto-clients`):

```bash
export MQTT_HOST=192.168.1.10 MQTT_USER=lora32 MQTT_PASS=...
scripts/mqtt-test.sh watch              # discovery configs, state and counters
scripts/mqtt-test.sh Blind_1 open
scripts/mqtt-test.sh Blind_1 stop       # halts a moving blind only
scripts/mqtt-test.sh Blind_1 my         # go to favourite position
scripts/mqtt-test.sh Blind_1 counter 14300   # resync counter (decimal)
```

Every command prints `[TX] <name> base=0x.... nframes=.. state=0` on serial and
updates `iown/<name>/counter`. When you use the physical remote while the board
is idle, the serial log shows `[RX] <name> heard counter=0x.... -> tracked`.

## Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| No `RX` lines at all | Wrong board variant (needs 868 MHz), antenna missing, or remote too far away |
| Only `crc=BAD` | Weak signal: move the remote closer |
| `mac=BAD`, `crc=ok` | Wrong productKey or channel address for the key |
| `[TX] ... state=0` but blind doesn't move | Counter not above the motor's last one: bump it (`counter` topic or rebuild tx) |
| `[SELFTEST] key/MAC mismatch - halting` | `IOWN_PRODUCT_KEY` in `include/iown_key.h` is wrong |
| `[MQTT] ... rc=5` | Broker rejected the username/password |
