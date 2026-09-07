// main.cpp — BLE notification-throughput measurement client (issue #71, phase 0e).
//
// Bench tool. Flash to a SPARE ESP32, point it at a HiveInside node running the
// throughput spike, and read the answer off the serial console.
//
// The question it answers: the audio feature in issue #71 needs a recording to
// travel from the node to the hub as GATT notifications. The only throughput
// figure we have today is the OTA relay's ~1-1.5 kB/s, and that path measures
// something else entirely — writes WITH RESPONSE (two connection intervals per
// chunk), each one blocking on an RRAM write inside the ATT callback, with a TLS
// download running concurrently on the same radio. An audio stream has no flash
// writes and no round trip per packet. This sketch measures the thing that
// actually matters.
//
// Deliberately a separate PlatformIO project from firmware/: nothing here should
// ever be linkable into a hub image.
//
// Read the result against these thresholds:
//   >= 8 kB/s  stream 16 kHz ADPCM in real time
//   4-8 kB/s   stream 8 kHz ADPCM, or buffer the clip at 16 kHz
//   <  4 kB/s  buffer the whole clip on the node; streaming is not viable

#include <Arduino.h>
#include <NimBLEDevice.h>

#if TP_WIFI_LOAD
#include <HTTPClient.h>
#include <WiFi.h>
#endif

namespace {

const char* BASE = "-7a1c-4b9e-9a2f-1d6e0b9c1a01";

String uuid(const char* prefix) { return String(prefix) + BASE; }

constexpr uint8_t OP_START = 0x01;
constexpr uint8_t OP_STOP = 0x02;

// ── Shared counters ────────────────────────────────────────────────────────
// The subscribe callback runs on the NimBLE host task while loop() reads these,
// so every access is under a spinlock — the same discipline beehive_gatt.cpp
// uses for its notification capture.
struct Counters {
  uint32_t packets = 0;
  uint32_t bytes = 0;
  uint32_t gaps = 0;      // notifications that did not follow the previous seq
  uint32_t missing = 0;   // how many packets those gaps account for
  uint32_t nextSeq = 0;
  bool haveSeq = false;
  uint32_t firstMs = 0;
  uint32_t lastMs = 0;
};
Counters g_rx;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

NimBLEClient* g_client = nullptr;
NimBLERemoteCharacteristic* g_ctrl = nullptr;
NimBLERemoteCharacteristic* g_data = nullptr;
NimBLERemoteCharacteristic* g_status = nullptr;

String g_foundMac;
uint8_t g_foundType = BLE_ADDR_PUBLIC;
bool g_found = false;

void onNotify(NimBLERemoteCharacteristic* /*chr*/, uint8_t* data, size_t len,
              bool /*isNotify*/) {
  const uint32_t now = millis();
  // The node puts a 32-bit sequence number at the head of every notification.
  // Comparing it against what we expected is what separates "the link is slow"
  // from "the link is dropping packets" — two very different conclusions.
  uint32_t seq = 0;
  if (len >= 4) {
    seq = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
          ((uint32_t)data[3] << 24);
  }

  portENTER_CRITICAL_ISR(&g_mux);
  if (g_rx.packets == 0) g_rx.firstMs = now;
  g_rx.lastMs = now;
  g_rx.packets++;
  g_rx.bytes += len;
  if (len >= 4) {
    if (g_rx.haveSeq && seq != g_rx.nextSeq) {
      g_rx.gaps++;
      if (seq > g_rx.nextSeq) g_rx.missing += (seq - g_rx.nextSeq);
    }
    g_rx.nextSeq = seq + 1;
    g_rx.haveSeq = true;
  }
  portEXIT_CRITICAL_ISR(&g_mux);
}

Counters snapshot() {
  Counters c;
  portENTER_CRITICAL(&g_mux);
  c = g_rx;
  portEXIT_CRITICAL(&g_mux);
  return c;
}

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    if (g_found) return;
    std::string name = dev->getName();
    if (name.rfind(TP_NAME_PREFIX, 0) != 0) return;
    g_foundMac = String(dev->getAddress().toString().c_str());
    g_foundType = dev->getAddress().getType();
    g_found = true;
    Serial.printf("[TP] found %s at %s (addr type %u)\n", name.c_str(),
                  g_foundMac.c_str(), (unsigned)g_foundType);
  }
};
ScanCallbacks g_scanCallbacks;

bool locate() {
  if (String(TP_TARGET_MAC).length() > 0) {
    g_foundMac = TP_TARGET_MAC;
    g_foundMac.toUpperCase();
    g_found = true;
    Serial.printf("[TP] using configured address %s\n", g_foundMac.c_str());
    return true;
  }
  Serial.printf("[TP] scanning for a device named %s* ...\n", TP_NAME_PREFIX);
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&g_scanCallbacks, false);
  scan->setActiveScan(true);  // the name lives in the scan response
  scan->getResults(8000, false);
  scan->clearResults();
  if (!g_found) Serial.println("[TP] no matching device found");
  return g_found;
}

bool connectAndDiscover() {
  g_client = NimBLEDevice::createClient();
  g_client->setConnectTimeout(10000);

  // Try the address type the scan reported first; fall back to the other, the
  // same two-pass approach gatt_ota.cpp uses for a seeded MAC.
  const uint8_t types[2] = {g_foundType,
                            g_foundType == BLE_ADDR_PUBLIC ? (uint8_t)BLE_ADDR_RANDOM
                                                           : (uint8_t)BLE_ADDR_PUBLIC};
  bool connected = false;
  for (int i = 0; i < 2 && !connected; i++) {
    NimBLEAddress addr(std::string(g_foundMac.c_str()), types[i]);
    Serial.printf("[TP] connecting to %s (addr type %u) ...\n", g_foundMac.c_str(),
                  (unsigned)types[i]);
    connected = g_client->connect(addr);
  }
  if (!connected) {
    Serial.println("[TP] connect failed");
    return false;
  }

  Serial.printf("[TP] connected; MTU %u\n", (unsigned)g_client->getMTU());

  NimBLERemoteService* svc = g_client->getService(NimBLEUUID(uuid("8e8b00f0").c_str()));
  if (!svc) {
    Serial.println("[TP] throughput service not found — is the node running an "
                   "image built with throughput-spike.conf?");
    return false;
  }
  g_ctrl = svc->getCharacteristic(NimBLEUUID(uuid("8e8b00f1").c_str()));
  g_data = svc->getCharacteristic(NimBLEUUID(uuid("8e8b00f2").c_str()));
  g_status = svc->getCharacteristic(NimBLEUUID(uuid("8e8b00f3").c_str()));
  if (!g_ctrl || !g_data) {
    Serial.println("[TP] CTRL/DATA characteristics missing");
    return false;
  }
  if (!g_data->subscribe(true, onNotify)) {
    Serial.println("[TP] subscribe failed");
    return false;
  }
  return true;
}

void printReport() {
  Counters c = snapshot();
  const uint32_t elapsedMs = (c.lastMs > c.firstMs) ? (c.lastMs - c.firstMs) : 0;
  const uint32_t rx_bps = elapsedMs ? (uint32_t)((uint64_t)c.bytes * 1000ULL / elapsedMs) : 0;

  Serial.println();
  Serial.println("---- received by this client ----------------------------");
  Serial.printf("  bytes      : %u\n", (unsigned)c.bytes);
  Serial.printf("  packets    : %u\n", (unsigned)c.packets);
  Serial.printf("  elapsed    : %u ms\n", (unsigned)elapsedMs);
  Serial.printf("  throughput : %u B/s (%u.%u kB/s)\n", (unsigned)rx_bps,
                (unsigned)(rx_bps / 1024), (unsigned)((rx_bps % 1024) * 10 / 1024));
  if (c.gaps) {
    Serial.printf("  LOSS       : %u gap(s), ~%u packet(s) missing\n",
                  (unsigned)c.gaps, (unsigned)c.missing);
  }

  if (g_status && g_client && g_client->isConnected()) {
    std::string s = g_status->readValue();
    if (s.size() >= 16) {
      const uint8_t* b = (const uint8_t*)s.data();
      uint32_t sent = (uint32_t)b[1] | ((uint32_t)b[2] << 8) | ((uint32_t)b[3] << 16) |
                      ((uint32_t)b[4] << 24);
      uint32_t ms = (uint32_t)b[5] | ((uint32_t)b[6] << 8) | ((uint32_t)b[7] << 16) |
                    ((uint32_t)b[8] << 24);
      uint32_t notifs = (uint32_t)b[9] | ((uint32_t)b[10] << 8) |
                        ((uint32_t)b[11] << 16) | ((uint32_t)b[12] << 24);
      uint32_t dev_bps = ms ? (uint32_t)((uint64_t)sent * 1000ULL / ms) : 0;
      Serial.println("---- reported by the node --------------------------------");
      Serial.printf("  state      : 0x%02X\n", (unsigned)b[0]);
      Serial.printf("  bytes      : %u (%u notifications of %u B)\n", (unsigned)sent,
                    (unsigned)notifs, (unsigned)b[13]);
      Serial.printf("  elapsed    : %u ms\n", (unsigned)ms);
      Serial.printf("  throughput : %u B/s\n", (unsigned)dev_bps);
      Serial.printf("  MTU        : %u\n",
                    (unsigned)((uint16_t)b[14] | ((uint16_t)b[15] << 8)));
      if (sent > c.bytes) {
        Serial.printf("  NOTE: %u B handed to the node's controller never arrived here\n",
                      (unsigned)(sent - c.bytes));
      }
    }
  }

  Serial.println("---- verdict for issue #71 -------------------------------");
  if (rx_bps >= 8192) {
    Serial.println("  >= 8 kB/s: real-time streaming of 16 kHz ADPCM is viable.");
  } else if (rx_bps >= 4096) {
    Serial.println("  4-8 kB/s: stream 8 kHz ADPCM, or buffer the clip at 16 kHz.");
  } else {
    Serial.println("  < 4 kB/s: buffer the whole clip on the node; streaming is out.");
  }
#if TP_WIFI_LOAD
  Serial.println("  (measured WITH a concurrent WiFi download — the coexistence case)");
#else
  Serial.println("  (measured with WiFi idle; re-run with -DTP_WIFI_LOAD=1 for the");
  Serial.println("   coexistence case, which is what a streaming design would face)");
#endif
  Serial.println("----------------------------------------------------------");
}

#if TP_WIFI_LOAD
// Keep the WiFi radio genuinely busy for the duration of the measurement. The
// point is 2.4 GHz contention against BLE, not the HTTP semantics, so the body
// is read and thrown away.
void wifiLoadTask(void*) {
  for (;;) {
    HTTPClient http;
    http.begin(TP_LOAD_URL);
    int code = http.GET();
    if (code == HTTP_CODE_OK) {
      WiFiClient* stream = http.getStreamPtr();
      uint8_t buf[1024];
      while (http.connected() && stream->available()) {
        stream->readBytes(buf, sizeof(buf));
        vTaskDelay(1);
      }
    }
    http.end();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
#endif

bool g_done = false;
uint32_t g_startedMs = 0;
uint32_t g_lastTickMs = 0;
uint32_t g_lastBytes = 0;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("[TP] HiveInside BLE notification throughput client");
  Serial.printf("[TP] duration=%us payload=%uB interval=%u units\n",
                (unsigned)TP_DURATION_S, (unsigned)TP_PAYLOAD,
                (unsigned)TP_INTERVAL_UNITS);

#if TP_WIFI_LOAD
  Serial.printf("[TP] bringing WiFi up for the coexistence measurement\n");
  WiFi.mode(WIFI_STA);
  WiFi.begin(TP_WIFI_SSID, TP_WIFI_PASS);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(250);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[TP] WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    xTaskCreate(wifiLoadTask, "tp-load", 4096, nullptr, 1, nullptr);
  } else {
    Serial.println("[TP] WiFi failed — the run will NOT be a coexistence measurement");
  }
#endif

  NimBLEDevice::init("tp-client");
  NimBLEDevice::setMTU(247);

  if (!locate() || !connectAndDiscover()) {
    Serial.println("[TP] setup failed; halting");
    g_done = true;
    return;
  }

  // The connection interval is requested by the NODE (throughput.c acts on the
  // fourth CTRL byte), not here: that is the direction a real audio session
  // would ask in, and it keeps this sketch off NimBLE's connection-parameter API.
  uint8_t cmd[4] = {OP_START, (uint8_t)TP_DURATION_S, (uint8_t)TP_PAYLOAD,
                    (uint8_t)TP_INTERVAL_UNITS};
  if (!g_ctrl->writeValue(cmd, sizeof(cmd), true)) {
    Serial.println("[TP] START write failed");
    g_done = true;
    return;
  }
  Serial.println("[TP] running ...");
  g_startedMs = millis();
  g_lastTickMs = g_startedMs;
}

void loop() {
  if (g_done) {
    delay(1000);
    return;
  }

  const uint32_t now = millis();
  if (now - g_lastTickMs >= 1000) {
    g_lastTickMs = now;
    Counters c = snapshot();
    const uint32_t delta = c.bytes - g_lastBytes;
    g_lastBytes = c.bytes;
    Serial.printf("  %6u B/s   total %8u B\n", (unsigned)delta, (unsigned)c.bytes);
  }

  // Two seconds of grace past the requested duration so the last notifications
  // and the STATUS read are not cut off.
  if (now - g_startedMs > ((uint32_t)TP_DURATION_S + 2) * 1000UL) {
    if (g_ctrl) {
      uint8_t stop = OP_STOP;
      g_ctrl->writeValue(&stop, 1, true);
    }
    printReport();
    if (g_client && g_client->isConnected()) g_client->disconnect();
    g_done = true;
  }

  delay(10);
}
