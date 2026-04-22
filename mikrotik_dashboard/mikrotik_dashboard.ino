// Dashboard en tiempo real de un router Mikrotik hAP ax3 sobre ESP-32 +
// pantalla TFT ST7735 128x160. Consulta la REST API de RouterOS v7 por HTTP
// y rota automaticamente entre tres paginas: Sistema, Trafico WAN y Clientes
// Wi-Fi. Cablea igual que tft_test/tft_test.ino.

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include "secrets.h"

// ---- Pines TFT (identicos a tft_test) ---------------------------------------
#define TFT_CS 15
#define TFT_DC 0
#define TFT_RST 2
// SCK -> GPIO18 (SPI HW)
// SDA -> GPIO23 (SPI HW, MOSI)

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// ---- Parametros de la UI ----------------------------------------------------
static const uint32_t POLL_MS = 2000;  // una peticion REST cada 2 s
static const uint32_t PAGE_MS = 6000;  // cambio de pagina cada 6 s
static const uint16_t SCREEN_W = 160;  // rotation 1
static const uint16_t SCREEN_H = 128;
static const uint16_t HEADER_H = 14;

static const uint8_t SPARK_LEN = 80;
static const uint8_t MAX_WIFI_CLIENTS = 8;
static const uint8_t WIFI_LINES = 6;

// ---- Estado global ----------------------------------------------------------
struct SystemInfo {
  int cpu = 0;
  uint32_t freeMemKB = 0;
  uint32_t totalMemKB = 0;
  char uptime[24] = "";
  char boardName[24] = "Mikrotik";
  bool valid = false;
};

struct TrafficInfo {
  uint64_t rxBytes = 0;
  uint64_t txBytes = 0;
  uint64_t rxBps = 0;
  uint64_t txBps = 0;
  uint32_t lastSampleMs = 0;
  bool primed = false;
  bool valid = false;
  float sparkRx[SPARK_LEN] = {0};
  float sparkTx[SPARK_LEN] = {0};
  uint8_t sparkHead = 0;
};

struct WifiClient {
  char mac[18];
  char iface[16];
  int signal;
};

struct WifiInfo {
  WifiClient clients[MAX_WIFI_CLIENTS];
  uint8_t count = 0;
  bool valid = false;
};

SystemInfo gSys;
TrafficInfo gTraf;
WifiInfo gWifi;

uint8_t currentPage = 0;
uint32_t lastPoll = 0;
uint32_t lastPageSwitch = 0;
uint8_t pollCursor = 0;  // 0=sys, 1=traffic, 2=wifi
bool lastRequestOk = true;

// ---- Helpers de dibujo ------------------------------------------------------
static void drawStatusDot(bool ok) {
  tft.fillCircle(SCREEN_W - 7, 7, 3, ok ? ST77XX_GREEN : ST77XX_RED);
}

static void drawHeader() {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, ST77XX_BLUE);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(3, 3);
  tft.print(gSys.boardName);
  drawStatusDot(lastRequestOk);
}

static void drawBar(int16_t x, int16_t y, int16_t w, int16_t h, int pct,
                    uint16_t color) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  tft.drawRect(x, y, w, h, ST77XX_WHITE);
  tft.fillRect(x + 1, y + 1, w - 2, h - 2, ST77XX_BLACK);
  int fill = (w - 2) * pct / 100;
  tft.fillRect(x + 1, y + 1, fill, h - 2, color);
}

static void drawSparkline(int16_t x, int16_t y, int16_t w, int16_t h,
                          const float* buf, uint8_t head, uint16_t color,
                          float maxVal) {
  tft.drawRect(x, y, w, h, ST77XX_DARKGREY);
  if (maxVal <= 0) return;
  int16_t prevY = -1;
  for (uint8_t i = 0; i < SPARK_LEN && i < (w - 2); i++) {
    uint8_t idx = (head + i) % SPARK_LEN;
    float v = buf[idx];
    int16_t py = y + h - 2 - (int16_t)((h - 3) * (v / maxVal));
    int16_t px = x + 1 + i;
    if (prevY >= 0) tft.drawLine(px - 1, prevY, px, py, color);
    prevY = py;
  }
}

static void formatBps(uint64_t bps, char* out, size_t n) {
  double b = (double)bps;
  if (b >= 1e9) snprintf(out, n, "%.2f Gbps", b / 1e9);
  else if (b >= 1e6) snprintf(out, n, "%.2f Mbps", b / 1e6);
  else if (b >= 1e3) snprintf(out, n, "%.1f kbps", b / 1e3);
  else snprintf(out, n, "%u bps", (unsigned)bps);
}

// ---- REST HTTP --------------------------------------------------------------
static bool fetchJson(const char* path, JsonDocument& doc) {
  WiFiClient client;
  HTTPClient http;
  char url[128];
  snprintf(url, sizeof(url), "http://%s/rest%s", ROUTER_HOST, path);
  http.setTimeout(2500);
  http.setReuse(false);
  if (!http.begin(client, url)) return false;
  http.setAuthorization(ROUTER_USER, ROUTER_PASS);
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    DeserializationError err = deserializeJson(doc, http.getStream());
    ok = !err;
    if (err) {
      Serial.printf("JSON err %s en %s\n", err.c_str(), path);
    }
  } else {
    Serial.printf("HTTP %d en %s\n", code, path);
  }
  http.end();
  return ok;
}

// ---- Fetchers ---------------------------------------------------------------
static void pollSystem() {
  StaticJsonDocument<512> doc;
  if (!fetchJson("/system/resource", doc)) {
    gSys.valid = false;
    lastRequestOk = false;
    return;
  }
  gSys.cpu = doc["cpu-load"] | 0;
  uint64_t freeB = doc["free-memory"] | 0ULL;
  uint64_t totalB = doc["total-memory"] | 0ULL;
  gSys.freeMemKB = (uint32_t)(freeB / 1024ULL);
  gSys.totalMemKB = (uint32_t)(totalB / 1024ULL);
  const char* up = doc["uptime"] | "";
  strncpy(gSys.uptime, up, sizeof(gSys.uptime) - 1);
  gSys.uptime[sizeof(gSys.uptime) - 1] = '\0';
  const char* bn = doc["board-name"] | "Mikrotik";
  strncpy(gSys.boardName, bn, sizeof(gSys.boardName) - 1);
  gSys.boardName[sizeof(gSys.boardName) - 1] = '\0';
  gSys.valid = true;
  lastRequestOk = true;
}

static void pushSparkle(float* buf, uint8_t& head, float v) {
  buf[head] = v;
  head = (head + 1) % SPARK_LEN;
}

static void pollTraffic() {
  StaticJsonDocument<1024> doc;
  char path[64];
  snprintf(path, sizeof(path), "/interface/%s", WAN_IFACE);
  if (!fetchJson(path, doc)) {
    gTraf.valid = false;
    lastRequestOk = false;
    return;
  }
  // RouterOS devuelve array o objeto segun endpoint; aqui es objeto.
  JsonObject obj = doc.is<JsonArray>() ? doc.as<JsonArray>()[0].as<JsonObject>()
                                       : doc.as<JsonObject>();
  uint64_t rx = (uint64_t)(obj["rx-byte"] | 0ULL);
  uint64_t tx = (uint64_t)(obj["tx-byte"] | 0ULL);
  uint32_t now = millis();
  if (gTraf.primed && now > gTraf.lastSampleMs) {
    uint32_t dt = now - gTraf.lastSampleMs;
    uint64_t drx = rx >= gTraf.rxBytes ? rx - gTraf.rxBytes : 0;
    uint64_t dtx = tx >= gTraf.txBytes ? tx - gTraf.txBytes : 0;
    gTraf.rxBps = (drx * 8000ULL) / dt;
    gTraf.txBps = (dtx * 8000ULL) / dt;
  } else {
    gTraf.rxBps = 0;
    gTraf.txBps = 0;
  }
  gTraf.rxBytes = rx;
  gTraf.txBytes = tx;
  gTraf.lastSampleMs = now;
  gTraf.primed = true;
  pushSparkle(gTraf.sparkRx, gTraf.sparkHead, (float)gTraf.rxBps);
  // sparkHead ya incrementado por pushSparkle; para Tx usamos la posicion
  // previa para mantenerlos alineados.
  uint8_t txIdx = (gTraf.sparkHead + SPARK_LEN - 1) % SPARK_LEN;
  gTraf.sparkTx[txIdx] = (float)gTraf.txBps;
  gTraf.valid = true;
  lastRequestOk = true;
}

static void pollWifiClients() {
  DynamicJsonDocument doc(4096);
  if (!fetchJson("/interface/wifi/registration-table", doc)) {
    // Fallback al paquete legacy "wireless"
    if (!fetchJson("/interface/wireless/registration-table", doc)) {
      gWifi.valid = false;
      lastRequestOk = false;
      return;
    }
  }
  JsonArray arr = doc.as<JsonArray>();
  uint8_t n = 0;
  for (JsonObject entry : arr) {
    if (n >= MAX_WIFI_CLIENTS) break;
    const char* mac = entry["mac-address"] | "";
    const char* iface = entry["interface"] | "";
    int sig = entry["signal"] | entry["signal-strength"] | 0;
    strncpy(gWifi.clients[n].mac, mac, sizeof(gWifi.clients[n].mac) - 1);
    gWifi.clients[n].mac[sizeof(gWifi.clients[n].mac) - 1] = '\0';
    strncpy(gWifi.clients[n].iface, iface, sizeof(gWifi.clients[n].iface) - 1);
    gWifi.clients[n].iface[sizeof(gWifi.clients[n].iface) - 1] = '\0';
    gWifi.clients[n].signal = sig;
    n++;
  }
  gWifi.count = n;
  gWifi.valid = true;
  lastRequestOk = true;
}

// ---- Paginas ----------------------------------------------------------------
static void drawPageSystem() {
  tft.fillRect(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);

  // CPU
  tft.setCursor(5, HEADER_H + 6);
  tft.print("CPU");
  char buf[24];
  snprintf(buf, sizeof(buf), "%3d%%", gSys.cpu);
  tft.setCursor(SCREEN_W - 30, HEADER_H + 6);
  tft.print(buf);
  uint16_t cpuColor = gSys.cpu > 80 ? ST77XX_RED
                    : gSys.cpu > 50 ? ST77XX_YELLOW
                                    : ST77XX_GREEN;
  drawBar(5, HEADER_H + 18, SCREEN_W - 10, 10, gSys.cpu, cpuColor);

  // RAM
  uint32_t usedKB = gSys.totalMemKB > gSys.freeMemKB
                        ? gSys.totalMemKB - gSys.freeMemKB
                        : 0;
  int ramPct = gSys.totalMemKB ? (int)((100ULL * usedKB) / gSys.totalMemKB) : 0;
  tft.setCursor(5, HEADER_H + 38);
  tft.print("RAM");
  snprintf(buf, sizeof(buf), "%lu/%lu KB",
           (unsigned long)usedKB, (unsigned long)gSys.totalMemKB);
  tft.setCursor(SCREEN_W - 6 * (int)strlen(buf) - 4, HEADER_H + 38);
  tft.print(buf);
  drawBar(5, HEADER_H + 50, SCREEN_W - 10, 10, ramPct, ST77XX_CYAN);

  // Uptime
  tft.setCursor(5, HEADER_H + 72);
  tft.print("Uptime:");
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(5, HEADER_H + 84);
  tft.setTextSize(2);
  tft.print(gSys.uptime);
  tft.setTextSize(1);
}

static void drawPageTraffic() {
  tft.fillRect(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(3, HEADER_H + 4);
  tft.print("WAN ");
  tft.print(WAN_IFACE);

  char b[24];
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(3, HEADER_H + 16);
  tft.print("Rx ");
  formatBps(gTraf.rxBps, b, sizeof(b));
  tft.print(b);

  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(3, HEADER_H + 28);
  tft.print("Tx ");
  formatBps(gTraf.txBps, b, sizeof(b));
  tft.print(b);

  // Escala autoajustada al max de ambos buffers
  float maxV = 1.0f;
  for (uint8_t i = 0; i < SPARK_LEN; i++) {
    if (gTraf.sparkRx[i] > maxV) maxV = gTraf.sparkRx[i];
    if (gTraf.sparkTx[i] > maxV) maxV = gTraf.sparkTx[i];
  }

  int16_t gx = 3;
  int16_t gy = HEADER_H + 44;
  int16_t gw = SCREEN_W - 6;
  int16_t gh = SCREEN_H - gy - 14;
  drawSparkline(gx, gy, gw, gh, gTraf.sparkRx, gTraf.sparkHead, ST77XX_CYAN,
                maxV);
  drawSparkline(gx, gy, gw, gh, gTraf.sparkTx, gTraf.sparkHead, ST77XX_YELLOW,
                maxV);

  tft.setTextColor(ST77XX_DARKGREY);
  tft.setCursor(3, SCREEN_H - 10);
  formatBps((uint64_t)maxV, b, sizeof(b));
  tft.print("max ");
  tft.print(b);
}

static void drawPageWifi() {
  tft.fillRect(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(3, HEADER_H + 4);
  tft.print("Clientes Wi-Fi: ");
  tft.print(gWifi.count);

  int16_t y = HEADER_H + 18;
  uint8_t shown = 0;
  for (uint8_t i = 0; i < gWifi.count && shown < WIFI_LINES; i++, shown++) {
    const WifiClient& c = gWifi.clients[i];
    tft.setTextColor(c.signal > -60   ? ST77XX_GREEN
                     : c.signal > -75 ? ST77XX_YELLOW
                                      : ST77XX_RED);
    tft.setCursor(3, y);
    // MAC completa no cabe, mostramos ultimos 8 caracteres (XX:YY:ZZ)
    const char* macTail = c.mac;
    size_t ml = strlen(c.mac);
    if (ml > 8) macTail = c.mac + ml - 8;
    tft.print(macTail);
    tft.setCursor(3 + 60, y);
    tft.print(c.signal);
    tft.print("dBm");
    tft.setCursor(3 + 110, y);
    tft.print(c.iface);
    y += 12;
  }
  if (gWifi.count > WIFI_LINES) {
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(3, y);
    tft.print("+");
    tft.print(gWifi.count - WIFI_LINES);
    tft.print(" mas...");
  }
  if (gWifi.count == 0) {
    tft.setTextColor(ST77XX_DARKGREY);
    tft.setCursor(3, y);
    tft.print("(sin clientes)");
  }
}

static void redraw() {
  drawHeader();
  switch (currentPage) {
    case 0: drawPageSystem(); break;
    case 1: drawPageTraffic(); break;
    case 2: drawPageWifi(); break;
  }
}

// ---- Wi-Fi ------------------------------------------------------------------
static void connectWifi() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 10);
  tft.print("Conectando a");
  tft.setCursor(5, 25);
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(5 + dots * 6, 45);
    tft.print(".");
    dots = (dots + 1) % 20;
    if (dots == 0) tft.fillRect(0, 45, SCREEN_W, 10, ST77XX_BLACK);
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  tft.fillRect(0, 45, SCREEN_W, 40, ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(5, 55);
  tft.print("OK ");
  tft.print(WiFi.localIP());
  delay(600);
}

// ---- Arduino setup/loop -----------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Mikrotik dashboard init");

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  connectWifi();

  // Primer pase para rellenar datos antes del primer redraw
  pollSystem();
  pollTraffic();
  pollWifiClients();
  lastPageSwitch = millis();
  lastPoll = millis();
  redraw();
}

void loop() {
  uint32_t now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    lastRequestOk = false;
    drawHeader();
    WiFi.reconnect();
    delay(500);
    return;
  }

  if (now - lastPoll >= POLL_MS) {
    lastPoll = now;
    switch (pollCursor) {
      case 0: pollSystem(); break;
      case 1: pollTraffic(); break;
      case 2: pollWifiClients(); break;
    }
    pollCursor = (pollCursor + 1) % 3;
    redraw();
  }

  if (now - lastPageSwitch >= PAGE_MS) {
    lastPageSwitch = now;
    currentPage = (currentPage + 1) % 3;
    redraw();
  }
}
