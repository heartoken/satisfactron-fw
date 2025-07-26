#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_NeoPixel.h>
#include "ota_update.h"
#include "device_config.h"

// —— CONFIG ———————————————————————————————————————
#define DEFAULT_DEVICE_ID "d6e82982-697f-11f0-bb42-fbdbc29338ec"
#define BASE_URL       "https://satisfactron.vercel.app"
#define API_ENDPOINT   "/api/votes"
#define TEST_URL       "https://satisfactron.vercel.app/"
#define LED_ERR_PIN    1
#define FIRMWARE_VERSION "25.07.26.2"

// —— HARDWARE —————————————————————————————————————
#define PIN_RGBLED_3V3  9
#define NUMPIXELS       5

const uint8_t uswPins[NUMPIXELS] = {13, 14, 21, 47, 48}; // USW5-1

Adafruit_NeoPixel strip(NUMPIXELS, PIN_RGBLED_3V3, NEO_RGB + NEO_KHZ800);

// —— GLOBAL INSTANCES —————————————————————————————
extern DeviceConfig deviceConfig; // Use extern - defined in device_config.cpp
OTAManager ota; // Add OTA instance
volatile uint32_t lastAnyVoteTime = 0; // Add missing variable
volatile bool otaInProgress = false; // ADD THIS - flag to disable voting during OTA

// —— TIMINGS —————————————————————————————————————
const uint32_t HOLD_EXTEND_MS = 2000;  // extend hold time per vote
const uint32_t DEBOUNCE_MS    = 500;
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
TaskHandle_t otaTaskHandle = NULL;

void queueVote(uint8_t vote) {
  // BLOCK VOTING DURING OTA
  if (otaInProgress) {
    Serial.printf("🚫 Vote blocked - OTA in progress\n");
    return;
  }
  
  uint8_t nextTail = (queueTail + 1) % QUEUE_SIZE;
  if (nextTail != queueHead) {
    voteQueue[queueTail] = {vote, millis()};
    queueTail = nextTail;
    lastAnyVoteTime = millis(); // Update global vote time for OTA
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
  
  // FIXED: Use the correct JSON format that works
  String payload = "{\"deviceId\":\"" + deviceConfig.getDeviceId() + "\",\"voteValue\":" + String(vote) + "}";
  
  int httpResponseCode = http.POST(payload);
  String response = http.getString();
  
  if (httpResponseCode == 201) {
    Serial.printf("✅ Vote sent: %d (HTTP %d)\n", vote, httpResponseCode);
  } else {
    Serial.printf("❌ Vote failed: %d (HTTP %d) %s\n", vote, httpResponseCode, response.c_str());
  }
  
  http.end();
}

void httpTask(void *parameter) {
  while (true) {
    // BLOCK HTTP TASK DURING OTA  
    if (!otaInProgress && queueHead != queueTail && WiFi.status() == WL_CONNECTED && serverOK) {
      Vote vote = voteQueue[queueHead];
      uint8_t nextHead = (queueHead + 1) % QUEUE_SIZE;
      
      sendVote(vote.value);
      queueHead = nextHead;
      Serial.printf("📤 Sent vote %u\n", vote.value);
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void setAllLEDsPurple() {
  otaInProgress = true; // SET FLAG BEFORE CHANGING LEDS
  for (int i = 0; i < NUMPIXELS; i++) {
    strip.setPixelColor(i, strip.Color(128, 0, 128)); // Purple
  }
  strip.show();
  Serial.println("💜 LEDs set to purple for OTA - VOTING DISABLED");
}

void restoreNormalLEDs() {
  uint32_t color = (WiFi.status() == WL_CONNECTED && serverOK) ? 
                   strip.Color(0, 0, 255) : strip.Color(255, 0, 0);
  updateIdleColors(color);
  otaInProgress = false; // CLEAR FLAG AFTER RESTORING LEDS
  Serial.println("🔄 LEDs restored to normal - VOTING ENABLED");
}

void otaTask(void *parameter) {
  // Wait for WiFi to be ready
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("⏳ OTA task waiting for WiFi...");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  
  Serial.println("🔄 OTA task started - WiFi ready");
  
  // Regular OTA handling
  while (true) {
    ota.handle();
    vTaskDelay(pdMS_TO_TICKS(60000)); // Check every 1 minute (not 5 seconds - too frequent)
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Give serial time to connect
  Serial.println("\n\n=== Satisfactron Device Starting ===");
  
  // Initialize device config
  if (!deviceConfig.init()) {
    Serial.println("❌ Device config init failed");
  }
  deviceConfig.printDeviceId();
  deviceConfig.printWiFiConfig();
  
  // Initialize hardware
  pinMode(LED_ERR_PIN, OUTPUT);
  digitalWrite(LED_ERR_PIN, HIGH); // Start with error LED ON
  
  for (uint8_t i = 0; i < NUMPIXELS; i++) {
    pinMode(uswPins[i], INPUT_PULLUP);
  }
  
  strip.begin();
  strip.clear();
  updateIdleColors(strip.Color(255, 0, 0)); // Start with RED LEDs - not ready
  
  // Initialize WiFi from NVS
  String wifiSSID = deviceConfig.getWiFiSSID();
  String wifiPass = deviceConfig.getWiFiPassword();
  
  if (wifiSSID.length() == 0) {
    Serial.println("❌ No WiFi SSID configured in NVS");
    updateIdleColors(strip.Color(255, 0, 0)); // Keep red
    while (true) delay(1000); // Halt execution
  }
  
  Serial.printf("🌐 Connecting to WiFi: %s\n", wifiSSID.c_str());
  WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
  
  // Wait for WiFi with timeout
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) { // Increased timeout
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("✅ WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    
    // Initialize OTA AFTER WiFi is connected
    Serial.println("🔧 Initializing OTA Manager...");
    ota.init(FIRMWARE_VERSION);
    ota.setPreUpdateCallback(setAllLEDsPurple);
    ota.setPostUpdateCallback(restoreNormalLEDs);
    
    // Force boot-time update check (bypass shouldCheckForUpdate)
    Serial.println("🔍 BOOT: Forcing OTA update check...");
    bool updateAvailable = ota.forceCheckForUpdate();
    
    if (updateAvailable) {
      Serial.println("📦 BOOT: Update available, starting OTA...");
      bool updateSuccess = ota.performUpdate();
      if (!updateSuccess) {
        Serial.println("❌ BOOT: Update failed, continuing with current version...");
      }
      // If successful, device will reboot and we won't reach here
    } else {
      Serial.println("✅ BOOT: No update needed, continuing...");
    }
    
  } else {
    Serial.println("❌ WiFi connection failed - OTA disabled");
    updateIdleColors(strip.Color(255, 0, 0)); // Red for error
  }
  
  // Create HTTP task on Core 1
  xTaskCreatePinnedToCore(
    httpTask,        // Function
    "HTTPTask",      // Name
    16384,           // Increased stack size
    NULL,            // Parameter
    2,               // Priority
    &httpTaskHandle, // Task handle
    1                // Core 1
  );
  
  // Create OTA task on Core 0 (only if WiFi connected)
  if (WiFi.status() == WL_CONNECTED) {
    xTaskCreatePinnedToCore(
      otaTask,         // Function
      "OTATask",       // Name
      16384,           // Increased stack size
      NULL,            // Parameter
      1,               // Priority
      &otaTaskHandle,  // Task handle
      0                // Core 0
    );
  }
  
  Serial.println("=== Setup Complete ===\n");
}

void loop() {
  uint32_t now = millis();
  bool wifiOK = (WiFi.status() == WL_CONNECTED);

  // WiFi state changes
  if (wifiOK && !wifiPrev) {
    Serial.printf("✅ WiFi up, IP %s\n", WiFi.localIP().toString().c_str());
    lastCheck = 0; // Force immediate server check
  }
  if (!wifiOK && wifiPrev) {
    Serial.println("❌ WiFi down");
    serverOK = false;
    digitalWrite(LED_ERR_PIN, HIGH); // Turn on error LED
    updateIdleColors(strip.Color(255, 0, 0));
  }
  wifiPrev = wifiOK;

  // Server connectivity check
  if (wifiOK && !serverOK && now - lastCheck >= CHECK_MS) {
    lastCheck = now;
    Serial.println("… testing server reachability");
    if (testServer()) {
      Serial.println("✅ Server reachable - System ready!");
      serverOK = true;
      digitalWrite(LED_ERR_PIN, LOW); // Turn OFF error LED - all good!
      updateIdleColors(strip.Color(0, 0, 255)); // NOW turn LEDs blue - ready!
    } else {
      Serial.println("❌ Server not reachable");
      digitalWrite(LED_ERR_PIN, HIGH); // Keep error LED on
      updateIdleColors(strip.Color(255, 0, 0)); // Keep LEDs red
    }
  }

  // Button scanning and vote processing - SKIP IF OTA IN PROGRESS
  if (!otaInProgress) {
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
  }

  // WiFi reconnection - use NVS credentials
  if (!wifiOK && now % 10000 < 50) {
    Serial.println("Reconnecting WiFi…");
    WiFi.disconnect();
    String ssid = deviceConfig.getWiFiSSID();
    String pass = deviceConfig.getWiFiPassword();
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
}
