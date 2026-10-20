# Sniff, decode and test

A short, practical workflow for capturing io-homecontrol 1W frames from your
remote, decoding them, checking your remote's key, and then pairing and testing
the LoRa32 as its own remote, all with a single LilyGO TTGO LoRa32 T3 V1.6.1. Background on the protocol and the Home
Assistant setup is in the [Home Assistant / MQTT guide](home-assistant-mqtt-guide.md).

| Step | Firmware env | Script |
| --- | --- | --- |
| 1. Sniff | `ttgo-lora32-v21-sniffer` | `scripts/Iown-Capture.py` |
| 2. Decode | – | `scripts/Iown-Decode.py` |
| 3. Check the key (optional) | – | `scripts/Iown-VerifyQRKey.py`, `scripts/Iown-GenKeyHeader.py` |
| 4. Set up and pair | `ttgo-lora32-v21-mqtt` | `scripts/Iown-GenController.py`, `scripts/mqtt-test.sh` |
| 5. Test MQTT | `ttgo-lora32-v21-mqtt` | `scripts/mqtt-test.sh` |

Steps 1–3 are only needed to look at your remote's traffic; to just use the
LoRa32, go to step 4.

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

Don't press PROG while capturing into the repo: a PROG press sends the remote's
key in a `0x30` frame, so such a log is a secret (keep it in `~/.config/iown/`).

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

- **`src`**: the channel address of your remote. Each channel has its own,
  adjacent address.
- **`counter`**: the highest counter seen for each channel. A press uses about
  3 counter values: one `cmd=00` and two `cmd=20` (Somfy private) frames.
- **`crc=BAD`** frames are corrupted receptions. `--frames-out` keeps only
  CRC-valid frames.

`captures/situo-variation-am-io-pure-ii_2026-10-18.log` is a real example to try
the decoder on.

## 3. Check the key (optional)

Save the QR text of your remote to a private file outside the repo, e.g.
`~/.config/iown/situo-qr.txt` (`chmod 600`). Then:

```bash
# does the QR productKey reproduce the MACs you sniffed?
python3 scripts/Iown-VerifyQRKey.py ~/.config/iown/situo-qr.txt captures/my-remote_frames.txt
#   success: "productKey decrypted (IV from node ID)  MAC match N/N"

# write the git-ignored key header (the key is never printed)
python3 scripts/Iown-GenKeyHeader.py ~/.config/iown/situo-qr.txt include/iown_key.h

# every frame should now show mac=ok
python3 scripts/Iown-Decode.py captures/my-remote_frames.txt --key-header include/iown_key.h
```

`mac=BAD` with `crc=ok` means the productKey doesn't belong to that remote (each
channel's key is derived from the productKey and the frame's `src` address).

## 4. Set up and pair

Create the LoRa32's own identity and the WiFi/MQTT header, list your blinds in
`BLINDS[]` (`src/mqtt.cpp`), then flash and watch the serial log:

```bash
python3 scripts/Iown-GenController.py              # include/iown_key.h; back it up
cp include/iown_net.example.h include/iown_net.h   # then edit
scripts/flash.sh
.venv/bin/pio device monitor -b 115200
#   [CTRL] Blind_1 address 131CC6 counter 0
#   [SELFTEST] MAC, key derivation, 0x30 frame OK
#   [WIFI] connecting... <ip>
#   [MQTT] connecting... ok
```

Pair each blind as in the guide's [Pairing a blind](home-assistant-mqtt-guide.md#9-pairing-a-blind):
blind half-way, PROG on your remote until it jogs, then once:

```bash
scripts/mqtt-test.sh Blind_1 pair
#   serial: [PAIR] Blind_1 sending PROG sequence
#           [TX] Blind_1 131CC6 base=0x0001 nframes=14 state=0
```

The blind jogs to confirm.

## 5. Test the MQTT firmware

Drive it with the mosquitto clients (package `mosquitto` or `mosquitto-clients`):

```bash
export MQTT_HOST=192.168.1.10 MQTT_USER=lora32 MQTT_PASS=...
scripts/mqtt-test.sh watch              # discovery configs, state and counters
scripts/mqtt-test.sh Blind_1 open
scripts/mqtt-test.sh Blind_1 stop       # halts a moving blind only
scripts/mqtt-test.sh Blind_1 my         # go to favourite position
scripts/mqtt-test.sh Blind_1 counter 100   # set the counter (decimal)
```

Every command prints `[TX] <name> <address> base=0x.... nframes=.. state=0` on
serial and updates `iown/<name>/counter`. Right after pairing, the motor may
ignore the first command; send it again.

## Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| No `RX` lines at all | Wrong board variant (needs 868 MHz), antenna missing, or remote too far away |
| Only `crc=BAD` | Weak signal: move the remote closer |
| `mac=BAD`, `crc=ok` | The productKey in `include/iown_key.h` is not this remote's |
| `[TX] ... state=0` but blind doesn't move | Not paired (pair again), or the counter isn't above the motor's last one (flash was erased: raise it with the `counter` topic) |
| No jog after Pair | Motor wasn't in pairing mode: PROG on the remote until the blind jogs first |
| `[SELFTEST] ... mismatch - halting` | Crypto code is broken (the tests use fixed public vectors, not your keys) |
| Build error "no controller identity" | Run `scripts/Iown-GenController.py` |
| `[MQTT] ... rc=5` | Broker rejected the username/password |
