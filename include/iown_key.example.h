// Template for include/iown_key.h (git-ignored). DO NOT put real keys in this
// example file. Generate the real header with:
//
//   python3 scripts/Iown-GenKeyHeader.py \
//       <qr-text-file> include/iown_key.h [node-id-hex]
//
// IOWN_PRODUCT_KEY is the 16-byte "productKey" from your remote's QR code
// (qr[8:40]). src/mqtt.cpp derives each channel's AES key from this plus the
// channel address, so this one value covers every channel/blind.
//
// IOWN_NODE_ID / IOWN_KEY are a single channel's address and derived key, used
// only by src/tx.cpp (the one-shot transmit test). src/mqtt.cpp ignores them.
//
// The optional controller identity block is the LoRa32's own 1W controller
// (random base address + secret, for pairing it as a separate remote). Add it with:
//
//   python3 scripts/Iown-GenController.py
//
// Iown-GenKeyHeader.py keeps this block when it regenerates the header.
#pragma once

#define IOWN_NODE_ID     { 0x00, 0x00, 0x00 }                                                   // 3 bytes, frame order
#define IOWN_KEY         { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 } // 16 bytes
#define IOWN_PRODUCT_KEY { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 } // 16 bytes (from QR)

// BEGIN controller identity (scripts/Iown-GenController.py) - back this up!
#define IOWN_CTRL_ADDR   { 0x00, 0x00, 0x00 }                                                   // 3 bytes, base address
#define IOWN_CTRL_SECRET { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 } // 16 bytes
// END controller identity
