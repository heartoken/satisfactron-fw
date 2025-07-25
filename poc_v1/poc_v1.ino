#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_NeoPixel.h>

// —— CONFIG ———————————————————————————————————————
#define WIFI_SSID      ""
#define WIFI_PASS      ""
#define DEVICE_ID      ""
#define BASE_URL       "https://satisfactron.vercel.app"
#define API_ENDPOINT   "/api/votes"
#define TEST_URL       "https://satisfactron.vercel.app"
#define LED_ERR_PIN    1

// —— HARDWARE —————————————————————————————————————
#define PIN_RGBLED_3V3  9
#define NUMPIXELS       5

const uint8_t uswPins[NUMPIXELS] = {13, 14, 21, 47, 48}; // USW5-1

Adafruit_NeoPixel strip(NUMPIXELS, PIN_RGBLED_3V3, NEO_RGB + NEO_KHZ800);

// —— TIMINGS —————————————————————————————————————
const uint32_t HOLD_EXTEND_MS = 2000;  // extend hold time per vote
const uint32_t DEBOUNCE_MS    = 250;
const uint32_t CHECK_MS       = 10000;

// —— VOTE QUEUE ——————————————————————————————————
#define QUEUE_SIZE 20
struct Vote {
  uint8_t value;
  uint32_t timestamp;
};
Vote voteQueue[QUEUE_SIZE];
volatile uint8_t queueHead = 0;
volatile uint8_t queueTail = 0;

// —— STATE ——————————————————————————————————————
bool     slotActive[NUMPIXELS]        = {false};
uint32_t slotEndTime[NUMPIXELS]       = {0};     // when LED should return to idle
uint32_t lastVoteTime[NUMPIXELS]      = {0};     // last time this slot voted
bool     wifiPrev                     = false;
volatile bool serverOK                = false;
uint32_t lastCheck                    = 0;

TaskHandle_t httpTaskHandle = NULL;

void queueVote(uint8_t vote) {
  uint8_t nextTail = (queueTail + 1) % QUEUE_SIZE;
  if (nextTail != queueHead) {
    voteQueue[queueTail] = {vote, millis()};
    queueTail = nextTail;
    Serial.printf("📥 Queued vote %u\n", vote);
  } else {
    Serial.printf("❌ Queue full, dropping vote %u\n", vote);
  }
}

void updateIdleColors(uint32_t color) {
  for (uint8_t i = 0; i < NUMPIXELS; i++) {
    if (!slotActive[i]) strip.setPixelColor(i, color);
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
    Serial.printf("✅ Vote %u sent OK\n", vote);
  } else {
    Serial.printf("❌ Vote %u failed %d %s\n", vote, httpCode, payload.c_str());
  }
}

void httpTask(void *parameter) {
  while (true) {
    if (queueHead != queueTail && WiFi.status() == WL_CONNECTED && serverOK) {
      Vote vote = voteQueue[queueHead];
      queueHead = (queueHead + 1) % QUEUE_SIZE;
      sendVote(vote.value);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_ERR_PIN, OUTPUT);
  digitalWrite(LED_ERR_PIN, LOW);
  
  strip.begin();
  updateIdleColors(strip.Color(255, 0, 0));
  
  for (uint8_t i = 0; i < NUMPIXELS; i++) {
    pinMode(uswPins[i], INPUT_PULLUP);
  }
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("WiFi.begin(%s)\n", WIFI_SSID);
  
  // Create HTTP task on Core 0
  xTaskCreatePinnedToCore(
    httpTask,         // Function
    "HTTPTask",       // Name
    16384,            // Stack size
    NULL,            // Parameter
    1,               // Priority
    &httpTaskHandle, // Task handle
    0                // Core 0
  );
}

void loop() {
  uint32_t now = millis();
  bool wifiOK = (WiFi.status() == WL_CONNECTED);

  // WiFi state changes
  if (wifiOK && !wifiPrev) {
    Serial.printf("✅ WiFi up, IP %s\n", WiFi.localIP().toString().c_str());
    lastCheck = 0;
  }
  if (!wifiOK && wifiPrev) {
    Serial.println("❌ WiFi down");
    serverOK = false;
    updateIdleColors(strip.Color(255, 0, 0));
  }
  wifiPrev = wifiOK;

  // Server connectivity check
  if (wifiOK && !serverOK && now - lastCheck >= CHECK_MS) {
    lastCheck = now;
    Serial.println("… testing server reachability");
    if (testServer()) {
      Serial.println("✅ Server reachable");
      serverOK = true;
      digitalWrite(LED_ERR_PIN, LOW);
      updateIdleColors(strip.Color(0, 0, 255));
    } else {
      Serial.println("❌ Server not reachable");
      digitalWrite(LED_ERR_PIN, HIGH);
      updateIdleColors(strip.Color(255, 0, 0));
    }
  }

  // Button scanning and vote processing
  for (uint8_t i = 0; i < NUMPIXELS; i++) {
    bool pressed = (digitalRead(uswPins[i]) == LOW);
    uint8_t vote = NUMPIXELS - i;
    
    // Handle new vote (respects debounce)
    if (pressed && now - lastVoteTime[i] >= DEBOUNCE_MS) {
      lastVoteTime[i] = now;
      slotEndTime[i] = now + HOLD_EXTEND_MS;  // extend or set hold time
      
      if (!slotActive[i]) {
        slotActive[i] = true;
        Serial.printf("→ Slot %u activated\n", vote);
      }
      
      // Set LED to green and queue vote
      strip.setPixelColor(i, strip.Color(0, 255, 0));
      strip.show();
      Serial.printf("🗳️ Vote %u registered\n", vote);
      queueVote(vote);
    }
    
    // Handle slot deactivation when hold time expires
    if (slotActive[i] && now >= slotEndTime[i]) {
      slotActive[i] = false;
      uint32_t col = (wifiOK && serverOK) ? strip.Color(0, 0, 255) : strip.Color(255, 0, 0);
      strip.setPixelColor(i, col);
      strip.show();
      Serial.printf("↱ Slot %u deactivated\n", vote);
    }
  }

  // WiFi reconnection
  if (!wifiOK && now % 10000 < 50) {
    Serial.println("Reconnecting WiFi…");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}
