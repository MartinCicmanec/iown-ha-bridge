// Template for include/iown_key.h (git-ignored). DO NOT put real keys in this
// example file.
//
// The controller identity block is the LoRa32's own 1W controller (random base
// address + secret; blind i uses base + i). src/mqtt.cpp needs it. Create it with:
//
//   python3 scripts/Iown-GenController.py
//
// IOWN_PRODUCT_KEY is optional: the 16-byte "productKey" from your remote's QR code
// (qr[8:40]), used only by the analysis scripts (Iown-Decode.py --key-header). Write it with:
//
//   python3 scripts/Iown-GenKeyHeader.py <qr-text-file> include/iown_key.h
//
// Iown-GenKeyHeader.py keeps the controller identity block when it rewrites the header.
#pragma once

#define IOWN_PRODUCT_KEY { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 } // 16 bytes (from QR)

// BEGIN controller identity (scripts/Iown-GenController.py) - back this up!
#define IOWN_CTRL_ADDR   { 0x00, 0x00, 0x00 }                                                   // 3 bytes, base address
#define IOWN_CTRL_SECRET { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 } // 16 bytes
// END controller identity
