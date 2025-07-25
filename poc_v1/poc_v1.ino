#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_NeoPixel.h>

// —— CONFIG ———————————————————————————————————————
#define WIFI_SSID      ""
#define WIFI_PASS      ""
#define DEVICE_ID      ""
#define BASE_URL       "https://satisfactron.vercel.app"
#define API_ENDPOINT   "/api/votes"

// public test server
#define TEST_URL       "https://example.com/"

// error LED on GPIO1
#define LED_ERR_PIN    1

// —— HARDWARE —————————————————————————————————————
#define PIN_RGBLED_3V3  9
#define NUMPIXELS       5

#define PIN_USW5        13
#define PIN_USW4        14
#define PIN_USW3        21
#define PIN_USW2        47
#define PIN_USW1        48

const uint8_t uswPins[NUMPIXELS] = {
  PIN_USW5, PIN_USW4, PIN_USW3, PIN_USW2, PIN_USW1
};

Adafruit_NeoPixel strip(NUMPIXELS, PIN_RGBLED_3V3, NEO_RGB + NEO_KHZ800);

// —— TIMINGS —————————————————————————————————————
const uint32_t HOLD_MS     = 2000;
const uint32_t DEBOUNCE_MS = 250;
const uint32_t CHECK_MS    = 10000;  // retry test every 10s

// —— STATE ——————————————————————————————————————
bool     active[NUMPIXELS]        = { false };
uint32_t activateTime[NUMPIXELS]  = { 0 };
uint32_t lastCountTime[NUMPIXELS] = { 0 };
bool     wifiPrev                = false;
bool     serverOK                = false;
uint32_t lastCheck               = 0;

void updateIdleColors(uint32_t color) {
  for (uint8_t i = 0; i < NUMPIXELS; i++) {
    if (!active[i]) strip.setPixelColor(i, color);
  }
  strip.show();
}

bool testServer() {
  HTTPClient http;
  http.begin(TEST_URL);
  int code = http.GET();
  http.end();
  return (code > 0 && code < 300);
}

void sendVote(uint8_t vote) {
  if (WiFi.status() != WL_CONNECTED || !serverOK) {
    Serial.printf("❌ Skipping vote %u\n", vote);
    return;
  }
  HTTPClient http;
  String url = String(BASE_URL) + API_ENDPOINT;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  String body = String("{\"deviceId\":\"") + DEVICE_ID +
                String("\",\"voteValue\":") + vote + String("}");
  int httpCode = http.POST(body);
  String payload = http.getString();
  http.end();
  if (httpCode == 201) {
    Serial.printf("✅ Vote %u OK\n", vote);
  } else {
    Serial.printf("❌ Vote %u failed %d %s\n", vote, httpCode, payload.c_str());
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_ERR_PIN, OUTPUT);
  digitalWrite(LED_ERR_PIN, LOW);
  strip.begin();
  updateIdleColors(strip.Color(255, 0, 0));  // red
  for (uint8_t i = 0; i < NUMPIXELS; i++) {
    pinMode(uswPins[i], INPUT_PULLUP);
  }
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("WiFi.begin(%s)\n", WIFI_SSID);
}

void loop() {
  uint32_t now = millis();
  bool wifiOK = (WiFi.status() == WL_CONNECTED);

  if (wifiOK && !wifiPrev) {
    Serial.printf("✅ WiFi up, IP %s\n", WiFi.localIP().toString().c_str());
    lastCheck = 0;  // force immediate server test
  }
  if (!wifiOK && wifiPrev) {
    Serial.println("❌ WiFi down");
    serverOK = false;
    updateIdleColors(strip.Color(255, 0, 0));
  }
  wifiPrev = wifiOK;

  // test public server every CHECK_MS once WiFi is up
  if (wifiOK && !serverOK && now - lastCheck >= CHECK_MS) {
    lastCheck = now;
    Serial.println("… testing server reachability");
    if (testServer()) {
      Serial.println("✅ Server reachable");
      serverOK = true;
      digitalWrite(LED_ERR_PIN, LOW);
      updateIdleColors(strip.Color(0, 0, 255));  // blue
    } else {
      Serial.println("❌ Server not reachable");
      serverOK = false;
      digitalWrite(LED_ERR_PIN, HIGH);
      updateIdleColors(strip.Color(255, 0, 0));  // stay red
    }
  }

  // button scan & vote
  for (uint8_t i = 0; i < NUMPIXELS; i++) {
    bool pressed = (digitalRead(uswPins[i]) == LOW);
    uint8_t vote = NUMPIXELS - i;
    if (pressed && !active[i] && now - lastCountTime[i] >= DEBOUNCE_MS) {
      active[i] = true;
      activateTime[i] = now;
      lastCountTime[i] = now;
      strip.setPixelColor(i, strip.Color(0, 255, 0));
      strip.show();
      Serial.printf("→ Button %u pressed\n", vote);
      sendVote(vote);
    }
    if (active[i] && !pressed && now - activateTime[i] >= HOLD_MS) {
      active[i] = false;
      uint32_t col = (wifiOK && serverOK)
                   ? strip.Color(0, 0, 255)
                   : strip.Color(255, 0, 0);
      strip.setPixelColor(i, col);
      strip.show();
      Serial.printf("↱ Button %u released\n", vote);
    }
  }

  // reconnect WiFi if lost
  if (!wifiOK && now % 10000 < 50) {
    Serial.println("Reconnecting WiFi…");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}
