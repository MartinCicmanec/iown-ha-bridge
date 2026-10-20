# Captures

Real io-homecontrol frames captured with the receive-only sniffer (`src/sniffer.cpp`, env `ttgo-lora32-v21-sniffer`).

## situo-variation-am-io-pure-ii_2026-10-18

- Transmitter: Somfy Situo Variation A/M io Pure II (1W remote), node ID `B51A3F`
- Receiver: LilyGO TTGO LoRa32 T3 V1.6.1 (SX1276), 868.95 MHz, 2-FSK, 38.4 kbps, 19.2 kHz deviation
- Button sequence: Up, Stop/My, Down, Up
- `.log`: sniffer serial output (`raw` = SX1276 FIFO after sync word, `frame` = UART decoded frame, `err@n` = first framing error, i.e. end of frame)
- `_frames.txt`: unique decoded frames, one per line, without sync word `FF33`

All 16 unique frames pass the CRC check (`compute_crc_8408`) and decode with the Kaitai parser (`scripts/iohomecontrol.py`).

| Counter | Command | Parameter | Button |
| :-----: | :-----: | :-------- | :----- |
| `36A9` | `0x00` | `014300000000` | Up |
| `36AA`, `36AB` | `0x20` | `02FF0143000C0000`, `02FF01430005FF00` | (after Up) |
| `36AC` | `0x00` | `0143D20020D20000` | Stop/My |
| `36AD`, `36AE` | `0x20` | `02FF0143020C0000`, `02FF01430205FF00` | (after Stop/My) |
| `36AF` | `0x00` | `0143C8000000` | Down |
| `36B0`, `36B1` | `0x20` | `02FF0143010C0000`, `02FF01430105FF00` | (after Down) |
| `36B2` | `0x00` | `014300000000` | Up |
| `36B3`, `36B4` | `0x20` | `02FF0143000C0000`, `02FF01430005FF00` | (after Up) |

Observations (not verified):

- Every command is repeated 3-4 times with the same counter. The first repeat has control byte 1 = `0x20`, the following ones `0x00`.
- Every button press sends one `0x00` (execute function) followed by two `0x20` frames.

## situo-prog_2026-10-20 (not committed)

PROG presses on the same Situo, captured with the 64-byte sniffer. The raw log is
**not** in the repo: the `0x30` frame carries the remote's QR productKey in clear.
Decoded sequence of one PROG cycle (three cycles were captured, counters
`37C3`, `37D3`, `37E8`):

| Counter | Command | Parameters | Notes |
| :-----: | :-----: | :--------- | :---- |
| N | `0x39` remove 1W controller | `00` | MAC'd as usual |
| N+1 | `0x30` send 1W key | `<16-byte key> 02 01` | ctrl0 `FC` (length 28), **no MAC**, counter inside the data |
| N+2 | `0x20` private | `02 03 0C 00 00` | MAC'd |
| N+3 … | `0x20` private | `02 03 05 <02, 03, …> 00` | one frame per step, ends with `05 FF 00` |
| (within) | `0x2E` | `00` | after step `08`, first repeat ctrl1 `0x20` |

Findings:

- `0x30` layout: `30 | encrypted key (16) | manufacturer 02 | 01 | counter (2)`, with no MAC.
  This differs from upstream's `demo()` frame (`Iown-ioCrypto.py`), which appends a MAC
  over `30 + key` and has a length byte that can't fit it.
- The encrypted key in `0x30` equals the Situo's QR productKey, so the QR code holds the
  key exactly as the remote sends it during pairing.
- The number of `02 03 05 xx` steps differs per cycle (up to `0C`, `11`, `0E`).
  Possibly tied to how long PROG was held (not verified).
