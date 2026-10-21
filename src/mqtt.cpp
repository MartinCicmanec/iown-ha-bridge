/**
 * @file mqtt.cpp "mqtt.cpp"
 * @brief iown-homecontrol 1W controller exposed to Home Assistant over MQTT/WiFi
 *
 * The LoRa32 is its own 1W controller: blind i uses address IOWN_CTRL_ADDR + i with
 * a key derived from IOWN_CTRL_SECRET, paired to the motor like a new remote
 * (iown/<name>/pair while a paired remote holds the motor in pairing mode).
 * Each blind is one HA cover with OPEN / CLOSE / STOP plus "My" and "Pair" buttons.
 * Covers are optimistic (1W has no position feedback). Each blind's rolling
 * counter is kept in NVS and can be set over MQTT.
 *
 * Secrets: include/iown_key.h (IOWN_CTRL_*) and include/iown_net.h (WiFi/MQTT),
 * both git-ignored.
 *
 * Build/flash: pio run -e ttgo-lora32-v21-mqtt -t upload
 *
 * Crypto (IV, MAC, key derivation) ported from upstream iown-homecontrol scripts/Iown-ioCrypto.py (Velocet), see README.md.
 *
 * SPDX-License-Identifier: CC-BY-SA-4.0
 * Copyright (c) 2026 Martin Cicmanec
 */

#include <Arduino.h>
#include <SPI.h>
#include <string.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <mbedtls/aes.h>

#include <LoRa32.h>
#include <RadioLib.h>

#include "IoHome.h"
#include "iown_key.h" // IOWN_CTRL_*  (git-ignored, generated)
#include "iown_net.h" // WiFi + MQTT creds (git-ignored)

#if !defined(IOWN_CTRL_ADDR) || !defined(IOWN_CTRL_SECRET)
  #error "include/iown_key.h has no controller identity: run scripts/Iown-GenController.py"
#endif

// io-homecontrol public transfer key (same value as scripts/Iown-ioCrypto.py)
static const uint8_t TRANSFER_KEY[16] = {
  0x34,0xc3,0x46,0x6e,0xd8,0x8f,0x4e,0x8e,0x16,0xaa,0x47,0x39,0x49,0x88,0x43,0x73};
static const uint8_t CTRL_ADDR[3]    = IOWN_CTRL_ADDR;   // LoRa32's base address
static const uint8_t CTRL_SECRET[16] = IOWN_CTRL_SECRET; // LoRa32's secret (sent in 0x30)

// --- blind configuration ---
// A blind's address is IOWN_CTRL_ADDR + its index here, and the motor is paired to
// that address: don't reorder or remove entries, only append (or re-pair).
struct Blind {
  const char* name;     // HA entity name
  uint8_t     addr[3];  // filled at boot, frame order (not secret)
  uint8_t     key[16];  // derived at boot
};
static Blind BLINDS[] = {
  {"Blind_1"}, // 131CC6 in this installation, paired 2026-10-21
  {"Blind_2"}, // 131CC7, paired 2026-10-21
};
static const size_t NBLINDS = sizeof(BLINDS) / sizeof(BLINDS[0]);

// command payloads (data = command ID + parameters, as captured from the Situo)
static const uint8_t CMD_OPEN[]  = {0x00, 0x01, 0x43, 0x00, 0x00, 0x00, 0x00};
static const uint8_t CMD_CLOSE[] = {0x00, 0x01, 0x43, 0xC8, 0x00, 0x00, 0x00};
static const uint8_t CMD_STOP[]  = {0x00, 0x01, 0x43, 0xD2, 0x00, 0x20, 0xD2, 0x00, 0x00};

// "Go to My/favourite" is a full 3-frame press: a stop (0x00) then two 0x20
// frames carrying the My button code (02). Sent with consecutive counters.
static const uint8_t MY_1[] = {0x00, 0x01, 0x43, 0xD2, 0x00, 0x20, 0xD2, 0x00, 0x00};
static const uint8_t MY_2[] = {0x20, 0x02, 0xFF, 0x01, 0x43, 0x02, 0x0C, 0x00, 0x00};
static const uint8_t MY_3[] = {0x20, 0x02, 0xFF, 0x01, 0x43, 0x02, 0x05, 0xFF, 0x00};
struct FrameData {const uint8_t* data;size_t len;bool keyFrame;}; // keyFrame: 0x30 built from the secret
static const FrameData MY_SEQ[] = {{MY_1, sizeof(MY_1)}, {MY_2, sizeof(MY_2)}, {MY_3, sizeof(MY_3)}};

// Pairing: the frame sequence a Situo sends on a PROG press (captures/README.md):
// 0x39 remove, 0x30 send key, 0x20 Somfy steps with a 0x2E in between. Sent with
// consecutive counters while the motor is in pairing mode.
static const uint8_t PAIR_REMOVE[] = {0x39, 0x00};
static const uint8_t PAIR_START[]  = {0x20, 0x02, 0x03, 0x0C, 0x00, 0x00};
static const uint8_t PAIR_2E[]     = {0x2E, 0x00};
#define PAIR_STEP(n) static const uint8_t PAIR_STEP_##n[] = {0x20, 0x02, 0x03, 0x05, 0x##n, 0x00};
PAIR_STEP(02) PAIR_STEP(03) PAIR_STEP(04) PAIR_STEP(05) PAIR_STEP(06) PAIR_STEP(07)
PAIR_STEP(08) PAIR_STEP(09) PAIR_STEP(0A) PAIR_STEP(FF)
#define STEP(n) {PAIR_STEP_##n, sizeof(PAIR_STEP_##n)}
static const FrameData PAIR_SEQ[] = {
  {PAIR_REMOVE, sizeof(PAIR_REMOVE)}, {nullptr, 0, true}, {PAIR_START, sizeof(PAIR_START)},
  STEP(02), STEP(03), STEP(04), STEP(05), STEP(06), STEP(07), STEP(08),
  {PAIR_2E, sizeof(PAIR_2E)}, STEP(09), STEP(0A), STEP(FF),
};

LORA32_RADIO radio = new Module(LORA32_SPI_CS, LORA32_RADIO_IO0, LORA32_RADIO_RST, LORA32_RADIO_IO1);
WiFiClient    wifiClient;
PubSubClient  mqtt(wifiClient);
Preferences   prefs;

// =================== crypto / framing (mirrors scripts/Iown-ioCrypto.py) ===================
static void aesEncryptBlock(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key, 128);
  mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in, out);
  mbedtls_aes_free(&ctx);
}

// key = AES(transfer_key, IV_from_address) XOR secret  (port of encrypt_1W_key)
static void deriveKey(const uint8_t addr[3], const uint8_t secret[16], uint8_t out[16]) {
  uint8_t iv[16], enc[16];
  for(int i = 0; i < 15; i += 3) {iv[i] = addr[0];iv[i+1] = addr[1];iv[i+2] = addr[2];}
  iv[15] = addr[0];
  aesEncryptBlock(TRANSFER_KEY, iv, enc);
  for(int i = 0; i < 16; i++) {out[i] = enc[i] ^ secret[i];}
}

static void ivChecksum(uint8_t frameByte, uint8_t* c1, uint8_t* c2) {
  uint8_t tmp = frameByte ^ *c2;
  uint8_t nc2 = (uint8_t)((*c1 & 0x7f) << 1);
  if((*c1 & 0x80) == 0) {
    if(tmp >= 128) {nc2 |= 1;}
    *c1 = nc2;
    *c2 = (uint8_t)(tmp << 1);
  } else {
    if(tmp >= 128) {nc2 |= 1;}
    *c1 = nc2 ^ 0x55;
    *c2 = (uint8_t)((tmp << 1) ^ 0x5b);
  }
}

static void buildIV(const uint8_t* data, size_t dataLen, const uint8_t counter[2], uint8_t iv[16]) {
  memset(iv, 0, 16);
  uint8_t c1 = 0, c2 = 0;
  for(size_t i = 0; i < dataLen; i++) {
    ivChecksum(data[i], &c1, &c2);
    if(i < 8) {iv[i] = data[i];}
  }
  for(size_t j = dataLen; j < 8; j++) {iv[j] = 0x55;}
  iv[8] = c1;iv[9] = c2;
  iv[10] = counter[0];iv[11] = counter[1];
  for(int i = 12; i < 16; i++) {iv[i] = 0x55;}
}

static void create1wMac(const uint8_t counter[2], const uint8_t key[16],
                        const uint8_t* data, size_t dataLen, uint8_t mac[6]) {
  uint8_t iv[16], enc[16];
  buildIV(data, dataLen, counter, iv);
  aesEncryptBlock(key, iv, enc);
  memcpy(mac, enc, 6);
}

static uint16_t crc8408(const uint8_t* data, size_t len) {
  uint16_t crc = 0;
  for(size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for(int b = 0; b < 8; b++) {crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);}
  }
  return crc;
}

static size_t buildFrame(const uint8_t addr[3], const uint8_t key[16], const uint8_t secret[16],
                         const FrameData& fd, uint8_t ctrl1, const uint8_t counter[2], uint8_t* out) {
  static const uint8_t dest[3] = {0x00, 0x00, 0x3F}; // broadcast
  size_t n = 0;
  if(fd.keyFrame) {
    // 0x30 send 1W key, as captured: 30 <secret> 02 01 <counter>, no MAC (length 28)
    out[n++] = (uint8_t)(0xE0 | (1 + 3 + 3 + 1 + 16 + 2 + 2));
    out[n++] = ctrl1;
    memcpy(out + n, dest, 3);n += 3;
    memcpy(out + n, addr, 3);n += 3;
    out[n++] = 0x30;
    memcpy(out + n, secret, 16);n += 16;
    out[n++] = 0x02;out[n++] = 0x01; // manufacturer Somfy, unknown
    out[n++] = counter[0];out[n++] = counter[1];
  } else {
    size_t lenField = 1 + 3 + 3 + fd.len + 2 + 6; // ctrl1..mac
    out[n++] = (uint8_t)(0xE0 | lenField);
    out[n++] = ctrl1;
    memcpy(out + n, dest, 3);n += 3;
    memcpy(out + n, addr, 3);n += 3;
    memcpy(out + n, fd.data, fd.len);n += fd.len;
    out[n++] = counter[0];out[n++] = counter[1];
    create1wMac(counter, key, fd.data, fd.len, out + n);n += 6;
  }
  uint16_t crc = crc8408(out, n);
  out[n++] = (uint8_t)(crc & 0xff);out[n++] = (uint8_t)(crc >> 8);
  return n;
}

static size_t uartEncode(const uint8_t* frame, size_t len, uint8_t* out, size_t outMax) {
  size_t bit = 0;
  auto put = [&](uint8_t v) {
    if(bit / 8 < outMax) {
      if(v) {out[bit / 8] |= (uint8_t)(0x80 >> (bit % 8));}
      else  {out[bit / 8] &= (uint8_t)~(0x80 >> (bit % 8));}
    }
    bit++;
  };
  memset(out, 0xFF, outMax);
  for(size_t i = 0; i < len; i++) {
    put(0);
    for(int b = 0; b < 8; b++) {put((frame[i] >> b) & 1);}
    put(1);
  }
  return (bit + 7) / 8;
}

// =================== counters (NVS) ===================
// Keyed by address (not entity name) so a counter follows its blind's identity.
// NVS keys must be <= 15 chars.
static String nvsKey(const Blind& bl) {
  char k[12];
  snprintf(k, sizeof(k), "c%02x%02x%02x", bl.addr[0], bl.addr[1], bl.addr[2]);
  return String(k);
}
static uint16_t counterGet(const Blind& bl) {return (uint16_t)prefs.getUShort(nvsKey(bl).c_str(), 0);}
static void counterSet(const Blind& bl, uint16_t v) {prefs.putUShort(nvsKey(bl).c_str(), v);}

// =================== radio TX ===================
// Transmit a sequence of frames with consecutive counters (base+1, base+2, ...),
// each repeated like the real remote (first copy with control byte 1 = 0x20).
static void sendFrames(Blind& bl, const FrameData* seq, size_t nframes) {
  uint16_t base = counterGet(bl);
  uint8_t frame[40], air[52];

  int16_t st = 0;
  for(size_t k = 0; k < nframes; k++) {
    uint16_t c = base + 1 + k;
    uint8_t counter[2] = {(uint8_t)(c >> 8), (uint8_t)(c & 0xff)};
    size_t flen = buildFrame(bl.addr, bl.key, CTRL_SECRET, seq[k], 0x00, counter, frame);
    size_t alen = uartEncode(frame, flen, air, sizeof(air));
    radio.fixedPacketLengthMode(alen);
    for(int rep = 0; rep < 4; rep++) {
      buildFrame(bl.addr, bl.key, CTRL_SECRET, seq[k], rep == 0 ? 0x20 : 0x00, counter, frame);
      uartEncode(frame, flen, air, sizeof(air));
      st = radio.transmit(air, alen);
      delay(40);
    }
  }
  counterSet(bl, base + nframes);
  Serial.printf("[TX] %s %02X%02X%02X base=0x%04X nframes=%u state=%d\n",
                bl.name, bl.addr[0], bl.addr[1], bl.addr[2], base + 1, (unsigned)nframes, st);
  radio.standby();
}

static void sendCommand(Blind& bl, const uint8_t* data, size_t dataLen) {
  FrameData one = {data, dataLen};
  sendFrames(bl, &one, 1);
}

// =================== MQTT topics + discovery ===================
static String baseTopic(const Blind& bl) {return String("iown/") + bl.name;}

static void publishState(const Blind& bl, const char* state) {
  mqtt.publish((baseTopic(bl) + "/state").c_str(), state, true);
}
static void publishCounter(const Blind& bl) {
  mqtt.publish((baseTopic(bl) + "/counter").c_str(), String(counterGet(bl)).c_str(), true);
}

static void publishDiscovery(const Blind& bl) {
  String uid = String("iown_") + bl.name;
  JsonDocument dev;
  dev["identifiers"][0] = "iown_lora32";
  dev["name"] = "iown-homecontrol LoRa32";
  dev["manufacturer"] = "iown-homecontrol";

  // cover
  {
    JsonDocument d;
    d["name"] = bl.name;
    d["unique_id"] = uid;
    d["command_topic"] = baseTopic(bl) + "/set";
    d["state_topic"] = baseTopic(bl) + "/state";
    d["payload_open"] = "OPEN";
    d["payload_close"] = "CLOSE";
    d["payload_stop"] = "STOP";
    d["state_open"] = "open";
    d["state_closed"] = "closed";
    d["optimistic"] = true;
    d["device_class"] = "blind";
    d["device"] = dev;
    String out;serializeJson(d, out);
    mqtt.publish((String("homeassistant/cover/") + uid + "/config").c_str(), out.c_str(), true);
  }
  // "My" button (same stop frame; recalls favourite when idle)
  {
    JsonDocument d;
    d["name"] = String(bl.name) + " My";
    d["unique_id"] = uid + "_my";
    d["command_topic"] = baseTopic(bl) + "/my";
    d["payload_press"] = "MY";
    d["device"] = dev;
    String out;serializeJson(d, out);
    mqtt.publish((String("homeassistant/button/") + uid + "_my/config").c_str(), out.c_str(), true);
  }
  // "Pair" button: pair this blind's address (motor must be in pairing mode)
  {
    JsonDocument d;
    d["name"] = String(bl.name) + " Pair";
    d["unique_id"] = uid + "_pair";
    d["command_topic"] = baseTopic(bl) + "/pair";
    d["payload_press"] = "PAIR";
    d["entity_category"] = "config";
    d["device"] = dev;
    String out;serializeJson(d, out);
    mqtt.publish((String("homeassistant/button/") + uid + "_pair/config").c_str(), out.c_str(), true);
  }
}

static void onMessage(char* topic, byte* payload, unsigned int len) {
  String t(topic), p;
  for(unsigned int i = 0; i < len; i++) {p += (char)payload[i];}

  for(size_t i = 0; i < NBLINDS; i++) {
    Blind& bl = BLINDS[i];
    String base = baseTopic(bl);
    if(t == base + "/set") {
      if(p == "OPEN")       {sendCommand(bl, CMD_OPEN, sizeof(CMD_OPEN));publishState(bl, "open");}
      else if(p == "CLOSE") {sendCommand(bl, CMD_CLOSE, sizeof(CMD_CLOSE));publishState(bl, "closed");}
      else if(p == "STOP")  {sendCommand(bl, CMD_STOP, sizeof(CMD_STOP));}
      publishCounter(bl);
    } else if(t == base + "/my") {
      sendFrames(bl, MY_SEQ, sizeof(MY_SEQ) / sizeof(MY_SEQ[0]));
      publishCounter(bl);
    } else if(t == base + "/pair") {
      Serial.printf("[PAIR] %s sending PROG sequence\n", bl.name);
      sendFrames(bl, PAIR_SEQ, sizeof(PAIR_SEQ) / sizeof(PAIR_SEQ[0]));
      publishCounter(bl);
    } else if(t == base + "/counter/set") {
      counterSet(bl, (uint16_t)p.toInt());
      publishCounter(bl);
      Serial.printf("[CTR] %s set to %u\n", bl.name, (unsigned)p.toInt());
    }
  }
}

static void mqttConnect() {
  while(!mqtt.connected()) {
    Serial.print(F("[MQTT] connecting... "));
    if(mqtt.connect(IOWN_MQTT_CLIENT, IOWN_MQTT_USER, IOWN_MQTT_PASS)) {
      Serial.println(F("ok"));
      for(size_t i = 0; i < NBLINDS; i++) {
        Blind& bl = BLINDS[i];
        mqtt.subscribe((baseTopic(bl) + "/set").c_str());
        mqtt.subscribe((baseTopic(bl) + "/my").c_str());
        mqtt.subscribe((baseTopic(bl) + "/pair").c_str());
        mqtt.subscribe((baseTopic(bl) + "/counter/set").c_str());
        mqtt.publish((baseTopic(bl) + "/own/counter").c_str(), "", true); // clear topic of the old two-identity firmware
        publishDiscovery(bl);
        publishCounter(bl);
      }
    } else {
      Serial.print(F("rc="));Serial.print(mqtt.state());Serial.println(F(" retry in 3s"));
      delay(3000);
    }
  }
}

static void halt(const __FlashStringHelper* what) {Serial.println(what);while(true) {delay(1000);}}

void setup() {
  setCpuFrequencyMhz(80); // lowest clock WiFi supports
  Serial.begin(115200);
  delay(300);

  prefs.begin("iown", false);
  uint32_t ctrlBase = ((uint32_t)CTRL_ADDR[0] << 16) | ((uint32_t)CTRL_ADDR[1] << 8) | CTRL_ADDR[2];
  for(size_t i = 0; i < NBLINDS; i++) {
    Blind& bl = BLINDS[i];
    uint32_t a = ctrlBase + i; // blind i uses base address + i, like a Situo's channels
    bl.addr[0] = (uint8_t)(a >> 16);bl.addr[1] = (uint8_t)(a >> 8);bl.addr[2] = (uint8_t)a;
    deriveKey(bl.addr, CTRL_SECRET, bl.key);
    Serial.printf("[CTRL] %s address %06X counter %u\n", bl.name, (unsigned)a, counterGet(bl));
  }

  // self-tests with public test vectors (no installation secrets)
  {
    // MAC port: upstream docs/linklayer.md example (key 0102..16, seq 1234)
    const uint8_t k[16]   = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x10,0x11,0x12,0x13,0x14,0x15,0x16};
    const uint8_t d[17]   = {0x30,0x7e,0x60,0x49,0x1f,0x97,0x6a,0xdf,0x65,0x3d,0xb0,0xed,0x78,0x5e,0x49,0xa2,0x01};
    const uint8_t seq[2]  = {0x12,0x34};
    const uint8_t want[6] = {0x19,0xe8,0x1e,0xc4,0x3d,0x5e};
    uint8_t mac[6];
    create1wMac(seq, k, d, sizeof(d), mac);
    if(memcmp(mac, want, 6) != 0) {halt(F("[SELFTEST] MAC mismatch - halting"));}

    // key derivation + command frame MAC: secret 0102..16 at address ABCDEF, OPEN at 1234,
    // expected value from upstream Iown-ioCrypto.py
    const uint8_t addr[3]  = {0xAB,0xCD,0xEF};
    const uint8_t want2[6] = {0x8A,0xB1,0x60,0xBB,0xD0,0x98};
    uint8_t key[16];
    deriveKey(addr, k, key);
    create1wMac(seq, key, CMD_OPEN, sizeof(CMD_OPEN), mac);
    if(memcmp(mac, want2, 6) != 0) {halt(F("[SELFTEST] key derivation mismatch - halting"));}

    // 0x30 key frame layout (verified against a captured Situo frame): same identity,
    // CRC computed with upstream Iown-ioCrypto.py
    const FrameData kf = {nullptr, 0, true};
    uint8_t f[40];
    size_t n = buildFrame(addr, key, k, kf, 0x00, seq, f);
    if(n != 31 || f[0] != 0xFC || f[29] != 0xFC || f[30] != 0x54) {halt(F("[SELFTEST] 0x30 key frame mismatch - halting"));}
    Serial.println(F("[SELFTEST] MAC, key derivation, 0x30 frame OK"));
  }

  int16_t st = radio.beginFSK(868.95, 38.4, 19.2, 125.0, 14, 60, false);
  if(st) {Serial.print(F("[RADIO] beginFSK: "));Serial.println(st);while(true) {delay(1000);}}
  uint8_t sync[IOHOME_SYNC_WORD_LEN] = {
    (uint8_t)(IOHOME_SYNC_WORD >> 16), (uint8_t)(IOHOME_SYNC_WORD >> 8), (uint8_t)IOHOME_SYNC_WORD};
  radio.setSyncWord(sync, IOHOME_SYNC_WORD_LEN);
  radio.setCRC(false);
  radio.setEncoding(RADIOLIB_ENCODING_NRZ);
  radio.setDataShaping(RADIOLIB_SHAPING_NONE);

  WiFi.mode(WIFI_STA);
  WiFi.begin(IOWN_WIFI_SSID, IOWN_WIFI_PASS);
  Serial.print(F("[WIFI] connecting"));
  while(WiFi.status() != WL_CONNECTED) {delay(400);Serial.print('.');}
  Serial.print(F(" "));Serial.println(WiFi.localIP());

  mqtt.setBufferSize(640); // HA discovery JSON is larger than the 256 default
  mqtt.setServer(IOWN_MQTT_HOST, IOWN_MQTT_PORT);
  mqtt.setCallback(onMessage);
}

void loop() {
  if(WiFi.status() != WL_CONNECTED) {WiFi.reconnect();delay(500);return;}
  if(!mqtt.connected()) {mqttConnect();}
  mqtt.loop();
  delay(10); // let the idle task run instead of spinning
}
